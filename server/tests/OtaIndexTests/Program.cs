// Tests for the OTA image library / index that zigbee2mqtt reads, covering the
// v2 -> v3 bridge -> v7 main upgrade path: a v2 device must never be offered
// the full image, and version/manufacturer/imageType gating must be real
// (computed by OtaImageStore from actual file headers and sidecar policies),
// not just labels.
//
// Deliberately a plain console program rather than an xUnit project, matching
// EvaluatorTests: no NuGet restore needed, runs on a machine with no network.
//
//   dotnet run --project server/tests/OtaIndexTests

using System.Buffers.Binary;
using HisServer.Services;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.FileProviders;
using Microsoft.Extensions.Logging.Abstractions;

var failures = 0;

void Check(string what, bool ok, string detail)
{
    Console.WriteLine($"  [{(ok ? "PASS" : "FAIL")}] {what,-58} {detail}");
    if (!ok) failures++;
}

byte[] MakeOtaHeader(ushort manufacturer, ushort imageType, uint fileVersion, string headerText)
{
    var data = new byte[56];
    BinaryPrimitives.WriteUInt32LittleEndian(data, 0x0BEEF11E);
    BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(10), manufacturer);
    BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(12), imageType);
    BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(14), fileVersion);
    var textBytes = System.Text.Encoding.ASCII.GetBytes(headerText);
    Array.Copy(textBytes, 0, data, 20, Math.Min(textBytes.Length, 32));
    return data;
}

const ushort XG26_MFG = 0x1049;
const ushort XG26_IMAGE_TYPE = 0;

