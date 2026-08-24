#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
GATEWAY="$ROOT_DIR/bin/gateway"
MOSQUITTO="$ROOT_DIR/bin/mosquitto"
NODE="$ROOT_DIR/runtime/node/bin/node"
Z2M_DIR="$ROOT_DIR/zigbee2mqtt"
Z2M_ENTRY="$Z2M_DIR/index.js"
Z2M_CONFIG="$Z2M_DIR/data/configuration.yaml"
MOSQUITTO_CONFIG="$ROOT_DIR/config/mosquitto.conf"
GATEWAY_CONFIG="${GATEWAY_CONFIG:-$ROOT_DIR/config/gateway.conf}"
RUNTIME_CONFIG="$ROOT_DIR/config/runtime.conf"
LOG_DIR="$ROOT_DIR/logs"

MOSQUITTO_PID=""
Z2M_PID=""
GATEWAY_PID=""
ACTIVE_GATEWAY_CONFIG="$GATEWAY_CONFIG"
SERVER_OVERRIDE=""

fail() {
    printf '[ERROR] %s\n' "$*" >&2
    exit 1
}

read_config() {
    local key="$1"
    local file="$2"
    awk -F= -v wanted="$key" '
        $0 !~ /^[[:space:]]*#/ && $1 == wanted {
            sub(/^[^=]*=/, "")
            sub(/\r$/, "")
            print
            exit
        }
    ' "$file"
}

tcp_open() {
    (exec 3<>"/dev/tcp/$1/$2") >/dev/null 2>&1
}

stop_process() {
    local pid="$1"
    local name="$2"
    local attempt

    [[ -n "$pid" ]] || return 0
    kill -0 "$pid" 2>/dev/null || return 0
    printf '[STOP] Dang dung %s...\n' "$name"
    kill "$pid" 2>/dev/null || true
    for attempt in {1..20}; do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep 0.25
    done
    kill -9 "$pid" 2>/dev/null || true
}

stop_stale_processes() {
    local pattern pid stale_pids

    # A terminal can be closed without giving the previous run.sh a chance to
    # execute its EXIT trap. Only stop processes launched from THIS package;
    # never touch another system-wide Mosquitto or an unrelated Node service.
    for pattern in \
        "$ROOT_DIR/zigbee2mqtt/index.js" \
        "$ROOT_DIR/bin/gateway" \
        "$ROOT_DIR/bin/mosquitto -c $ROOT_DIR/config/mosquitto.conf"; do
        stale_pids="$(pgrep -f -- "$pattern" 2>/dev/null || true)"
        for pid in $stale_pids; do
            [[ -n "$pid" && "$pid" != "$$" ]] || continue
            stop_process "$pid" "phien Smart IV cu"
        done
    done
}

