# HIS Server

Hospital bed vitals monitoring server: ingests live vitals from bedside IoT
devices over TCP, stores history in MySQL, serves a real-time web dashboard,
a REST API for a companion mobile app, and FCM push notifications for
critical/warning alerts.

Runs cross-platform (Linux and Windows) on ASP.NET Core / Kestrel — this
replaces a previous Windows-only WinForms implementation.

## Project layout

```
src/HisServer/        ASP.NET Core app (backend + static web frontend)
  Api/                 REST endpoint definitions
  Hubs/                SignalR hub (live push to the browser)
  Ingestion/            TCP vitals listener (port 5000) + wire-format parser
  Domain/                Status thresholds, alert-transition rules, offline scanner
  Data/                  MySQL access (Dapper + MySqlConnector)
  Services/              Vitals-save throttling, FCM push
  Models/                Shared DTOs
  wwwroot/                Static web frontend (no build step — plain HTML/CSS/JS)
database/schema.sql     MySQL schema (run once before first launch)
HisServer.sln
```

## Prerequisites

- .NET 8 SDK
- MySQL 8.x reachable from the server
- (Optional) a Firebase service account JSON file, if you want mobile push notifications

## Database setup

```bash
mysql -u <user> -p < database/schema.sql
```

This creates the `his_server` database and all required tables. Safe to
re-run (`CREATE TABLE IF NOT EXISTS`).

## Configuration

All configuration is standard ASP.NET Core config — `appsettings.json`,
environment variables, or `dotnet user-secrets` for local development. There
is **no hardcoded fallback** for the database connection string; the app
fails fast at startup if it isn't set.

| Setting | Env var | Default | Notes |
|---|---|---|---|
| MySQL connection string | `ConnectionStrings__MySql` | *(required, no default)* | e.g. `Server=localhost;Port=3306;Database=his_server;Uid=...;Pwd=...;` |
| Bed vitals TCP ingestion port | `Tcp__Port` | `5000` | Newline-delimited JSON, one reading per line |
| Vitals history save interval | `VitalsSave__IntervalSeconds` | `10` | How often a bed's readings get written to `vital_samples` |
| Offline detection threshold | `Offline__ThresholdSeconds` | `3` | No data within this window ⇒ bed marked Offline |
| Offline scan interval | `Offline__ScanIntervalSeconds` | `1` | How often the offline scanner runs |
| Firebase project ID | `Firebase__ProjectId` | *(empty = push disabled)* | |
| Firebase service account path | `FPT_FIREBASE_SERVICE_ACCOUNT` | *(empty = push disabled)* | Path to the service account JSON file |

Local development example:

```bash
cd src/HisServer
dotnet user-secrets set "ConnectionStrings:MySql" "Server=localhost;Port=3306;Database=his_server;Uid=root;Pwd=yourpassword;SslMode=None;AllowPublicKeyRetrieval=True;"
dotnet run
```

The web console is served at `http://localhost:5000/` (or whatever port
Kestrel binds to) and the bed vitals TCP listener runs on the port from
`Tcp:Port` (default `5000` — override one of the two if running both on the
same host, since they'd otherwise collide).

## Bed vitals wire format

Devices/gateways connect over TCP and send one JSON object per line:

```json
{"bedId":"BED-01","room":"ICU-1","spo2":98,"heartRate":75,"temperature":36.8,"dripRate":20}
```

Field names are matched case-insensitively with several accepted aliases per
field (kept for backward compatibility with existing firmware) — see
`Ingestion/BedDataParser.cs` for the full alias list.

## Deployment notes

- The app is a single self-contained Kestrel process — run it behind a
  reverse proxy (nginx, Caddy, IIS) or an OS-level tunnel if it needs to be
  reachable outside the local network. The app itself does not spawn any
  tunnel process.
- On Linux, run it as a `systemd` service (`dotnet src/HisServer/bin/Release/net8.0/HisServer.dll`).
- On Windows, run it as a Windows Service or via `dotnet run` / the published
  executable — no admin rights are required for the HTTP port (unlike the
  old `HttpListener`-based implementation).
