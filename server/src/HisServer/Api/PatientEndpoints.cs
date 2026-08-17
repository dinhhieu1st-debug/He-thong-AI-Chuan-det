using HisServer.Data;
using HisServer.Domain;
using HisServer.Hubs;
using HisServer.Models;
using Microsoft.AspNetCore.SignalR;

namespace HisServer.Api;

/// <summary>
/// Admitting a patient to a bed, and discharging them.
///
/// Nurses own this: they are the ones at the bedside when a patient arrives.
/// It is separate from the bed's own settings (room, targets) because the bed
/// outlives the patient in it - discharging must not disturb anything the
/// device is doing.
/// </summary>
public static class PatientEndpoints
{
    public sealed record AdmitRequest(string? PatientName, string? PatientCode, DateTime? AdmittedAt);

    public static void MapPatientEndpoints(this WebApplication app)
    {
        var group = app.MapGroup("/api/beds")
            .RequireAuthorization(Capabilities.EditPatient);

        group.MapPut("/{bedId}/patient", async (
            string bedId,
            AdmitRequest request,
            BedStateStore store,
            BedRepository repository,
            IHubContext<MonitoringHub, IMonitoringClient> hub) =>
        {
            if (store.Get(bedId) is null)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            }

            var name = string.IsNullOrWhiteSpace(request.PatientName)
                ? null : request.PatientName.Trim();
            var code = string.IsNullOrWhiteSpace(request.PatientCode)
                ? null : request.PatientCode.Trim();

            // Clearing the name IS the discharge: an empty bed keeps no leftover
            // code or admission time to be misread as the next patient's.
            if (name is null)
            {
                code = null;
            }

            // Admission time defaults to now on a new admission, and is kept as
            // it was when only the spelling of a name is being corrected.
            var admittedAt = name is null
                ? (DateTime?)null
                : request.AdmittedAt ?? store.Get(bedId)?.AdmittedAt ?? DateTime.UtcNow;

            var bed = store.Upsert(bedId, state =>
            {
                state.PatientName = name;
                state.PatientCode = code;
                state.AdmittedAt = admittedAt;
            });

            await repository.UpdatePatientAsync(bedId, name, code, admittedAt);
            await hub.Clients.Group(MonitoringHub.WardGroup).BedUpdated(BedDto.From(bed));

            return Results.Ok(BedDto.From(bed));
        });
    }
}