detect_coordinator_port() {
    local configured="$1"
    local candidate

    # Prefer the stable USB identity so ttyACM numbering may change after a
    # reconnect without breaking the launcher.
    for candidate in /dev/serial/by-id/* /dev/ttyACM* /dev/ttyUSB*; do
        [[ -e "$candidate" && -r "$candidate" && -w "$candidate" ]] || continue
        printf '%s\n' "$candidate"
        return 0
    done

    # Retain an explicitly configured non-standard serial path as a fallback.
    if [[ -n "$configured" && -e "$configured" && -r "$configured" && -w "$configured" ]]; then
        printf '%s\n' "$configured"
        return 0
    fi
    return 1
}

cleanup() {
    trap - EXIT INT TERM
    stop_process "$GATEWAY_PID" 'gateway'
    stop_process "$Z2M_PID" 'Zigbee2MQTT'
    stop_process "$MOSQUITTO_PID" 'Mosquitto'
}

trap cleanup EXIT
trap 'exit 130' INT TERM

[[ "$(uname -m)" == "aarch64" ]] || fail "Goi nay chi chay tren Linux aarch64. May hien tai: $(uname -m)."
for command_name in awk grep tail mkdir chmod sleep pgrep; do
    command -v "$command_name" >/dev/null 2>&1 || fail "He dieu hanh thieu lenh $command_name."
done

arguments=("$@")
for ((argument_index = 0; argument_index < ${#arguments[@]}; argument_index++)); do
    argument="${arguments[$argument_index]}"
    case "$argument" in
        --config)
            ((argument_index++))
            ((argument_index < ${#arguments[@]})) || fail "--config can mot duong dan."
            ACTIVE_GATEWAY_CONFIG="${arguments[$argument_index]}"
            [[ "$ACTIVE_GATEWAY_CONFIG" == /* ]] || ACTIVE_GATEWAY_CONFIG="$ROOT_DIR/$ACTIVE_GATEWAY_CONFIG"
            ;;
        --server)
            ((argument_index++))
            ((argument_index < ${#arguments[@]})) || fail "--server can mot dia chi IP hoac hostname."
            [[ -z "$SERVER_OVERRIDE" ]] || fail "Chi duoc nhap mot dia chi server."
            SERVER_OVERRIDE="${arguments[$argument_index]}"
            ;;
        -h|--help)
            printf 'Cach dung: bash run.sh [IP-server]\n'
            printf '           bash run.sh --server <IP-server> [--config <file>]\n'
            exit 0
            ;;
        -*)
            fail "Tuy chon khong hop le: $argument"
            ;;
        *)
            [[ -z "$SERVER_OVERRIDE" ]] || fail "Chi duoc nhap mot dia chi server."
            SERVER_OVERRIDE="$argument"
            ;;
    esac
done

for required in "$GATEWAY" "$MOSQUITTO" "$NODE" "$Z2M_ENTRY" "$Z2M_CONFIG" "$MOSQUITTO_CONFIG" "$ACTIVE_GATEWAY_CONFIG" "$RUNTIME_CONFIG"; do
    [[ -f "$required" ]] || fail "Thieu file runtime: $required"
done
[[ -d "$Z2M_DIR/node_modules" ]] || fail "Thieu node_modules cua Zigbee2MQTT."

chmod u+x "$GATEWAY" "$MOSQUITTO" "$NODE" 2>/dev/null || fail "Khong the cap quyen chay cho binary."
NODE_VERSION="$($NODE --version 2>&1)" || fail "Node ARM64 khong chay duoc: $NODE_VERSION"

CONFIGURED_COORDINATOR_PORT="$(read_config COORDINATOR_PORT "$RUNTIME_CONFIG")"
Z2M_TIMEOUT="$(read_config Z2M_START_TIMEOUT "$RUNTIME_CONFIG")"
SERVER_HOST="${SERVER_OVERRIDE:-$(read_config HIS_SERVER_HOST "$ACTIVE_GATEWAY_CONFIG")}"
OTA_INDEX_PORT="$(read_config OTA_INDEX_PORT "$RUNTIME_CONFIG")"
OTA_INDEX_URL="$(read_config OTA_INDEX_URL "$RUNTIME_CONFIG")"
COORDINATOR_PORT="$(detect_coordinator_port "$CONFIGURED_COORDINATOR_PORT")" || \
    fail "Khong tu dong tim thay coordinator trong /dev/serial/by-id, /dev/ttyACM* hoac /dev/ttyUSB*."
MQTT_PORT=1885
Z2M_TIMEOUT="${Z2M_TIMEOUT:-90}"
SERVER_HOST="${SERVER_HOST:-127.0.0.1}"
OTA_INDEX_PORT="${OTA_INDEX_PORT:-5194}"
OTA_INDEX_URL="${OTA_INDEX_URL:-http://$SERVER_HOST:$OTA_INDEX_PORT/api/ota/index.json}"

printf '[AUTO] Da tim thay coordinator: %s\n' "$COORDINATOR_PORT"

# Release a serial lock left by an earlier Smart IV launcher before starting
# the new Zigbee2MQTT instance.
stop_stale_processes

mkdir -p "$LOG_DIR" "$Z2M_DIR/data/log" || fail "Khong tao duoc thu muc log."
cd "$ROOT_DIR" || fail "Khong mo duoc thu muc gateway."

if tcp_open 127.0.0.1 "$MQTT_PORT"; then
    printf '[1/3] MQTT port %s da co broker dang chay; su dung broker hien tai.\n' "$MQTT_PORT"
else
    printf '[1/3] Dang khoi dong Mosquitto noi bo port %s...\n' "$MQTT_PORT"
    "$MOSQUITTO" -c "$MOSQUITTO_CONFIG" >"$LOG_DIR/mosquitto.log" 2>&1 &
    MOSQUITTO_PID=$!
    for _ in {1..40}; do
        kill -0 "$MOSQUITTO_PID" 2>/dev/null || {
            tail -n 30 "$LOG_DIR/mosquitto.log" >&2
            fail "Mosquitto dung khi khoi dong."
        }
        tcp_open 127.0.0.1 "$MQTT_PORT" && break
        sleep 0.25
    done
    tcp_open 127.0.0.1 "$MQTT_PORT" || fail "Mosquitto khong mo port $MQTT_PORT."
fi

printf '[2/3] Dang khoi dong Zigbee2MQTT voi coordinator %s...\n' "$COORDINATOR_PORT"
: >"$LOG_DIR/zigbee2mqtt.log"
(
    cd "$Z2M_DIR" || exit 1
    export NODE_ENV=production
    export ZIGBEE2MQTT_DATA="$Z2M_DIR/data"
    export ZIGBEE2MQTT_CONFIG_SERIAL_PORT="$COORDINATOR_PORT"
    export ZIGBEE2MQTT_CONFIG_MQTT_SERVER="mqtt://127.0.0.1:$MQTT_PORT"
    export ZIGBEE2MQTT_CONFIG_OTA_ZIGBEE_OTA_OVERRIDE_INDEX_LOCATION="$OTA_INDEX_URL"
    exec "$NODE" "$Z2M_ENTRY"
) >"$LOG_DIR/zigbee2mqtt.log" 2>&1 &
Z2M_PID=$!

for ((attempt = 0; attempt < Z2M_TIMEOUT; attempt++)); do
    kill -0 "$Z2M_PID" 2>/dev/null || {
        tail -n 40 "$LOG_DIR/zigbee2mqtt.log" >&2
        fail "Zigbee2MQTT dung khi khoi dong."
    }
    grep -q 'Zigbee2MQTT started!' "$LOG_DIR/zigbee2mqtt.log" && break
    sleep 1
done
grep -q 'Zigbee2MQTT started!' "$LOG_DIR/zigbee2mqtt.log" || {
    tail -n 40 "$LOG_DIR/zigbee2mqtt.log" >&2
    fail "Zigbee2MQTT chua san sang sau ${Z2M_TIMEOUT}s."
}

printf '[3/3] Dang khoi dong gateway Zigbee -> MQTT -> HIS server...\n'
printf '      Node: %s | MQTT: 127.0.0.1:%s\n' "$NODE_VERSION" "$MQTT_PORT"
printf '      HIS server: %s | OTA index: %s\n' "$SERVER_HOST" "$OTA_INDEX_URL"
printf '      Zigbee2MQTT web: http://<IP-cua-Pi>:8080\n'
printf '      Nhan Ctrl+C de dung toan bo.\n'
export GATEWAY_CONFIG="$ACTIVE_GATEWAY_CONFIG"
export MQTT_HOST=127.0.0.1
export MQTT_PORT
"$GATEWAY" "$@" &
GATEWAY_PID=$!

while true; do
    if ! kill -0 "$GATEWAY_PID" 2>/dev/null; then
        wait "$GATEWAY_PID"
        exit $?
    fi
    if ! kill -0 "$Z2M_PID" 2>/dev/null; then
        tail -n 40 "$LOG_DIR/zigbee2mqtt.log" >&2
        fail "Zigbee2MQTT da dung ngoai y muon."
    fi
    sleep 1
done
