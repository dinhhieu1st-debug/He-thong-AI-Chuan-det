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
    string HeaderText);

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

    private readonly string directory;
    private readonly ILogger<OtaImageStore> log;

    public OtaImageStore(IWebHostEnvironment env, ILogger<OtaImageStore> log)
    {
        this.log = log;
        directory = Path.Combine(env.ContentRootPath, "ota-images");
        Directory.CreateDirectory(directory);
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
            if (image is not null) images.Add(image with { SizeBytes = info.Length });
        }

        return images.OrderByDescending(i => i.FileVersion).ToList();
    }

    public string? PathFor(string fileName)
    {
        var safe = Path.GetFileName(fileName);
        var path = Path.Combine(directory, safe);
        return File.Exists(path) ? path : null;
    }

    public bool Delete(string fileName)
    {
        var path = PathFor(fileName);
        if (path is null) return false;
        File.Delete(path);
        log.LogInformation("OTA image deleted: {File}", Path.GetFileName(path));
        return true;
    }
}