var root = Path.Combine(Path.GetTempPath(), "ota-index-tests-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(root);
var otaImagesPath = Path.Combine(root, "ota-images");
Directory.CreateDirectory(otaImagesPath);

// The v3 bridge: protected filename (so OtaImageStore applies maxFileVersion
// = 2 automatically, the same real path production relies on), small enough
// that a v2 device with the legacy ~256 KiB window could store it.
File.WriteAllBytes(
    Path.Combine(otaImagesPath, "smart-iv-ota-bridge-v3.ota"),
    MakeOtaHeader(XG26_MFG, XG26_IMAGE_TYPE, 3, "ICTU SmartIV OTA bridge v3"));

// The v7 full application: gated to v3+ only via an explicit sidecar policy,
// the same mechanism used for the real smart-iv-monitor-v7.ota so the test
// does not need a multi-hundred-KB fixture to trip the oversize heuristic.
File.WriteAllBytes(
    Path.Combine(otaImagesPath, "smart-iv-monitor-v7.ota"),
    MakeOtaHeader(XG26_MFG, XG26_IMAGE_TYPE, 7, "ICTU SmartIV XG26 v7"));
File.WriteAllText(
    Path.Combine(otaImagesPath, "smart-iv-monitor-v7.ota.policy.json"),
    """{ "minFileVersion": 3 }""");

// A wrong-manufacturer and a wrong-image-type image: neither should ever be
// offered to the XG26 device, regardless of version.
File.WriteAllBytes(
    Path.Combine(otaImagesPath, "other-mfg.ota"),
    MakeOtaHeader(0x9999, XG26_IMAGE_TYPE, 5, "Someone else's device"));
File.WriteAllBytes(
    Path.Combine(otaImagesPath, "other-type.ota"),
    MakeOtaHeader(XG26_MFG, 99, 5, "Wrong image type"));

var env = new FakeWebHostEnvironment { ContentRootPath = root };
var store = new OtaImageStore(env, NullLogger<OtaImageStore>.Instance);
var images = store.List();

Console.WriteLine("\n== OtaImageStore.List() reads real header + policy fields ==");
{
    var bridge = images.SingleOrDefault(i => i.FileName == "smart-iv-ota-bridge-v3.ota");
    Check("bridge present", bridge is not null, bridge?.FileName ?? "missing");
    Check("bridge fileVersion == 3", bridge?.FileVersion == 3, $"{bridge?.FileVersion}");
    Check("bridge is protected (Required bridge)", bridge?.IsProtected == true, $"{bridge?.IsProtected}");
    Check("bridge maxFileVersion == 2 (auto, from protected-name rule)", bridge?.MaxFileVersion == 2, $"{bridge?.MaxFileVersion}");
    Check("bridge minFileVersion is unset", bridge?.MinFileVersion is null, $"{bridge?.MinFileVersion}");

    var main = images.SingleOrDefault(i => i.FileName == "smart-iv-monitor-v7.ota");
    Check("main present", main is not null, main?.FileName ?? "missing");
    Check("main fileVersion == 7", main?.FileVersion == 7, $"{main?.FileVersion}");
    Check("main minFileVersion == 3 (from sidecar policy)", main?.MinFileVersion == 3, $"{main?.MinFileVersion}");
    Check("main maxFileVersion is unset", main?.MaxFileVersion is null, $"{main?.MaxFileVersion}");
    Check("main is not protected", main?.IsProtected == false, $"{main?.IsProtected}");
}

// Mirrors the matching zigbee2mqtt performs against the generated index
// (manufacturerCode, imageType, fileVersion > installed, and the optional
// minFileVersion/maxFileVersion window) - the constraint that actually keeps
// a v2 device off the v7 image lives in zigbee2mqtt's own selection, not in
// this server, so the test proves the INDEX carries the right numbers for
// that selection to be safe.
OtaImage? SelectOffer(IReadOnlyList<OtaImage> candidates, ushort deviceMfg, ushort deviceImageType, uint installedVersion) =>
    candidates
        .Where(i => i.ManufacturerCode == deviceMfg)
        .Where(i => i.ImageType == deviceImageType)
        .Where(i => i.FileVersion > installedVersion)
        .Where(i => i.MinFileVersion is null || installedVersion >= i.MinFileVersion)
        .Where(i => i.MaxFileVersion is null || installedVersion <= i.MaxFileVersion)
        .OrderByDescending(i => i.FileVersion)
        .FirstOrDefault();

Console.WriteLine("\n== v2 -> v3 bridge -> v7 upgrade sequencing (as zigbee2mqtt would apply it) ==");
{
    var offeredAtV2 = SelectOffer(images, XG26_MFG, XG26_IMAGE_TYPE, 2);
    Check("v2 -> offered the v3 bridge, never v7", offeredAtV2?.FileName == "smart-iv-ota-bridge-v3.ota", offeredAtV2?.FileName ?? "none");

    var offeredAtV3 = SelectOffer(images, XG26_MFG, XG26_IMAGE_TYPE, 3);
    Check("v3 -> offered v7", offeredAtV3?.FileName == "smart-iv-monitor-v7.ota", offeredAtV3?.FileName ?? "none");

    var offeredAtV4 = SelectOffer(images, XG26_MFG, XG26_IMAGE_TYPE, 4);
    Check("v4 -> offered v7", offeredAtV4?.FileName == "smart-iv-monitor-v7.ota", offeredAtV4?.FileName ?? "none");

    var offeredAtV6 = SelectOffer(images, XG26_MFG, XG26_IMAGE_TYPE, 6);
    Check("v6 -> offered v7", offeredAtV6?.FileName == "smart-iv-monitor-v7.ota", offeredAtV6?.FileName ?? "none");

    var offeredAtV7 = SelectOffer(images, XG26_MFG, XG26_IMAGE_TYPE, 7);
    Check("v7 -> up to date, nothing offered", offeredAtV7 is null, offeredAtV7?.FileName ?? "none (up to date)");
}

Console.WriteLine("\n== manufacturer / image type isolation ==");
{
    // A device whose manufacturerCode matches neither the XG26 images (0x1049)
    // nor the stray other-mfg.ota fixture (0x9999) - nothing in the library
    // should ever match it.
    var wrongMfg = SelectOffer(images, 0x2222, XG26_IMAGE_TYPE, 2);
    Check("device with a different manufacturerCode -> nothing offered", wrongMfg is null, wrongMfg?.FileName ?? "none");

    var wrongType = SelectOffer(images, XG26_MFG, 42, 2);
    Check("device with a different imageType -> nothing offered", wrongType is null, wrongType?.FileName ?? "none");
}

Directory.Delete(root, recursive: true);

Console.WriteLine($"\n{(failures == 0 ? "ALL PASS" : $"{failures} FAILURE(S)")}");
Environment.Exit(failures == 0 ? 0 : 1);

sealed class FakeWebHostEnvironment : IWebHostEnvironment
{
    public string ContentRootPath { get; set; } = "";
    public string WebRootPath { get; set; } = "";
    public IFileProvider WebRootFileProvider { get; set; } = new NullFileProvider();
    public IFileProvider ContentRootFileProvider { get; set; } = new NullFileProvider();
    public string ApplicationName { get; set; } = "OtaIndexTests";
    public string EnvironmentName { get; set; } = "Test";
}
