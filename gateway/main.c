/*
 * main.c
 *
 * C program running on Windows / Raspberry Pi / Linux
 *
 * Responsibilities:
 * - Automatically launch start_services.bat on Windows to run:
 *   + Mosquitto MQTT broker
 *   + Zigbee2MQTT
 * - Connect to the MQTT broker
 * - Subscribe to the Zigbee2MQTT topic of the SmartIV-Sensor device (empty_2)
 * - Read the JSON fields exposed by zigbee2mqtt_smart_iv_converter.js:
 *   heart_rate, spo2, flow, drop_rate, alarm, signal_lost, spo2_low,
 *   heart_rate_abnormal, line_blocked, ae_alarm, hr_signal, spo2_signal,
 *   flow_signal, drops_signal, weight_g, drops_per_min, target_flow_ml_h,
 *   tare_in_progress, tare_just_completed, hr_baseline_just_completed
 * - Also accepts COMMANDS from the HIS Server over the same TCP socket used
 *   to forward vitals (newline-delimited JSON, e.g.
 *   {"cmd":"set_target_flow_ml_h","value":120}) and republishes them to
 *   zigbee2mqtt's "<topic>/set" so the doctor's change reaches the device.
 *
 * Example received payload:
 * {"heart_rate":81,"spo2":97,"flow":100,"drop_rate":100,"alarm":false,
 *  "signal_lost":false,"spo2_low":false,"heart_rate_abnormal":false,
 *  "line_blocked":false,"ae_alarm":false,"hr_signal":false,"spo2_signal":false,
 *  "flow_signal":false,"drops_signal":true,"weight_g":480,"drops_per_min":19,
 *  "target_flow_ml_h":100,"linkquality":196}
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <mosquitto.h>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET his_socket_t;
#define HIS_INVALID_SOCKET INVALID_SOCKET
#define his_close(s) closesocket(s)
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
typedef int his_socket_t;
#define HIS_INVALID_SOCKET (-1)
#define his_close(s) close(s)
#endif

#define DEFAULT_MQTT_HOST "localhost"
#define DEFAULT_MQTT_PORT 1885

/* Update this to the actual device_id/friendly_name after pairing in
 * zigbee2mqtt (check the frontend or configuration.yaml). */
#define DEFAULT_DEVICE_ID "0x64028ffffe641802"
#define DEFAULT_TOPIC "zigbee2mqtt/0x64028ffffe641802"

/* zigbee2mqtt publishes device_joined / device_interview / device_leave here. */
#define BRIDGE_EVENT_TOPIC "zigbee2mqtt/bridge/event"

/* The full inventory, RETAINED by zigbee2mqtt and republished whenever it
 * changes. Subscribing means the gateway re-announces every known device as
 * soon as it connects - so a device removed from the server by mistake comes
 * back on its own, instead of needing someone to make the hardware rejoin the
 * Zigbee network just to be seen again. */
#define BRIDGE_DEVICES_TOPIC "zigbee2mqtt/bridge/devices"

/* OTA. zigbee2mqtt lo toan bo viec truyen anh firmware; gateway chi lam hai
 * viec: chuyen lenh cua ky thuat vien xuong, va chuyen tien do nguoc len.
 *
 * Tien do di theo HAI duong khac nhau, va can ca hai:
 *   - bridge/response/... : ket qua cua mot lenh (co ban moi khong, xong chua,
 *                           loi gi). Den mot lan cho moi lenh.
 *   - trang thai thiet bi : truong "update" trong ban tin cua chinh thiet bi,
 *                           mang phan tram va con lai bao nhieu. Cap nhat lien
 *                           tuc trong luc nap.
 * Chi nghe duong dau thi thanh tien do dung im suot vai phut nap firmware. */
#define BRIDGE_OTA_CHECK_REQ  "zigbee2mqtt/bridge/request/device/ota_update/check"
#define BRIDGE_OTA_UPDATE_REQ "zigbee2mqtt/bridge/request/device/ota_update/update"
#define BRIDGE_OTA_RESP_WILD  "zigbee2mqtt/bridge/response/device/ota_update/#"

/* Khai bao truoc: xu ly lenh cua HIS Server nam truoc dinh nghia cua ba ham
 * nay trong file, va tat ca deu dung chung mot bo helper doc JSON. */
int  get_string_from_json(const char *json, const char *key, char *out, size_t out_size);
static void publish_ota_request(const char *topic, const char *device_id);
static void his_send_ota_status(const char *device_id, const char *state,
                                int progress, int remaining_s, const char *message,
                                int installed_version, int latest_version);

#define BUFFER_SIZE 256

#define AUTO_START_SERVICES 1
#define START_SERVICES_BAT "start_services.bat"

/* TCP connection to the HIS Server (Server_FPT_upload/Server,
 * BedDataReceiver.cs, default port 5000). If his_host == NULL, do not
 * forward, just print to the screen as before. */
static const char *his_host = NULL;
static int his_port = 5000;
static const char *his_bed_id = "BED-101";
static const char *his_room = "ICU-1";
static his_socket_t his_socket = HIS_INVALID_SOCKET;

static int running = 1;

/* Needed by check_his_commands() to republish a doctor's command to MQTT -
 * set once in main() after the MQTT client and "<topic>/set" string are built. */
static struct mosquitto *g_mosq = NULL;
static char g_mqtt_set_topic[300];

/* Partial-line buffer for commands arriving from the HIS Server on
 * his_socket - a single recv() is not guaranteed to land on a line
 * boundary, so incomplete data is held here until a '\n' completes it. */
#define HIS_CMD_BUF_SIZE 512
static char     his_cmd_buf[HIS_CMD_BUF_SIZE];
static size_t   his_cmd_buf_len = 0;

/*
 * Automatically runs start_services.bat before connecting to MQTT.
 *
 * Notes:
 * - start_services.bat must be in the same directory as main.exe
 * - start_services.bat should only run Mosquitto and Zigbee2MQTT
 * - start_services.bat should NOT re-run main.exe, to avoid an infinite loop
 */
void start_services_if_needed(void)
{
#if defined(_WIN32)
    if (AUTO_START_SERVICES == 0) {
        return;
    }

    DWORD attr = GetFileAttributesA(START_SERVICES_BAT);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        printf("%s not found, skipping automatic service startup\n", START_SERVICES_BAT);
        return;
    }

    printf("Launching %s to start Mosquitto and Zigbee2MQTT...\n", START_SERVICES_BAT);

    int ret = system("cmd /c start_services.bat");

    printf("%s launched, return code: %d\n", START_SERVICES_BAT, ret);
    printf("Waiting for services to start...\n");

    /*
     * Wait for start_services.bat to open the service windows.
     * Zigbee2MQTT usually needs a few seconds to connect to the coordinator and MQTT.
     */
    Sleep(3000);
#else
    printf("Not running on Windows, skipping the .bat file\n");
#endif
}

/*
 * Catch Ctrl+C to exit the program cleanly.
 */
void handle_signal(int signal_number)
{
    (void)signal_number;
    running = 0;
}

/*
 * Extracts a numeric value from a simple JSON payload.
 *
 * Example payload:
 * {"brightness_1":72,"brightness_2":65,"linkquality":216,"state_1":"ON"}
 *
 * Call:
 * get_int_from_json(payload, "brightness_1", &value)
 *
 * Result:
 * value = 72
 */
