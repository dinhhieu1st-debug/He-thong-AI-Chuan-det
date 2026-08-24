using System.Buffers.Binary;

namespace HisServer.Services;

/// <summary>
/// One uploaded firmware image, described by its own header rather than by
/// whatever the file happened to be called.
/// </summary>
public sealed record OtaImage(
    string FileName,
    ushort ManufacturerCode,
    ushort ImageType,
    uint FileVersion,
    long SizeBytes,
    DateTime UploadedAt,
    string HeaderText,
    uint? MinFileVersion = null,
    uint? MaxFileVersion = null,
    bool IsProtected = false);

/// <summary>
/// Optional per-image selection rules understood by zigbee2mqtt.  They are
/// stored next to an image as &lt;image-name&gt;.policy.json so a small bridge
/// image and the full image can safely coexist in one OTA index.
/// </summary>
public sealed record OtaImagePolicy(uint? MinFileVersion, uint? MaxFileVersion);

/// <summary>
/// Holds the .ota images a technician has uploaded, and publishes the index
/// zigbee2mqtt reads.
///
/// Why the server holds them at all: before this, releasing firmware meant
/// scp'ing a file to the Pi and hand-editing a JSON file over ssh. That is a
/// step nobody can do from the ward, and hand-editing the index is exactly
/// where a wrong manufacturer code slips in. z2m can fetch both the index and
/// the images over HTTP, so pointing it at this server removes the ssh step
/// entirely: upload through the browser and the image is live.
/// </summary>
public sealed class OtaImageStore
{
    /* An OTA file starts with this. Anything else is not one, whatever it is
     * named - and a technician picking the wrong file from a folder is the
     * likeliest way a bad image gets published. */
    private const uint OtaMagic = 0x0BEEF11E;
    // v2 reserves 256 KiB, but the Silicon Labs simple-storage driver keeps
    // 2 KiB for its byte mask and OTA header metadata.
    private const long LegacyOtaMaxImageBytes = 262144 - 2048;
    private const uint BridgeVersion = 3;
    private const string BridgeFileName = "smart-iv-ota-bridge-v3.ota";

    private readonly string directory;
    private readonly ILogger<OtaImageStore> log;

    public OtaImageStore(IWebHostEnvironment env, ILogger<OtaImageStore> log)
    {
        this.log = log;
        directory = Path.Combine(env.ContentRootPath, "ota-images");
        Directory.CreateDirectory(directory);
        EnsureSystemBridge(env.ContentRootPath);
        PromotePendingImages();
    }

    public string DirectoryPath => directory;

    /// <summary>
    /// Reads the ZCL OTA header. Returns null when this is not an OTA file.
    /// </summary>
    public static OtaImage? ReadHeader(string fileName, ReadOnlySpan<byte> data,
                                       DateTime uploadedAt)
    {
        if (data.Length < 56) return null;

        var magic = BinaryPrimitives.ReadUInt32LittleEndian(data);
        if (magic != OtaMagic) return null;

        var manufacturer = BinaryPrimitives.ReadUInt16LittleEndian(data[10..]);
        var imageType    = BinaryPrimitives.ReadUInt16LittleEndian(data[12..]);
        var fileVersion  = BinaryPrimitives.ReadUInt32LittleEndian(data[14..]);

        /* The 32-byte human-readable string the image was built with. Shown in
         * the UI because "4169 / type 0 / version 2" tells a technician far
         * less than "ICTU SmartIV-Sensor v2" about what they are about to
         * push onto a bed. */
        var headerText = System.Text.Encoding.ASCII
            .GetString(data[20..52])
            .TrimEnd('\0', ' ');

        return new OtaImage(fileName, manufacturer, imageType, fileVersion,
                            data.Length, uploadedAt, headerText);
    }

    public async Task<OtaImage> SaveAsync(string originalName, Stream content,
                                          CancellationToken ct = default)
    {
        using var buffer = new MemoryStream();
        await content.CopyToAsync(buffer, ct);
        var bytes = buffer.ToArray();

        var safeName = Path.GetFileName(originalName);
        if (string.IsNullOrWhiteSpace(safeName)) safeName = "firmware.ota";
        if (IsProtected(safeName))
            throw new InvalidDataException(
                "The required v3 OTA bridge is managed by the server and cannot be overwritten.");

        var image = ReadHeader(safeName, bytes, DateTime.UtcNow)
            ?? throw new InvalidDataException(
                "Not a Zigbee OTA image: the file does not start with the 0x0BEEF11E signature. (A .gbl or .s37 from the same build folder is the usual mistake.)");

        await File.WriteAllBytesAsync(Path.Combine(directory, safeName), bytes, ct);
        log.LogInformation(
            "OTA image saved: {File} mfg={Mfg} type={Type} version={Version} ({Size} bytes)",
            safeName, image.ManufacturerCode, image.ImageType, image.FileVersion, bytes.Length);

        return image;
    }

