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
static void his_send_bed_data(int heart_rate, int spo2, int flow_rate, int drop_rate,
                               int hr_signal, int spo2_signal, int flow_signal,
                               int drops_signal, int line_blocked, int ae_alarm,
                               int weight_g, int drops_per_min, int target_flow_ml_h,
                               int target_drops_per_min, int tare_in_progress,
                               int tare_just_completed, int hr_baseline_just_completed,
                               int hr_baseline_seconds_remaining, int hr_baseline_bpm,
                               int tare_event_count, int hr_baseline_event_count)
{
    char line[800];
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

    snprintf(line, sizeof(line),
             "{\"bedId\":\"%s\",\"room\":\"%s\",\"spo2\":%d,\"heartRate\":%d,"
             "\"dripRate\":%d,\"flowRate\":%d,\"heartRateSignal\":%s,"
             "\"spo2Signal\":%s,\"flowSignal\":%s,\"dripRateSignal\":%s,"
             "\"lineBlocked\":%s,\"aeAlarm\":%s,\"weightG\":%d,"
             "\"dropsPerMin\":%d,\"targetFlowMlH\":%d,\"targetDropsPerMin\":%d,"
             "\"tareInProgress\":%s,\"tareJustCompleted\":%s,"
             "\"hrBaselineJustCompleted\":%s,\"hrBaselineSecondsRemaining\":%d,"
             "\"hrBaselineBpm\":%d,\"tareEventCount\":%d,\"hrBaselineEventCount\":%d}\n",
             his_bed_id, his_room, spo2, heart_rate, drop_rate, flow_rate,
             hr_signal ? "true" : "false", spo2_signal ? "true" : "false",
             flow_signal ? "true" : "false", drops_signal ? "true" : "false",
             line_blocked ? "true" : "false", ae_alarm ? "true" : "false",
             weight_g, drops_per_min, target_flow_ml_h, target_drops_per_min,
             tare_in_progress ? "true" : "false",
             tare_just_completed ? "true" : "false",
             hr_baseline_just_completed ? "true" : "false",
             hr_baseline_seconds_remaining, hr_baseline_bpm,
             tare_event_count, hr_baseline_event_count);

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
            } else if (strstr(his_cmd_buf, "reset_tare") != NULL) {
                publish_mqtt_set("loadcell tare reset", "reset_tare", "true");
            } else if (strstr(his_cmd_buf, "recalibrate_hr_baseline") != NULL) {
                publish_mqtt_set("HR baseline recalibration", "recalibrate_hr_baseline", "true");
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

    his_send_bed_data(heart_rate, spo2, flow, drop_rate,
                      hr_signal, spo2_signal, flow_signal, drops_signal,
                      line_blocked, ae_alarm,
                      weight_g, drops_per_min, target_flow_ml_h, target_drops_per_min,
                      tare_in_progress, tare_just_completed, hr_baseline_just_completed,
                      hr_baseline_seconds_remaining, hr_baseline_bpm,
                      tare_event_count, hr_baseline_event_count);

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
