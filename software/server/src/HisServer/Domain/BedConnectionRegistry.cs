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
    private sealed class HttpGateway
    {
        public ConcurrentQueue<string> Commands { get; } = new();
        public DateTime LastSeenUtc { get; set; } = DateTime.UtcNow;
    }

    private readonly ConcurrentDictionary<string, Connection> connections = new(StringComparer.OrdinalIgnoreCase);
    private readonly ConcurrentDictionary<string, HttpGateway> httpGateways = new(StringComparer.OrdinalIgnoreCase);
    private readonly ConcurrentDictionary<string, string> httpBedGateways = new(StringComparer.OrdinalIgnoreCase);
    private static readonly TimeSpan HttpGatewayOnlineWindow = TimeSpan.FromSeconds(30);

    public void Register(string bedId, NetworkStream stream)
    {
        connections[bedId] = new Connection(stream, new SemaphoreSlim(1, 1));
    }

    public void RegisterHttp(string bedId, string gatewayId)
    {
        var gateway = httpGateways.GetOrAdd(gatewayId, _ => new HttpGateway());
        gateway.LastSeenUtc = DateTime.UtcNow;
        httpBedGateways[bedId] = gatewayId;
    }

    public IReadOnlyList<string> DrainHttpCommands(string gatewayId)
    {
        var gateway = httpGateways.GetOrAdd(gatewayId, _ => new HttpGateway());
        gateway.LastSeenUtc = DateTime.UtcNow;

        var commands = new List<string>();
        while (gateway.Commands.TryDequeue(out var command))
        {
            commands.Add(command);
        }
        return commands;
    }

    /// <summary>
    /// Sends the same line to every connected gateway.
    ///
    /// Used for commands that are not about one bed - "re-read your device
    /// inventory" concerns the gateway itself, and a ward may have one gateway
    /// per room. Returns how many accepted it, so the caller can tell "sent to
    /// two gateways" from "no gateway is connected".
    /// </summary>
    public async Task<int> BroadcastCommandAsync(string jsonLine, CancellationToken cancellationToken = default)
    {
        /* Once per SOCKET, not once per bed. A Pi gateway forwards every
         * device paired to it, so several beds share one connection, and a
         * broadcast command is addressed to the gateway rather than to a bed -
         * "re-read your device inventory" concerns the gateway itself. Sending
         * it per bed wrote the same line down the same socket three times and
         * had the gateway publish the same MQTT request three times.
         *
         * The count is therefore gateways reached, which is what the caller
         * wants to report. */
        var delivered = 0;
        var reached = new HashSet<NetworkStream>();

        foreach (var (bedId, connection) in connections)
        {
            if (!reached.Add(connection.Stream))
            {
                continue;
            }

            if (await TrySendCommandAsync(bedId, jsonLine, cancellationToken))
            {
                delivered++;
            }
        }


        var reachedHttpGateways = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var gatewayId in httpBedGateways.Values)
        {
            if (!reachedHttpGateways.Add(gatewayId)
                || !httpGateways.TryGetValue(gatewayId, out var gateway)
                || DateTime.UtcNow - gateway.LastSeenUtc > HttpGatewayOnlineWindow)
            {
                continue;
            }

            gateway.Commands.Enqueue(jsonLine);
            delivered++;
        }
        return delivered;
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
            if (!httpBedGateways.TryGetValue(bedId, out var gatewayId)
                || !httpGateways.TryGetValue(gatewayId, out var gateway)
                || DateTime.UtcNow - gateway.LastSeenUtc > HttpGatewayOnlineWindow)
            {
                return false;
            }

            gateway.Commands.Enqueue(jsonLine);
            return true;
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
