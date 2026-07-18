using System.Collections.Concurrent;
using System.Net.Sockets;
using System.Text;

namespace HisServer.Domain;

/// <summary>
/// Tracks the live TCP stream for each bed's gateway connection, so the REST
/// API can send a command DOWN to the device (e.g. a doctor changing the
/// target infusion rate) over the SAME socket the gateway already opened to
/// forward vitals up. The gateway is the TCP client here, so there is no
/// separate "connect to the gateway" path - a command can only be delivered
/// while the gateway's connection is open (i.e. after it has sent at least
/// one vitals reading).
///
/// A <see cref="SemaphoreSlim"/> per bed serializes writes, since two API
/// calls for the same bed could otherwise race and interleave bytes on the
/// wire. Reads happen concurrently on a different task
/// (<see cref="BedTcpIngestionService"/>), which is safe - one reader plus
/// one serialized writer on the same NetworkStream do not conflict.
/// </summary>
public sealed class BedConnectionRegistry
{
    private sealed record Connection(NetworkStream Stream, SemaphoreSlim WriteLock);

    private readonly ConcurrentDictionary<string, Connection> connections = new(StringComparer.OrdinalIgnoreCase);

    public void Register(string bedId, NetworkStream stream)
    {
        connections[bedId] = new Connection(stream, new SemaphoreSlim(1, 1));
    }

    public void Unregister(string bedId)
    {
        connections.TryRemove(bedId, out _);
    }

    /// <summary>
    /// Sends a single newline-terminated JSON command line to the bed's
    /// gateway connection. Returns false if the bed has no live connection
    /// (gateway not yet connected, or offline) or the write failed.
    /// </summary>
    public async Task<bool> TrySendCommandAsync(string bedId, string jsonLine, CancellationToken cancellationToken = default)
    {
        if (!connections.TryGetValue(bedId, out var connection))
        {
            return false;
        }

        var bytes = Encoding.UTF8.GetBytes(jsonLine.EndsWith('\n') ? jsonLine : jsonLine + "\n");

        await connection.WriteLock.WaitAsync(cancellationToken);
        try
        {
            await connection.Stream.WriteAsync(bytes, cancellationToken);
            return true;
        }
        catch (IOException)
        {
            // Connection died between the registry check and the write - the
            // ingestion service's read loop will notice and Unregister() on
            // its own; nothing more to do here.
            return false;
        }
        finally
        {
            connection.WriteLock.Release();
        }
    }
}
