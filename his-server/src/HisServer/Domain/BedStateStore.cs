using System.Collections.Concurrent;
using HisServer.Models;

namespace HisServer.Domain;

/// <summary>
/// Single in-memory source of truth for "current" bed state, shared by the TCP
/// ingestion service, the offline scanner, the REST API, and the SignalR hub.
/// </summary>
public sealed class BedStateStore
{
    private readonly ConcurrentDictionary<string, BedState> beds = new(StringComparer.OrdinalIgnoreCase);

    public IReadOnlyCollection<BedState> GetAll() => beds.Values.ToList();

    public BedState? Get(string bedId) => beds.GetValueOrDefault(bedId);

    public BedState Upsert(string bedId, Action<BedState> mutate)
    {
        return beds.AddOrUpdate(
            bedId,
            _ =>
            {
                var created = new BedState { BedId = bedId, Room = string.Empty };
                mutate(created);
                return created;
            },
            (_, existing) =>
            {
                mutate(existing);
                return existing;
            });
    }

    public void Remove(string bedId) => beds.TryRemove(bedId, out _);
}