int get_int_from_json(const char *json, const char *key, int *value)
{
    char pattern[128];
    char *pos;
    char *colon;
    char *number_start;

    if (json == NULL || key == NULL || value == NULL) {
        return 0;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    pos = strstr(json, pattern);
    if (pos == NULL) {
        return 0;
    }

    colon = strchr(pos, ':');
    if (colon == NULL) {
        return 0;
    }

    number_start = colon + 1;

    while (*number_start != '\0' && isspace((unsigned char)*number_start)) {
        number_start++;
    }

    if (*number_start == '-') {
        return 0;
    }

    if (!isdigit((unsigned char)*number_start)) {
        return 0;
    }

    *value = atoi(number_start);
    return 1;
}

/*
 * Same, but accepts a NEGATIVE number too.
 *
 * get_int_from_json() above deliberately bails out on a leading '-', which is
 * fine for everything it reads (a weight, a rate, a count - none can be
 * negative). The forecaster's slopes can: "heart rate falling" IS a negative
 * bpm/min, and it is the direction that matters clinically. Read through the
 * unsigned helper, -12 bpm/min would silently arrive as 0, i.e. "steady".
 */
int get_signed_int_from_json(const char *json, const char *key, int *value)
{
    char pattern[128];
    char *pos;
    char *colon;
    char *number_start;

    if (json == NULL || key == NULL || value == NULL) {
        return 0;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    pos = strstr(json, pattern);
    if (pos == NULL) {
        return 0;
    }

    colon = strchr(pos, ':');
    if (colon == NULL) {
        return 0;
    }

    number_start = colon + 1;
    while (*number_start != '\0' && isspace((unsigned char)*number_start)) {
        number_start++;
    }

    if (*number_start != '-' && !isdigit((unsigned char)*number_start)) {
        return 0;   /* null, or something that is not a number at all */
    }
    if (*number_start == '-' && !isdigit((unsigned char)number_start[1])) {
        return 0;
    }

    *value = atoi(number_start);
    return 1;
}

/*
 * Extracts a bool value (true/false) from a simple JSON payload.
 *
 * Example: get_bool_from_json(payload, "alarm", &value)
 */
int get_bool_from_json(const char *json, const char *key, int *value)
{
    char pattern[128];
    char *pos;
    char *colon;
    char *val_start;

    if (json == NULL || key == NULL || value == NULL) {
        return 0;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    pos = strstr(json, pattern);
    if (pos == NULL) {
        return 0;
    }

    colon = strchr(pos, ':');
    if (colon == NULL) {
        return 0;
    }

    val_start = colon + 1;
    while (*val_start != '\0' && isspace((unsigned char)*val_start)) {
        val_start++;
    }

    if (strncmp(val_start, "true", 4) == 0) {
        *value = 1;
        return 1;
    }
    if (strncmp(val_start, "false", 5) == 0) {
        *value = 0;
        return 1;
    }

    return 0;
}

/*
 * Opens a TCP connection to the HIS Server (BedDataReceiver, port 5000).
 * Returns HIS_INVALID_SOCKET if the connection fails.
 */
static his_socket_t his_connect(const char *host, int port)
{
    struct addrinfo hints, *res, *rp;
    char port_str[16];
    his_socket_t sock = HIS_INVALID_SOCKET;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        return HIS_INVALID_SOCKET;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == HIS_INVALID_SOCKET) {
            continue;
        }
        if (connect(sock, rp->ai_addr, (int)rp->ai_addrlen) == 0) {
            break;
        }
        his_close(sock);
        sock = HIS_INVALID_SOCKET;
    }

    freeaddrinfo(res);
    return sock;
}

/*
 * Sends one line of JSON (English field names only, no more Vietnamese
 * fields) to the HIS Server (BedTcpIngestionService.cs / BedDataParser.cs),
 * terminated with '\n'. The server itself computes status/alert content
 * from the raw numeric values - the gateway only sends raw data + each
 * channel's signal status, it no longer infers status on its own.
 */
/* Renders a forecast as a JSON number, or as the literal null when the model
 * could not vouch for it. */
static void forecast_text(char *out, size_t size, int have, int value)
{
    if (have) {
        snprintf(out, size, "%d", value);
    } else {
        snprintf(out, size, "null");
    }
}

/* Output of the on-chip time-series forecaster, forwarded verbatim.
 *
 * Grouped in a struct rather than added to his_send_bed_data()'s argument
 * list, which is already twenty-one values long.
 *
 * The three "have_*" flags exist because a missing forecast and a forecast of
 * zero are completely different statements. When a channel loses signal the
 * model still produces a number for it - computed from the baseline filler,
 * not from the patient - so the firmware marks it invalid and the server must
 * receive null, not a figure the dashboard would label "forecast". */
typedef struct {
    int ready;
    int anomaly;
    int early_warning;
    int hr_trend;                  /* 0 steady, 1 rising, 2 falling */
    int drops_trend;
    int have_hr_forecast;    int hr_forecast_16s;
    int have_spo2_forecast;  int spo2_forecast_16s;
    int have_drops_forecast; int drops_forecast_16s;
    int hr_trend_bpm_per_min;      /* signed: negative = falling */
    int drops_trend_dpm_per_min;   /* signed: negative = slowing down */
    int anomaly_score_x100;
    int hr_forecast_trusted;
    int drops_forecast_trusted;

    /* --- AI v2: which side is at fault, not just "something is wrong" ------
     *
     * The device now runs three independent models and decides a 4-level alert
     * from two attributable branches. A single boolean cannot carry that, and
     * the distinction is the one a nurse acts on first: a blocked line and a
     * deteriorating patient need completely different responses.
     *
     * The gateway still does NOT interpret any of this - it forwards. The bed's
     * status is decided in exactly one place, VitalsStatusEvaluator.cs on the
     * server, and that stays true. */
    int alert_level;       /* 0 normal, 1 line warning, 2 vitals, 3 critical */
    int line_branch;
    int patient_branch;
    int drip_anomaly;      /* Model 1 confirmed through persistence */
    int vitals_anomaly;    /* Model 2 confirmed through persistence */
    int line_state;        /* line_state_t from the device; -1 = not yet valid */
    int remaining_ml;      /* -1 when the load cell cannot estimate it */
    int remaining_min;
} ts_forecast_t;

static void his_send_bed_data(int heart_rate, int spo2, int flow_rate, int drop_rate,
                               int hr_signal, int spo2_signal, int flow_signal,
                               int drops_signal, int line_blocked, int ae_alarm,
                               int weight_g, int drops_per_min, int target_flow_ml_h,
                               int target_drops_per_min, int tare_in_progress,
                               int tare_just_completed, int hr_baseline_just_completed,
                               int hr_baseline_seconds_remaining, int hr_baseline_bpm,
                               int tare_event_count, int hr_baseline_event_count,
                               int link_quality, const char *device_id,
                               int monitoring,
                               const ts_forecast_t *ts)
{
    char line[1400];
    char ts_line[420];
    char hr_forecast_text[12];
    char spo2_forecast_text[12];
    char drops_forecast_text[12];
    size_t len;

    if (his_host == NULL) {
        return;
    }

    if (his_socket == HIS_INVALID_SOCKET) {
        his_socket = his_connect(his_host, his_port);
        if (his_socket == HIS_INVALID_SOCKET) {
            printf("Could not connect to HIS Server %s:%d\n", his_host, his_port);
            return;
        }
        printf("Connected to HIS Server %s:%d (TCP)\n", his_host, his_port);
    }

    forecast_text(hr_forecast_text, sizeof(hr_forecast_text),
                  ts->have_hr_forecast, ts->hr_forecast_16s);
    forecast_text(spo2_forecast_text, sizeof(spo2_forecast_text),
                  ts->have_spo2_forecast, ts->spo2_forecast_16s);
    forecast_text(drops_forecast_text, sizeof(drops_forecast_text),
                  ts->have_drops_forecast, ts->drops_forecast_16s);

    /* Field names match what BedDataParser already accepts (it has read these
     * since the serial fallback gateway started sending them), so nothing on
     * the server needed changing to light the forecast card up. */
    snprintf(ts_line, sizeof(ts_line),
             ",\"tsReady\":%s,\"tsAnomaly\":%s,\"tsEarlyWarning\":%s,"
             "\"tsTrend\":%d,\"dropsTrend\":%d,"
             "\"hrTrendBpmPerMin\":%d,\"dropsTrendDpmPerMin\":%d,"
             "\"tsAnomalyScore\":%d,"
             "\"hrForecastTrusted\":%s,\"dropsForecastTrusted\":%s,"
             "\"alertLevel\":%d,\"lineBranch\":%s,\"patientBranch\":%s,"
             "\"dripAnomaly\":%s,\"vitalsAnomaly\":%s,"
             "\"lineState\":%d,\"remainingMl\":%d,\"remainingMin\":%d",
             ts->ready ? "true" : "false",
             ts->anomaly ? "true" : "false",
             ts->early_warning ? "true" : "false",
             ts->hr_trend, ts->drops_trend,
             ts->hr_trend_bpm_per_min, ts->drops_trend_dpm_per_min,
             ts->anomaly_score_x100,
             ts->hr_forecast_trusted ? "true" : "false",
             ts->drops_forecast_trusted ? "true" : "false",
             ts->alert_level,
             ts->line_branch ? "true" : "false",
             ts->patient_branch ? "true" : "false",
             ts->drip_anomaly ? "true" : "false",
             ts->vitals_anomaly ? "true" : "false",
             ts->line_state, ts->remaining_ml, ts->remaining_min);

    snprintf(line, sizeof(line),
             "{\"bedId\":\"%s\",\"room\":\"%s\",\"spo2\":%d,\"heartRate\":%d,"
             "\"dripRate\":%d,\"flowRate\":%d,\"heartRateSignal\":%s,"
             "\"spo2Signal\":%s,\"flowSignal\":%s,\"dripRateSignal\":%s,"
             "\"lineBlocked\":%s,\"aeAlarm\":%s,\"weightG\":%d,"
             "\"dropsPerMin\":%d,\"targetFlowMlH\":%d,\"targetDropsPerMin\":%d,"
             "\"tareInProgress\":%s,\"tareJustCompleted\":%s,"
             "\"hrBaselineJustCompleted\":%s,\"hrBaselineSecondsRemaining\":%d,"
             "\"hrBaselineBpm\":%d,\"tareEventCount\":%d,\"hrBaselineEventCount\":%d,"
             "\"linkQuality\":%d,\"deviceId\":\"%s\",\"monitoring\":%s"
             "%s"
             ",\"hrForecast16s\":%s,\"spo2Forecast16s\":%s,\"dropsForecast16s\":%s}\n",
             his_bed_id, his_room, spo2, heart_rate, drop_rate, flow_rate,
             hr_signal ? "true" : "false", spo2_signal ? "true" : "false",
             flow_signal ? "true" : "false", drops_signal ? "true" : "false",
             line_blocked ? "true" : "false", ae_alarm ? "true" : "false",
             weight_g, drops_per_min, target_flow_ml_h, target_drops_per_min,
             tare_in_progress ? "true" : "false",
             tare_just_completed ? "true" : "false",
             hr_baseline_just_completed ? "true" : "false",
             hr_baseline_seconds_remaining, hr_baseline_bpm,
             tare_event_count, hr_baseline_event_count,
             link_quality,
             device_id != NULL ? device_id : "",
             monitoring ? "true" : "false",
             ts_line,
             hr_forecast_text, spo2_forecast_text, drops_forecast_text);

    len = strlen(line);
    if (send(his_socket, line, (int)len, 0) < 0) {
        printf("Lost connection to HIS Server, will retry on the next send\n");
        his_close(his_socket);
        his_socket = HIS_INVALID_SOCKET;
    }
}

/*
 * Publishes a single-field JSON payload to zigbee2mqtt's "<topic>/set",
 * e.g. publish_mqtt_set("target_flow_ml_h", "120") publishes
 * {"target_flow_ml_h":120}. field_value_json is inserted as-is (already
 * valid JSON - a bare number, or "true"), not quoted, so callers pass
 * numbers/booleans, not strings.
 */
static void publish_mqtt_set(const char *description, const char *field_name, const char *field_value_json)
{
    if (g_mosq == NULL) {
        return;
    }

    char payload[64];
    snprintf(payload, sizeof(payload), "{\"%s\":%s}", field_name, field_value_json);
    int pub_rc = mosquitto_publish(g_mosq, NULL, g_mqtt_set_topic,
                                   (int)strlen(payload), payload, 0, false);
    if (pub_rc == MOSQ_ERR_SUCCESS) {
        printf("HIS Server requested %s -> published to %s\n", description, g_mqtt_set_topic);
    } else {
        printf("Failed to publish %s command: %s\n", description, mosquitto_strerror(pub_rc));
    }
}

/*
 * Checks for an incoming command line from the HIS Server on the SAME TCP
 * socket used to forward vitals (the gateway is the TCP client, so the
 * socket only exists once at least one vitals send has succeeded). Uses a
 * zero-timeout select() so this never blocks the main loop - if nothing is
 * waiting, it returns immediately.
 *
 * Expected command format (newline-delimited JSON):
 *   {"cmd":"set_target_flow_ml_h","value":120}
 *   {"cmd":"set_target_drops_per_min","value":20}
 *   {"cmd":"reset_tare"}
 *   {"cmd":"set_monitoring","value":1}   (1 = bat theo doi, 0 = che do cho)
 *   {"cmd":"recalibrate_hr_baseline"}
 *
 * On a recognized command, republishes to zigbee2mqtt's "<topic>/set" so
 * the toZigbee converters in zigbee2mqtt_smart_iv_converter.js write the
 * matching Zigbee attribute down to the device.
 */
static void check_his_commands(void)
{
    if (his_socket == HIS_INVALID_SOCKET) {
        return;
    }

    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(his_socket, &readfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    int ready = select((int)his_socket + 1, &readfds, NULL, NULL, &tv);
    if (ready <= 0 || !FD_ISSET(his_socket, &readfds)) {
        return;
    }

    char chunk[256];
    int n = recv(his_socket, chunk, sizeof(chunk) - 1, 0);
    if (n <= 0) {
        /* HIS Server closed the connection or a real error occurred - the
         * next his_send_bed_data() call will transparently reconnect. */
        printf("HIS Server command channel closed, will reconnect on next send\n");
        his_close(his_socket);
        his_socket = HIS_INVALID_SOCKET;
        his_cmd_buf_len = 0;
        return;
    }
    chunk[n] = '\0';

    for (int i = 0; i < n && his_cmd_buf_len < HIS_CMD_BUF_SIZE - 1; i++) {
        if (chunk[i] == '\n') {
            his_cmd_buf[his_cmd_buf_len] = '\0';

            int value = 0;
            char value_str[16];

            if (strstr(his_cmd_buf, "\"cmd\"") == NULL) {
                /* not a recognized command line - ignore */
            } else if (strstr(his_cmd_buf, "set_target_flow_ml_h") != NULL
                       && get_int_from_json(his_cmd_buf, "value", &value)) {
                snprintf(value_str, sizeof(value_str), "%d", value);
                publish_mqtt_set("target flow rate", "target_flow_ml_h", value_str);
            } else if (strstr(his_cmd_buf, "set_target_drops_per_min") != NULL
                       && get_int_from_json(his_cmd_buf, "value", &value)) {
                snprintf(value_str, sizeof(value_str), "%d", value);
                publish_mqtt_set("target drop rate", "target_drops_per_min", value_str);
            } else if (strstr(his_cmd_buf, "set_monitoring") != NULL
                       && get_int_from_json(his_cmd_buf, "value", &value)) {
                /* Trang thai, khong phai lenh ban-roi-quen: thiet bi giu nguyen
                 * gia tri va bao nguoc len, nen nut tren web luon phan anh dung
                 * thu chip dang lam. */
                publish_mqtt_set("monitoring", "monitoring",
                                 value ? "true" : "false");
            } else if (strstr(his_cmd_buf, "reset_tare") != NULL) {
                publish_mqtt_set("loadcell tare reset", "reset_tare", "true");
            } else if (strstr(his_cmd_buf, "recalibrate_hr_baseline") != NULL) {
                publish_mqtt_set("HR baseline recalibration", "recalibrate_hr_baseline", "true");
            } else if (strstr(his_cmd_buf, "ota_check") != NULL) {
                char dev[64];
                if (get_string_from_json(his_cmd_buf, "deviceId", dev, sizeof(dev))) {
                    publish_ota_request(BRIDGE_OTA_CHECK_REQ, dev);
                }
            } else if (strstr(his_cmd_buf, "ota_update") != NULL) {
                char dev[64];
                if (get_string_from_json(his_cmd_buf, "deviceId", dev, sizeof(dev))) {
                    /* Bao ngay la da nhan lenh. Neu doi z2m tra loi thi nut bam
                     * ben ky thuat vien im lang vai giay, va nguoi ta se bam
                     * lai - dieu cuoi cung ta muon voi mot lenh nap firmware. */
                    his_send_ota_status(dev, "starting", -1, -1, "Update requested", -1, -1);
                    publish_ota_request(BRIDGE_OTA_UPDATE_REQ, dev);
                }
            } else if (strstr(his_cmd_buf, "rescan_devices") != NULL) {
                /* Re-subscribing makes the broker redeliver the RETAINED
                 * inventory, which is then announced device by device.
                 *
                 * Needed because zigbee2mqtt republishes that topic only when
                 * the Zigbee network changes. Removing a device record on the
                 * server changes nothing on the radio, so without this the
                 * device would not be seen again until the gateway happened to
                 * restart - which is exactly how a deleted device came to look
                 * permanently lost. */
                printf("Rescan requested by HIS Server\n");
                if (g_mosq != NULL) {
                    mosquitto_unsubscribe(g_mosq, NULL, BRIDGE_DEVICES_TOPIC);
                    if (mosquitto_subscribe(g_mosq, NULL, BRIDGE_DEVICES_TOPIC, 0) != MOSQ_ERR_SUCCESS) {
                        printf("Rescan failed: could not re-subscribe to %s\n",
                               BRIDGE_DEVICES_TOPIC);
                    }
                }
            }

            his_cmd_buf_len = 0;
        } else {
            his_cmd_buf[his_cmd_buf_len++] = chunk[i];
        }
    }
}

/*
 * Callback fired when connected to the MQTT broker.
 */
void on_connect(struct mosquitto *mosq, void *userdata, int rc)
{
    const char *topic = (const char *)userdata;

    if (rc == 0) {
        printf("Connected to MQTT broker\n");
        printf("Listening on topic: %s\n", topic);

        int sub_rc = mosquitto_subscribe(mosq, NULL, topic, 0);
        if (sub_rc != MOSQ_ERR_SUCCESS) {
            printf("Error subscribing to topic: %s\n", mosquitto_strerror(sub_rc));
        }

        /* zigbee2mqtt announces every join/interview on this topic. Without it
         * a device that joined the network was invisible to the server: the
         * technician had to read the IEEE address out of the z2m log over SSH
         * and type it into the Devices tab, where a single wrong character
         * produced a device row that matched nothing and looked, from the UI,
         * exactly like a device that was simply offline. */
        sub_rc = mosquitto_subscribe(mosq, NULL, BRIDGE_EVENT_TOPIC, 0);
        if (sub_rc != MOSQ_ERR_SUCCESS) {
            printf("Error subscribing to %s: %s\n",
                   BRIDGE_EVENT_TOPIC, mosquitto_strerror(sub_rc));
        } else {
            printf("Listening for new devices on: %s\n", BRIDGE_EVENT_TOPIC);
        }

        sub_rc = mosquitto_subscribe(mosq, NULL, BRIDGE_OTA_RESP_WILD, 0);
        if (sub_rc != MOSQ_ERR_SUCCESS) {
            printf("Could not subscribe to %s - OTA results will not be reported\n",
                   BRIDGE_OTA_RESP_WILD);
        }

        sub_rc = mosquitto_subscribe(mosq, NULL, BRIDGE_DEVICES_TOPIC, 0);
        if (sub_rc != MOSQ_ERR_SUCCESS) {
            printf("Error subscribing to %s: %s\n",
                   BRIDGE_DEVICES_TOPIC, mosquitto_strerror(sub_rc));
        } else {
            printf("Reading the device inventory from: %s\n", BRIDGE_DEVICES_TOPIC);
        }
    } else {
        printf("Error connecting to MQTT broker, error code: %d\n", rc);
    }
}

/*
 * Callback fired when the MQTT connection is lost.
 */
void on_disconnect(struct mosquitto *mosq, void *userdata, int rc)
{
    (void)mosq;
    (void)userdata;

    printf("MQTT connection lost, rc = %d\n", rc);

    if (rc != 0) {
        printf("Waiting to automatically reconnect...\n");
    }
}

/*
 * Extracts a string value from a flat JSON payload:
 *   get_string_from_json(payload, "ieee_address", buf, sizeof buf)
 *
 * The existing helpers only read numbers and booleans; a device address is
 * neither. Same minimal-parser approach as the rest of this file - the
 * payloads are machine-generated and flat, so a full JSON library would be
 * a dependency bought for nothing.
 */
int get_string_from_json(const char *json, const char *key, char *out, size_t out_size)
{
    char pattern[128];
    char *pos;
    char *colon;
    char *start;
    char *end;
    size_t length;

    if (json == NULL || key == NULL || out == NULL || out_size == 0) {
        return 0;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(json, pattern);
    if (pos == NULL) {
        return 0;
    }

    colon = strchr(pos, ':');
    if (colon == NULL) {
        return 0;
    }

    start = colon + 1;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    if (*start != '"') {
        return 0;               /* null, a number, an object - not a string */
    }
    start++;

    end = strchr(start, '"');
    if (end == NULL) {
        return 0;
    }

    length = (size_t)(end - start);
    if (length >= out_size) {
        length = out_size - 1;
    }
    memcpy(out, start, length);
    out[length] = '\0';
    return 1;
}

/*
 * Tells the HIS Server that a Zigbee device joined, so it appears in the
 * Devices tab for a technician to assign to a bed.
 *
 * Sent over the SAME TCP connection that carries vitals, as a line with a
 * "type" field. Vitals lines have no such field, so the server can tell them
 * apart, and no second port, protocol or credential is introduced for what is
 * one message per device per join.
 */
/* Chuyen trang thai OTA len HIS Server.
 *
 * Gateway KHONG dien giai gi o day - no khong quyet dinh "dang cap nhat" nghia
 * la gi, khong tinh phan tram, khong doan thanh cong hay that bai. Nguyen tac
 * cua he thong nay la trang thai giuong chi duoc quyet o mot noi tren server;
 * OTA theo dung nguyen tac do. */
static void his_send_ota_status(const char *device_id, const char *state,
                                int progress, int remaining_s, const char *message,
                                int installed_version, int latest_version)
{
    char line[600];
    size_t len;

    if (his_host == NULL || device_id == NULL || device_id[0] == '\0') {
        return;
    }
    if (his_socket == HIS_INVALID_SOCKET) {
        his_socket = his_connect(his_host, his_port);
        if (his_socket == HIS_INVALID_SOCKET) {
            return;
        }
    }

    /* progress va remaining gui -1 khi chua biet, khong gui 0. "Chua bat dau"
     * va "dang o 0%%" la hai chuyen khac nhau, va gop chung se lam thanh tien
     * do nhay ve dau moi lan z2m gui mot ban tin khong kem phan tram. */
    /* Phien ban di kem trang thai. Khong co no thi lich su chi ghi duoc "da
     * cap nhat luc 10:47", con cau hoi thuc su cua ky thuat vien - "tu ban nao
     * len ban nao" - thi khong tra loi duoc. -1 = chua biet. */
    snprintf(line, sizeof(line),
             "{\"type\":\"ota_status\",\"deviceId\":\"%s\",\"state\":\"%s\""
             ",\"progress\":%d,\"remainingSeconds\":%d,\"message\":\"%s\""
             ",\"installedVersion\":%d,\"latestVersion\":%d}\n",
             device_id, state ? state : "unknown", progress, remaining_s,
             message ? message : "", installed_version, latest_version);

    len = strlen(line);
    if (send(his_socket, line, (int)len, 0) < 0) {
        his_close(his_socket);
        his_socket = HIS_INVALID_SOCKET;
    } else {
        printf("OTA %s: %s (%d%%)\n", device_id, state ? state : "unknown", progress);
    }
}

/* Thiet bi cua lenh OTA gan nhat.
 *
 * Can vi khi that bai, zigbee2mqtt tra ve {"data":{},"error":...,"status":...}
 * - KHONG kem "id". Khong nho lai thi gateway khong biet phan hoi do cua thiet
 * bi nao va bo qua, va ben ky thuat vien se thay nut Kiem tra khong phan ung gi
 * ca. Chinh loi nay lam lan thu dau tien im lang.
 *
 * Mot bien la du: server tu choi lenh OTA thu hai khi mot lenh dang chay, nen
 * tai mot thoi diem chi co mot thiet bi dang cap nhat. */
static char g_ota_pending_device[64] = "";

/* Phat mot lenh OTA xuong zigbee2mqtt. */
static void publish_ota_request(const char *topic, const char *device_id)
{
    if (g_mosq == NULL || device_id == NULL || device_id[0] == '\0') {
        return;
    }
    snprintf(g_ota_pending_device, sizeof(g_ota_pending_device), "%s", device_id);
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"id\":\"%s\"}", device_id);
    if (mosquitto_publish(g_mosq, NULL, topic, (int)strlen(payload), payload, 0, false)
        == MOSQ_ERR_SUCCESS) {
        printf("OTA request -> %s for %s\n", topic, device_id);
    } else {
        printf("OTA request FAILED -> %s for %s\n", topic, device_id);
    }
}

static void his_announce_device(const char *device_id, const char *friendly_name)
{
    char line[400];
    size_t len;

    if (his_host == NULL || device_id == NULL || device_id[0] == '\0') {
        return;
    }

    if (his_socket == HIS_INVALID_SOCKET) {
        his_socket = his_connect(his_host, his_port);
        if (his_socket == HIS_INVALID_SOCKET) {
            printf("Could not reach HIS Server to announce device %s\n", device_id);
            return;
        }
        printf("Connected to HIS Server %s:%d (TCP)\n", his_host, his_port);
    }

    snprintf(line, sizeof(line),
             "{\"type\":\"device_announce\",\"deviceId\":\"%s\",\"friendlyName\":\"%s\"}\n",
             device_id, friendly_name != NULL ? friendly_name : "");

    len = strlen(line);
    if (send(his_socket, line, (int)len, 0) < 0) {
        printf("Lost connection to HIS Server while announcing a device\n");
        his_close(his_socket);
        his_socket = HIS_INVALID_SOCKET;
    } else {
        printf("Announced device to HIS Server: %s (%s)\n",
               device_id, friendly_name != NULL ? friendly_name : "unnamed");
    }
}

/*
 * Handles one zigbee2mqtt bridge event. Returns 1 if the message was a bridge
 * event (and must not be parsed as vitals).
 */
static int handle_bridge_event(const char *payload)
{
    char type[64];
    char ieee[64];
    char name[96];

    if (!get_string_from_json(payload, "type", type, sizeof(type))) {
        return 1;               /* a bridge event we do not care about */
    }

    /* device_joined fires the moment it associates; device_interview fires
     * again when z2m knows what it is. Both carry the address, and taking
     * either means a device shows up even if the interview stalls - the
     * failure the error table in the A-Z guide describes. */
    if (strcmp(type, "device_joined") != 0 && strcmp(type, "device_interview") != 0) {
        if (strcmp(type, "device_leave") == 0) {
            printf("Zigbee device left the network\n");
        }
        return 1;
    }

    if (!get_string_from_json(payload, "ieee_address", ieee, sizeof(ieee))) {
        return 1;
    }
    if (!get_string_from_json(payload, "friendly_name", name, sizeof(name))) {
        name[0] = '\0';
    }

    printf("\nZigbee event: %s -> %s (%s)\n", type, ieee, name[0] ? name : "unnamed");
    his_announce_device(ieee, name);
    return 1;
}

#define MAX_INVENTORY_DEVICES 32
#define MAX_IEEE_LEN 32
#define MAX_NAME_LEN 96

/* friendly_name -> IEEE address, learned from zigbee2mqtt's retained
 * inventory.
 *
 * Vitals arrive on zigbee2mqtt/<friendly_name> and carry no address, but the
 * server needs to know WHICH DEVICE sent them - that is what decides the bed
 * the readings belong to. Without this the bed came from the gateway's command
 * line, so re-assigning a device in the console changed nothing about where
 * its data landed. */
static struct {
    char name[MAX_NAME_LEN];
    char ieee[MAX_IEEE_LEN];
} g_device_names[MAX_INVENTORY_DEVICES];
static int g_device_name_count = 0;

static void remember_device_name(const char *name, const char *ieee)
{
    int i;

    if (name == NULL || name[0] == '\0' || ieee == NULL || ieee[0] == '\0') {
        return;
    }

    for (i = 0; i < g_device_name_count; i++) {
        if (strcmp(g_device_names[i].name, name) == 0) {
            snprintf(g_device_names[i].ieee, MAX_IEEE_LEN, "%s", ieee);
            return;
        }
    }

    if (g_device_name_count < MAX_INVENTORY_DEVICES) {
        snprintf(g_device_names[g_device_name_count].name, MAX_NAME_LEN, "%s", name);
        snprintf(g_device_names[g_device_name_count].ieee, MAX_IEEE_LEN, "%s", ieee);
        g_device_name_count++;
    }
}

/* The address of whoever publishes on this topic, or "" if not known yet. */
static const char *device_id_for_topic(const char *topic)
{
    const char *slash;
    int i;

    if (topic == NULL) {
        return "";
    }

    slash = strrchr(topic, '/');
    if (slash == NULL) {
        return "";
    }
    slash++;

    for (i = 0; i < g_device_name_count; i++) {
        if (strcmp(g_device_names[i].name, slash) == 0) {
            return g_device_names[i].ieee;
        }
    }
    return "";
}

/*
 * Reads the address at an "ieee_address" match. Returns the position just
 * after the closing quote, or NULL if the payload is malformed.
 */
static const char *inventory_read_address(const char *match, const char *key,
                                          char *out, size_t out_size)
{
    const char *start = match + strlen(key);
    const char *end = strchr(start, '"');
    size_t len;

    if (end == NULL) {
        return NULL;
    }

    len = (size_t)(end - start);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return end;
}

/*
 * The friendly_name closest BEFORE `limit`, i.e. the one in the same record.
 */
static int inventory_name_before(const char *payload, const char *limit,
                                 char *out, size_t out_size)
{
    const char *key = "\"friendly_name\":\"";
    const char *cursor = payload;
    const char *best = NULL;

    while ((cursor = strstr(cursor, key)) != NULL && cursor < limit) {
        best = cursor;
        cursor += strlen(key);
    }

    if (best == NULL) {
        return 0;
    }
    return inventory_read_address(best, key, out, out_size) != NULL;
}

/*
 * Is the record containing this address the coordinator? zigbee2mqtt orders
 * keys alphabetically, so "type" sits after "ieee_address" within the same
 * object; looking no further than the next address keeps the check inside one
 * record.
 */
static int inventory_record_is_coordinator(const char *after_address, const char *key)
{
    const char *next = strstr(after_address, key);
    size_t window = next != NULL ? (size_t)(next - after_address) : strlen(after_address);
    char *record = (char *)malloc(window + 1);
    int result = 0;

    if (record == NULL) {
        return 0;
    }

    memcpy(record, after_address, window);
    record[window] = '\0';
    result = strstr(record, "\"type\":\"Coordinator\"") != NULL;
    free(record);
    return result;
}

/*
 * Announces every device in zigbee2mqtt's retained inventory.
 *
 * The payload is a JSON ARRAY of full device records, far too large and nested
 * for the flat helpers above, so this scans for each "ieee_address" instead of
 * parsing properly. That is enough: the address IS the device id, and the rest
 * of the record is of no interest here.
 *
 * The coordinator appears in the same list and must be skipped - it is the USB
 * stick in the Pi, not a bedside monitor, and adding it to the equipment
 * inventory would leave the technician with a device that can never be
 * assigned to a bed. zigbee2mqtt orders keys alphabetically, so "type" sits
 * after "ieee_address" inside the same object: looking ahead only as far as
 * the NEXT address keeps the check inside one record.
 */
static void handle_device_inventory(const char *payload)
{
    /* The coordinator's address appears TWICE in this payload: once as its own
     * record, and again inside every other device's bindings, where it is the
     * destination those devices report to. The second occurrence carries no
     * "type" field, so checking only the surrounding record let the USB stick
     * into the equipment inventory as if it were a bedside monitor.
     *
     * So: collect the coordinator addresses first, then announce everything
     * else exactly once. Deduplicating also stops a device with several
     * bindings being announced several times. */
    char skip[MAX_INVENTORY_DEVICES][MAX_IEEE_LEN];
    char seen[MAX_INVENTORY_DEVICES][MAX_IEEE_LEN];
    int skip_count = 0;
    int seen_count = 0;
    int announced = 0;
    const char *key = "\"ieee_address\":\"";
    const char *cursor;

    for (cursor = payload; (cursor = strstr(cursor, key)) != NULL; ) {
        char ieee[MAX_IEEE_LEN];
        const char *end = inventory_read_address(cursor, key, ieee, sizeof(ieee));
        if (end == NULL) {
            break;
        }

        if (inventory_record_is_coordinator(end, key) && skip_count < MAX_INVENTORY_DEVICES) {
            snprintf(skip[skip_count++], MAX_IEEE_LEN, "%s", ieee);
        }
        cursor = end;
    }

    for (cursor = payload; (cursor = strstr(cursor, key)) != NULL; ) {
        char ieee[MAX_IEEE_LEN];
        const char *end = inventory_read_address(cursor, key, ieee, sizeof(ieee));
        int i, skip_this = 0;

        if (end == NULL) {
            break;
        }

        for (i = 0; i < skip_count; i++) {
            if (strcmp(skip[i], ieee) == 0) { skip_this = 1; break; }
        }
        for (i = 0; !skip_this && i < seen_count; i++) {
            if (strcmp(seen[i], ieee) == 0) { skip_this = 1; break; }
        }

        if (!skip_this) {
            char name[MAX_NAME_LEN];

            if (seen_count < MAX_INVENTORY_DEVICES) {
                snprintf(seen[seen_count++], MAX_IEEE_LEN, "%s", ieee);
            }

            /* friendly_name sorts before ieee_address, so the nearest one
             * BEHIND this address belongs to the same record. */
            if (inventory_name_before(payload, cursor, name, sizeof(name))) {
                remember_device_name(name, ieee);
            }

            his_announce_device(ieee, name);
            announced++;
        }

        cursor = end;
    }

    if (announced > 0) {
        printf("Announced %d device(s) from the zigbee2mqtt inventory\n", announced);
    }
}

/*
 * Callback fired when MQTT data is received.
 *
 * Reads the 5 AI values (heart_rate, spo2, flow, drop_rate, alarm bitmap
 * already decoded by zigbee2mqtt_smart_iv_converter.js into clear JSON
 * fields) and prints them to the screen.
 */
void on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg)
{
    (void)mosq;
    (void)userdata;

    char *payload;
    int heart_rate = 0, spo2 = 0, flow = 0, drop_rate = 0;
    int alarm = 0, signal_lost = 0, spo2_low = 0, heart_rate_abnormal = 0, line_blocked = 0, ae_alarm = 0;
    int hr_signal = 0, spo2_signal = 0, flow_signal = 0, drops_signal = 0;
    int weight_g = 0, drops_per_min = 0, target_flow_ml_h = 0, target_drops_per_min = 0;
    int tare_in_progress = 0, tare_just_completed = 0, hr_baseline_just_completed = 0;
    int hr_baseline_seconds_remaining = 0, hr_baseline_bpm = 0;
    int tare_event_count = 0, hr_baseline_event_count = 0;
    /* Zigbee signal strength, 0-255. zigbee2mqtt adds it to every payload and
     * the gateway used to drop it. It is the one number that tells a
     * technician whether a flaky bed needs a repeater or a new sensor. */
    int link_quality = -1;
    ts_forecast_t ts;

    memset(&ts, 0, sizeof(ts));
    /* Both default to "trusted" so an older firmware that never sends these
     * two flags does not make the dashboard relabel every figure as
     * untrustworthy - same default the server's parser uses. */
    ts.hr_forecast_trusted = 1;
    ts.drops_forecast_trusted = 1;

    if (msg == NULL || msg->payload == NULL || msg->payloadlen <= 0) {
        return;
    }

    payload = (char *)malloc((size_t)msg->payloadlen + 1);
    if (payload == NULL) {
        printf("Memory allocation error\n");
        return;
    }

    memcpy(payload, msg->payload, (size_t)msg->payloadlen);
    payload[msg->payloadlen] = '\0';

    if (msg->topic != NULL && strcmp(msg->topic, BRIDGE_EVENT_TOPIC) == 0) {
        handle_bridge_event(payload);
        free(payload);
        return;
    }

    if (msg->topic != NULL && strcmp(msg->topic, BRIDGE_DEVICES_TOPIC) == 0) {
        handle_device_inventory(payload);
        free(payload);
        return;
    }

    /* Ket qua cua mot lenh OTA. Den mot lan cho moi lenh. */
    if (msg->topic != NULL
        && strstr(msg->topic, "bridge/response/device/ota_update/") != NULL) {
        char dev[64] = "";
        char status[32] = "";
        char err[240] = "";
        int available = 0;

        get_string_from_json(payload, "id", dev, sizeof(dev));
        get_string_from_json(payload, "status", status, sizeof(status));
        get_string_from_json(payload, "error", err, sizeof(err));

        /* Phan hoi loi khong kem "id" - dung thiet bi cua lenh vua gui. */
        if (dev[0] == '\0') {
            snprintf(dev, sizeof(dev), "%s", g_ota_pending_device);
        }

        if (dev[0] != '\0') {
            if (strcmp(status, "error") == 0) {
                his_send_ota_status(dev, "failed", -1, -1, err, -1, -1);
            } else if (strstr(msg->topic, "/check") != NULL) {
                get_bool_from_json(payload, "updateAvailable", &available);
                his_send_ota_status(dev, available ? "available" : "upToDate",
                                    -1, -1, "", -1, -1);
            } else if (strstr(msg->topic, "/update") != NULL) {
                his_send_ota_status(dev, "done", 100, 0, "Update finished", -1, -1);
            }
        }
        free(payload);
        return;
    }

    printf("\nMQTT message received\n");
    printf("Topic: %s\n", msg->topic);
    printf("Payload: %s\n", payload);

    if (get_int_from_json(payload, "heart_rate", &heart_rate)) {
        printf("Heart rate (HR) : %d bpm\n", heart_rate);
    }
    if (get_int_from_json(payload, "spo2", &spo2)) {
        printf("SpO2            : %d %%\n", spo2);
    }
    if (get_int_from_json(payload, "flow", &flow)) {
        printf("Flow rate       : %d %%\n", flow);
    }
    if (get_int_from_json(payload, "drop_rate", &drop_rate)) {
        printf("Drop rate       : %d %%\n", drop_rate);
    }

    if (get_bool_from_json(payload, "alarm", &alarm)) {
        printf("Combined alarm  : %s\n", alarm ? "YES" : "no");
        if (alarm) {
            if (get_bool_from_json(payload, "signal_lost", &signal_lost) && signal_lost) {
                printf("  - Signal lost\n");
            }
            if (get_bool_from_json(payload, "spo2_low", &spo2_low) && spo2_low) {
                printf("  - Low SpO2\n");
            }
            if (get_bool_from_json(payload, "heart_rate_abnormal", &heart_rate_abnormal) && heart_rate_abnormal) {
                printf("  - Abnormal heart rate\n");
            }
            if (get_bool_from_json(payload, "line_blocked", &line_blocked) && line_blocked) {
                printf("  - Infusion line blocked/free-flow\n");
            }
            if (get_bool_from_json(payload, "ae_alarm", &ae_alarm) && ae_alarm) {
                printf("  - Autoencoder threshold exceeded\n");
            }
        }
    }

    get_bool_from_json(payload, "hr_signal", &hr_signal);
    get_bool_from_json(payload, "spo2_signal", &spo2_signal);
    get_bool_from_json(payload, "flow_signal", &flow_signal);
    get_bool_from_json(payload, "drops_signal", &drops_signal);
    printf("Channel signal  : HR=%s SpO2=%s Flow=%s Drops=%s\n",
           hr_signal ? "OK" : "lost", spo2_signal ? "OK" : "lost",
           flow_signal ? "OK" : "lost", drops_signal ? "OK" : "lost");

    if (get_int_from_json(payload, "weight_g", &weight_g)) {
        printf("IV bag weight   : %d g\n", weight_g);
    }
    if (get_int_from_json(payload, "drops_per_min", &drops_per_min)) {
        printf("Drops per min   : %d dpm\n", drops_per_min);
    }
    if (get_int_from_json(payload, "target_flow_ml_h", &target_flow_ml_h)) {
        printf("Target flow rate: %d ml/h\n", target_flow_ml_h);
    }
    if (get_int_from_json(payload, "target_drops_per_min", &target_drops_per_min)) {
        printf("Target drop rate: %d dpm\n", target_drops_per_min);
    }
    get_bool_from_json(payload, "tare_in_progress", &tare_in_progress);
    if (get_bool_from_json(payload, "tare_just_completed", &tare_just_completed) && tare_just_completed) {
        printf("Loadcell tare just completed\n");
    }
    if (get_bool_from_json(payload, "hr_baseline_just_completed", &hr_baseline_just_completed) && hr_baseline_just_completed) {
        printf("HR 60s baseline sample just completed\n");
    }
    get_int_from_json(payload, "hr_baseline_seconds_remaining", &hr_baseline_seconds_remaining);
    get_int_from_json(payload, "hr_baseline_bpm", &hr_baseline_bpm);
    get_int_from_json(payload, "tare_event_count", &tare_event_count);
    get_int_from_json(payload, "hr_baseline_event_count", &hr_baseline_event_count);
    if (get_int_from_json(payload, "linkquality", &link_quality)) {
        printf("Link quality    : %d/255\n", link_quality);
    }

    get_bool_from_json(payload, "ts_ready", &ts.ready);
    /* Dang theo doi hay dang cho. Mac dinh 1 khi thiet bi doi cu chua bao
     * truong nay: mot thiet bi dang giam sat that ma bi hien thanh "cho" la
     * kieu sai nguy hiem hon nhieu so voi chieu nguoc lai. */
    int monitoring = 1;
    get_bool_from_json(payload, "monitoring", &monitoring);

    get_bool_from_json(payload, "ts_anomaly", &ts.anomaly);
    get_bool_from_json(payload, "ts_early_warning", &ts.early_warning);
    get_int_from_json(payload, "ts_trend", &ts.hr_trend);
    get_int_from_json(payload, "drops_trend", &ts.drops_trend);
    get_signed_int_from_json(payload, "hr_trend_bpm_per_min", &ts.hr_trend_bpm_per_min);
    get_signed_int_from_json(payload, "drops_trend_dpm_per_min", &ts.drops_trend_dpm_per_min);
    get_int_from_json(payload, "ts_anomaly_score", &ts.anomaly_score_x100);
    /* Tien do OTA di kem ban tin cua chinh thiet bi, trong doi tuong "update".
     * Day la duong DUY NHAT mang phan tram - bridge/response chi bao dau/cuoi.
     * Khong co doan nay thi thanh tien do dung im suot vai phut nap firmware. */
    {
        const char *upd = strstr(payload, "\"update\"");
        if (upd != NULL) {
            char state[32] = "";
            int progress = -1, remaining = -1;
            int installed = -1, latest = -1;
            get_string_from_json(upd, "state", state, sizeof(state));
            get_int_from_json(upd, "progress", &progress);
            get_int_from_json(upd, "remaining", &remaining);
            get_int_from_json(upd, "installed_version", &installed);
            get_int_from_json(upd, "latest_version", &latest);
            if (state[0] != '\0') {
                his_send_ota_status(device_id_for_topic(msg->topic), state,
                                    progress, remaining, "", installed, latest);
            }
        }
    }

    get_bool_from_json(payload, "hr_forecast_trusted", &ts.hr_forecast_trusted);
    get_bool_from_json(payload, "drops_forecast_trusted", &ts.drops_forecast_trusted);

    /* AI v2. Defaults matter: a device still running v1 firmware sends none of
     * these, and must not be reported as "level 0, everything fine" - so
     * line_state stays -1 (unknown) and the level is derived from the legacy
     * alarm flag rather than assumed. */
    ts.line_state = -1;
    ts.remaining_ml = -1;
    ts.remaining_min = -1;
    if (!get_int_from_json(payload, "alert_level", &ts.alert_level)) {
        ts.alert_level = alarm ? 2 : 0;   /* legacy firmware: alarm -> vitals */
    }
    get_bool_from_json(payload, "line_branch", &ts.line_branch);
    get_bool_from_json(payload, "patient_branch", &ts.patient_branch);
    get_bool_from_json(payload, "drip_anomaly", &ts.drip_anomaly);
    get_bool_from_json(payload, "vitals_anomaly", &ts.vitals_anomaly);
    get_int_from_json(payload, "line_state", &ts.line_state);
    get_int_from_json(payload, "remaining_ml", &ts.remaining_ml);
    get_int_from_json(payload, "remaining_min", &ts.remaining_min);
    /* A forecast field arrives as a number or as null; the parse failing IS
     * the null case, and it must stay null all the way to the server. */
    ts.have_hr_forecast = get_int_from_json(payload, "hr_forecast_16s", &ts.hr_forecast_16s);
    ts.have_spo2_forecast = get_int_from_json(payload, "spo2_forecast_16s", &ts.spo2_forecast_16s);
    ts.have_drops_forecast = get_int_from_json(payload, "drops_forecast_16s", &ts.drops_forecast_16s);

    if (ts.ready) {
        printf("AI forecast     : HR+16s=%s SpO2+16s=%s Drops+16s=%s"
               " | HR %+d bpm/min, drops %+d dpm/min | score=%d/100%s%s\n",
               ts.have_hr_forecast ? "yes" : "n/a",
               ts.have_spo2_forecast ? "yes" : "n/a",
               ts.have_drops_forecast ? "yes" : "n/a",
               ts.hr_trend_bpm_per_min, ts.drops_trend_dpm_per_min,
               ts.anomaly_score_x100,
               ts.anomaly ? " [ANOMALY]" : "",
               ts.early_warning ? " [EARLY WARNING]" : "");
    } else {
        printf("AI forecast     : still filling the 64s window\n");
    }

    {
        static const char *level_name[4] = { "NORMAL", "LINE WARNING",
                                             "VITALS ALERT", "CRITICAL" };
        printf("Alert level     : %s%s%s\n",
               level_name[(ts.alert_level >= 0 && ts.alert_level <= 3)
                          ? ts.alert_level : 0],
               ts.line_branch ? "  [line]" : "",
               ts.patient_branch ? "  [patient]" : "");
        if (ts.remaining_min >= 0) {
            printf("Bag remaining   : ~%d mL (~%d min)\n",
                   ts.remaining_ml, ts.remaining_min);
        }
    }

    his_send_bed_data(heart_rate, spo2, flow, drop_rate,
                      hr_signal, spo2_signal, flow_signal, drops_signal,
                      line_blocked, ae_alarm,
                      weight_g, drops_per_min, target_flow_ml_h, target_drops_per_min,
                      tare_in_progress, tare_just_completed, hr_baseline_just_completed,
                      hr_baseline_seconds_remaining, hr_baseline_bpm,
                      tare_event_count, hr_baseline_event_count, link_quality,
                      device_id_for_topic(msg->topic), monitoring, &ts);

    free(payload);
}

int main(int argc, char *argv[])
{
    const char *mqtt_host = DEFAULT_MQTT_HOST;
    int mqtt_port = DEFAULT_MQTT_PORT;
    const char *mqtt_topic = DEFAULT_TOPIC;

    struct mosquitto *mosq;
    int rc;

    /*
     * Optional usage:
     * main.exe
     * main.exe 192.168.1.10
     * main.exe 192.168.1.10 1885
     * main.exe 192.168.1.10 1885 zigbee2mqtt/SmartIV-Sensor
     * main.exe 192.168.1.10 1885 zigbee2mqtt/SmartIV-Sensor <his_host> <his_port> <bed_id> <room>
     *
     * <his_host>: IP/hostname of the machine running the HIS Server
     * (Server_FPT_upload), e.g. 192.168.1.20, or localhost if on the same
     * machine. Leave blank to skip forwarding, just print to the screen as before.
     */
    if (argc >= 2) {
        mqtt_host = argv[1];
    }

    if (argc >= 3) {
        mqtt_port = atoi(argv[2]);
    }

    if (argc >= 4) {
        mqtt_topic = argv[3];
    }

    if (argc >= 5) {
        his_host = argv[4];
    }

    if (argc >= 6) {
        his_port = atoi(argv[5]);
    }

    if (argc >= 7) {
        his_bed_id = argv[6];
    }

    if (argc >= 8) {
        his_room = argv[7];
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

#ifdef _WIN32
    {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            printf("Could not initialize Winsock\n");
            return 1;
        }
    }
#endif

    /*
     * Automatically launch start_services.bat before the C server connects to MQTT.
     */
    start_services_if_needed();

    printf("Smart IV data receiver server (C) is running...\n");
    printf("MQTT host : %s\n", mqtt_host);
    printf("MQTT port : %d\n", mqtt_port);
    printf("Topic     : %s\n", mqtt_topic);
    if (his_host != NULL) {
        printf("HIS Server: %s:%d (bed_id=%s, room=%s)\n", his_host, his_port, his_bed_id, his_room);
    } else {
        printf("HIS Server: not forwarding (screen output only)\n");
    }

    mosquitto_lib_init();

    mosq = mosquitto_new("server_c_zigbee_receiver", true, (void *)mqtt_topic);
    if (mosq == NULL) {
        printf("Could not create MQTT client\n");
        mosquitto_lib_cleanup();
        return 1;
    }

    /* Needed by check_his_commands() to republish a doctor's "set target
     * flow rate" command from the HIS Server down to zigbee2mqtt. */
    g_mosq = mosq;
    snprintf(g_mqtt_set_topic, sizeof(g_mqtt_set_topic), "%s/set", mqtt_topic);

    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_disconnect_callback_set(mosq, on_disconnect);
    mosquitto_message_callback_set(mosq, on_message);

    /*
     * Automatically reconnect if the broker connection is lost.
     */
    mosquitto_reconnect_delay_set(mosq, 2, 10, true);

    rc = mosquitto_connect(mosq, mqtt_host, mqtt_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        printf("Could not connect to MQTT broker: %s\n", mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }

    while (running) {
        rc = mosquitto_loop(mosq, 1000, 1);

        /* Non-blocking check for a command from the HIS Server on the
         * vitals-forwarding socket (e.g. a doctor changing the target flow
         * rate) - cheap to call every iteration since it returns
         * immediately when nothing is waiting. */
        check_his_commands();

        if (rc != MOSQ_ERR_SUCCESS) {
            printf("MQTT loop error: %s\n", mosquitto_strerror(rc));
            printf("Retrying connection...\n");

            /* Wait 1 second before auto-reconnecting, to avoid a tight
             * connect/disconnect loop (spamming the same client ID) if the
             * broker/network has a persistent issue for a few seconds. */
#ifdef _WIN32
            Sleep(1000);
#else
            sleep(1);
#endif
            mosquitto_reconnect(mosq);
        }
    }

    printf("\nExiting program...\n");

    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    if (his_socket != HIS_INVALID_SOCKET) {
        his_close(his_socket);
    }
#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
