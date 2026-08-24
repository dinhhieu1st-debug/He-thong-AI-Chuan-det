using HisServer.Domain;
using HisServer.Services;

namespace HisServer.Api;

/// <summary>
/// Uploading firmware images, and serving the index zigbee2mqtt reads.
///
/// TWO AUDIENCES, TWO AUTHORISATION RULES, on purpose:
///
///   - The technician's browser uploads, lists and deletes. Behind
///     ManageDevices, like everything else that can change a device.
///
///   - zigbee2mqtt fetches the index and the image files. It runs on the Pi as
///     a service and has no login, so those two endpoints are anonymous.
///
/// That means anyone on the LAN can download the firmware images. That is
/// acceptable here and worth stating plainly: the images are the team's own
/// application binaries, not patient data, and they are already readable on the
/// Pi by anyone with the ssh password from the README. What must NOT become
/// anonymous is upload or delete - being able to put a file into this folder is
/// being able to choose what runs on a bedside monitor.
/// </summary>
public static class OtaImageEndpoints
{
    public static void MapOtaImageEndpoints(this IEndpointRouteBuilder app)
    {
        var group = app.MapGroup("/api/ota");

        group.MapGet("/images", (OtaImageStore store) => Results.Ok(store.List()))
             .RequireAuthorization(Capabilities.ManageDevices);

        group.MapPost("/images", async (HttpRequest request, OtaImageStore store) =>
        {
            if (!request.HasFormContentType)
                return Results.BadRequest(new { error = "Expected multipart/form-data." });

            var form = await request.ReadFormAsync();
            var file = form.Files.GetFile("file");
            if (file is null || file.Length == 0)
                return Results.BadRequest(new { error = "No file selected." });

            try
            {
                using var stream = file.OpenReadStream();
                var image = await store.SaveAsync(file.FileName, stream);
                return Results.Ok(image);
            }
            catch (InvalidDataException ex)
            {
                /* The header did not parse. Saying so precisely matters: the
                 * most likely mistake is picking the .gbl or the .s37 sitting
                 * next to the .ota in the same build folder, and "invalid
                 * file" would leave the technician re-uploading the same wrong
                 * one. */
                return Results.BadRequest(new { error = ex.Message });
            }
        }).RequireAuthorization(Capabilities.ManageDevices)
          .DisableAntiforgery();

        group.MapDelete("/images/{fileName}", (string fileName, OtaImageStore store) =>
            store.Delete(fileName) ? Results.NoContent() : Results.NotFound())
             .RequireAuthorization(Capabilities.ManageDevices);

        /* ---- what zigbee2mqtt reads ---------------------------------------
         *
         * The index is generated, never hand-edited. Each entry's manufacturer
         * code, image type and version are read back out of the file itself,
         * so an index can no longer disagree with the image it points at - the
         * exact defect found in the branch's xg24_ota_index.json, which
         * declared manufacturer 4098 while pointing at a 4169 image. */
        group.MapGet("/index.json", (OtaImageStore store, HttpRequest request) =>
        {
            var baseUrl = $"{request.Scheme}://{request.Host}";

            return Results.Ok(store.List().Select(i => new
            {
                url = $"{baseUrl}/api/ota/images/{Uri.EscapeDataString(i.FileName)}/download",
                manufacturerCode = i.ManufacturerCode,
                imageType = i.ImageType,
                fileVersion = i.FileVersion,
                fileSize = i.SizeBytes,
            }));
        }).AllowAnonymous();

        group.MapGet("/images/{fileName}/download", (string fileName, OtaImageStore store) =>
        {
            var path = store.PathFor(fileName);
            return path is null
                ? Results.NotFound()
                : Results.File(path, "application/octet-stream");
        }).AllowAnonymous();
    }
}
