# Documentation index

System-level (cross-component) documentation lives here. Documentation that
belongs to a single component lives next to that component's code (see the
"Location" column).

| Document | Contents | Location |
|---|---|---|
| [architecture.md](architecture.md) | Data flow, Zigbee protocol, pinout, HR/SpO2 processing and alert logic | `docs/` |
| [setup-guide.md](setup-guide.md) | Setting up the whole system from scratch, in the order the team actually ran it | `docs/` |
| [troubleshooting.md](troubleshooting.md) | Common issues and how to fix them | `docs/` |
| [system-integration.md](system-integration.md) | Firmware ↔ HIS alert contract, how to deploy the converter, how to restart HIS/Pi after a schema change | `docs/` |
| PIN_MAP.md | Full G26 pinout reference | `firmware/PIN_MAP.md` |
| README.md (gateway) | Building and running the TCP↔MQTT gateway on the Pi | `software/gateway-pi/README.md` |
| README.md (server) | HIS Server .NET 8 architecture | `software/server/README.md` |
| README.md (host_ai) | Training and deploying the AI model | `software/host_ai/README.md` |
| DATASET_CARD.md, DATASET_PLAN.md | Dataset used to train the drip/vitals AI | `software/host_ai/` |
| PI_DEPLOYMENT.md | Deploying the AI runtime on Raspberry Pi | `software/host_ai/deployment/` |

New to the project? Start at the [repository root README](../README.md).