    public IReadOnlyList<OtaImage> List()
    {
        var images = new List<OtaImage>();

        foreach (var path in Directory.EnumerateFiles(directory, "*.ota"))
        {
            byte[] head;
            try
            {
                using var stream = File.OpenRead(path);
                head = new byte[Math.Min(64, stream.Length)];
                stream.ReadExactly(head, 0, head.Length);
            }
            catch (IOException) { continue; }

            var info = new FileInfo(path);
            var image = ReadHeader(info.Name, head, info.LastWriteTimeUtc);
            if (image is not null)
            {
                var policy = ReadPolicy(path);
                var protectedImage = IsProtected(info.Name);
                images.Add(image with
                {
                    SizeBytes = info.Length,
                    // A technician may upload a new full image without knowing
                    // about sidecar policies.  Anything larger than the broken
                    // v2 limit must never be offered until the bridge is installed.
                    MinFileVersion = policy?.MinFileVersion
                        ?? (!protectedImage && info.Length > LegacyOtaMaxImageBytes
                            ? BridgeVersion : null),
                    MaxFileVersion = policy?.MaxFileVersion
                        ?? (protectedImage ? BridgeVersion - 1 : null),
                    IsProtected = protectedImage,
                });
            }
        }

        return images.OrderByDescending(i => i.FileVersion).ToList();
    }

    public string? PathFor(string fileName)
    {
        var safe = Path.GetFileName(fileName);
        var path = Path.Combine(directory, safe);
        return File.Exists(path) ? path : null;
    }

    /// <summary>
    /// Deletes an image and its sidecar files. The protected v3 bridge only
    /// comes out with <paramref name="force"/> set - a technician confirming a
    /// specific warning in the UI, not an accidental click. Note this only
    /// removes the LIVE copy in ota-images: if ota-system/smart-iv-ota-bridge-v3.ota
    /// still exists, <see cref="EnsureSystemBridge"/> re-copies it back on the
    /// next server start, by design (that is how an updated official bridge
    /// gets deployed). Force-deleting here does not touch that source copy.
    /// </summary>
    public bool Delete(string fileName, bool force = false)
    {
        if (IsProtected(fileName) && !force) return false;
        var path = PathFor(fileName);
        if (path is null) return false;
        File.Delete(path);
        foreach (var sidecarSuffix in SidecarSuffixes)
        {
            var sidecarPath = path + sidecarSuffix;
            if (File.Exists(sidecarPath)) File.Delete(sidecarPath);
        }
        log.LogInformation("OTA image deleted: {File}{Forced}",
            Path.GetFileName(path), force ? " (forced)" : "");
        return true;
    }

    // .policy.json is real (see ReadPolicy). .state.json does not exist in this
    // codebase - OTA transfer state lives in OtaStatusRegistry, in memory, not
    // as a per-image file - but the suffix is listed here so deleting an image
    // never leaves an orphaned sidecar if that ever changes.
    private static readonly string[] SidecarSuffixes = [".policy.json", ".state.json"];

    public bool IsProtected(string fileName) =>
        string.Equals(Path.GetFileName(fileName), BridgeFileName,
                      StringComparison.OrdinalIgnoreCase);

    private OtaImagePolicy? ReadPolicy(string imagePath)
    {
        var policyPath = imagePath + ".policy.json";
        if (!File.Exists(policyPath)) return null;

        try
        {
            return System.Text.Json.JsonSerializer.Deserialize<OtaImagePolicy>(
                File.ReadAllText(policyPath),
                new System.Text.Json.JsonSerializerOptions
                {
                    PropertyNameCaseInsensitive = true,
                });
        }
        catch (Exception ex) when (ex is IOException or System.Text.Json.JsonException)
        {
            log.LogWarning(ex, "Ignoring invalid OTA policy {Policy}", policyPath);
            return null;
        }
    }

    private void PromotePendingImages()
    {
        /* A pending suffix lets a new server binary and a new image be copied
         * while the old process is still serving beds.  The old binary ignores
         * the file; this policy-aware binary promotes it during its next manual
         * start, so there is no interval where v2 devices can see the full v7
         * image without the bridge rule. */
        foreach (var pendingPath in Directory.EnumerateFiles(directory, "*.ota.pending"))
        {
            var livePath = pendingPath[..^".pending".Length];
            try
            {
                File.Move(pendingPath, livePath, overwrite: true);
                log.LogInformation("Promoted pending OTA image: {File}", Path.GetFileName(livePath));
            }
            catch (IOException ex)
            {
                log.LogWarning(ex, "Could not promote pending OTA image {File}", pendingPath);
            }
        }
    }

    private void EnsureSystemBridge(string contentRoot)
    {
        var sourceDirectory = Path.Combine(contentRoot, "ota-system");
        var sourceImage = Path.Combine(sourceDirectory, BridgeFileName);
        var liveImage = Path.Combine(directory, BridgeFileName);

        if (File.Exists(sourceImage))
        {
            File.Copy(sourceImage, liveImage, overwrite: true);
            log.LogInformation("Synchronized protected OTA bridge: {File}", BridgeFileName);
        }

        var sourcePolicy = sourceImage + ".policy.json";
        var livePolicy = liveImage + ".policy.json";
        if (File.Exists(sourcePolicy))
            File.Copy(sourcePolicy, livePolicy, overwrite: true);
    }
}
