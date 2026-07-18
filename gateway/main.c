/*
 * main.c
 *
 * Chuong trinh C chay tren Windows / Raspberry Pi / Linux
 *
 * Nhiem vu:
 * - Tu dong goi start_services.bat tren Windows de chay:
 *   + Mosquitto MQTT broker
 *   + Zigbee2MQTT
 * - Ket noi MQTT broker
 * - Subscribe topic Zigbee2MQTT cua thiet bi SmartIV-Sensor (empty_2)
 * - Doc cac truong JSON do zigbee2mqtt_smart_iv_converter.js expose:
 *   heart_rate, spo2, flow, drop_rate, alarm, signal_lost, spo2_low,
 *   heart_rate_abnormal, line_blocked, ae_alarm, hr_signal, spo2_signal,
 *   flow_signal, drops_signal
 *
 * Vi du payload nhan duoc:
 * {"heart_rate":81,"spo2":97,"flow":100,"drop_rate":100,"alarm":false,
 *  "signal_lost":false,"spo2_low":false,"heart_rate_abnormal":false,
 *  "line_blocked":false,"ae_alarm":false,"hr_signal":false,"spo2_signal":false,
 *  "flow_signal":false,"drops_signal":true,"linkquality":196}
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

/* Doi lai cho dung device_id/friendly_name thuc te sau khi pair trong
 * zigbee2mqtt (xem trong frontend hoac configuration.yaml). */
#define DEFAULT_DEVICE_ID "0x64028ffffe641802"
#define DEFAULT_TOPIC "zigbee2mqtt/0x64028ffffe641802"

#define BUFFER_SIZE 256

#define AUTO_START_SERVICES 1
#define START_SERVICES_BAT "start_services.bat"

/* Ket noi TCP sang HIS Server (Server_FPT_upload/Server, BedDataReceiver.cs,
 * cong mac dinh 5000). Neu his_host == NULL thi khong forward, chi in man
 * hinh nhu truoc. */
static const char *his_host = NULL;
static int his_port = 5000;
static const char *his_bed_id = "BED-101";
static const char *his_room = "ICU-1";
static his_socket_t his_socket = HIS_INVALID_SOCKET;

static int running = 1;

/*
 * Tu dong chay start_services.bat truoc khi ket noi MQTT.
 *
 * Luu y:
 * - File start_services.bat phai nam cung thu muc voi main.exe
 * - start_services.bat chi nen chay Mosquitto va Zigbee2MQTT
 * - Khong nen de start_services.bat chay lai main.exe, tranh lap vo han
 */
void start_services_if_needed(void)
{
#if defined(_WIN32)
    if (AUTO_START_SERVICES == 0) {
        return;
    }

    DWORD attr = GetFileAttributesA(START_SERVICES_BAT);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        printf("Khong tim thay %s, bo qua buoc tu dong khoi dong dich vu\n", START_SERVICES_BAT);
        return;
    }

    printf("Dang goi %s de khoi dong Mosquitto va Zigbee2MQTT...\n", START_SERVICES_BAT);

    int ret = system("cmd /c start_services.bat");

    printf("Da goi %s, ma tra ve: %d\n", START_SERVICES_BAT, ret);
    printf("Cho dich vu khoi dong...\n");

    /*
     * Cho start_services.bat mo cac cua so dich vu.
     * Zigbee2MQTT thuong can vai giay de ket noi coordinator va MQTT.
     */
    Sleep(3000);
#else
    printf("He dieu hanh khong phai Windows, bo qua file .bat\n");
#endif
}

/*
 * Bat Ctrl + C de thoat chuong trinh gon gang.
 */
void handle_signal(int signal_number)
{
    (void)signal_number;
    running = 0;
}

/*
 * Lay gia tri so tu JSON don gian.
 *
 * Vi du payload:
 * {"brightness_1":72,"brightness_2":65,"linkquality":216,"state_1":"ON"}
 *
 * Goi:
 * lay_gia_tri_so_tu_json(payload, "brightness_1", &value)
 *
 * Ket qua:
 * value = 72
 */
int lay_gia_tri_so_tu_json(const char *json, const char *key, int *value)
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
 * Lay gia tri bool tu JSON don gian (true/false).
 *
 * Vi du: lay_gia_tri_bool_tu_json(payload, "alarm", &value)
 */
int lay_gia_tri_bool_tu_json(const char *json, const char *key, int *value)
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
 * Mo ket noi TCP toi HIS Server (BedDataReceiver, cong 5000). Tra ve
 * HIS_INVALID_SOCKET neu khong ket noi duoc.
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
 * Gui 1 dong JSON (tieng Anh, khong con truong tieng Viet) sang HIS Server
 * (BedTcpIngestionService.cs / BedDataParser.cs), ket thuc bang '\n'. Server
 * tu tinh status/alert content tu cac gia tri so - gateway chi gui du lieu
 * tho + trang thai tin hieu tung kenh, khong tu suy dien trang thai nua.
 */
static void his_send_bed_data(int heart_rate, int spo2, int flow_rate, int drop_rate,
                               int hr_signal, int spo2_signal, int flow_signal,
                               int drops_signal, int line_blocked, int ae_alarm)
{
    char line[512];
    size_t len;

    if (his_host == NULL) {
        return;
    }

    if (his_socket == HIS_INVALID_SOCKET) {
        his_socket = his_connect(his_host, his_port);
        if (his_socket == HIS_INVALID_SOCKET) {
            printf("Khong ket noi duoc HIS Server %s:%d\n", his_host, his_port);
            return;
        }
        printf("Da ket noi HIS Server %s:%d (TCP)\n", his_host, his_port);
    }

    snprintf(line, sizeof(line),
             "{\"bedId\":\"%s\",\"room\":\"%s\",\"spo2\":%d,\"heartRate\":%d,"
             "\"dripRate\":%d,\"flowRate\":%d,\"heartRateSignal\":%s,"
             "\"spo2Signal\":%s,\"flowSignal\":%s,\"dripRateSignal\":%s,"
             "\"lineBlocked\":%s,\"aeAlarm\":%s}\n",
             his_bed_id, his_room, spo2, heart_rate, drop_rate, flow_rate,
             hr_signal ? "true" : "false", spo2_signal ? "true" : "false",
             flow_signal ? "true" : "false", drops_signal ? "true" : "false",
             line_blocked ? "true" : "false", ae_alarm ? "true" : "false");

    len = strlen(line);
    if (send(his_socket, line, (int)len, 0) < 0) {
        printf("Mat ket noi HIS Server, se thu ket noi lai o lan gui sau\n");
        his_close(his_socket);
        his_socket = HIS_INVALID_SOCKET;
    }
}

/*
 * Callback khi ket noi MQTT broker.
 */
void on_connect(struct mosquitto *mosq, void *userdata, int rc)
{
    const char *topic = (const char *)userdata;

    if (rc == 0) {
        printf("Da ket noi MQTT broker\n");
        printf("Dang lang nghe topic: %s\n", topic);

        int sub_rc = mosquitto_subscribe(mosq, NULL, topic, 0);
        if (sub_rc != MOSQ_ERR_SUCCESS) {
            printf("Loi subscribe topic: %s\n", mosquitto_strerror(sub_rc));
        }
    } else {
        printf("Loi ket noi MQTT broker, ma loi: %d\n", rc);
    }
}

/*
 * Callback khi mat ket noi MQTT.
 */
void on_disconnect(struct mosquitto *mosq, void *userdata, int rc)
{
    (void)mosq;
    (void)userdata;

    printf("Mat ket noi MQTT, rc = %d\n", rc);

    if (rc != 0) {
        printf("Dang cho tu dong ket noi lai...\n");
    }
}

/*
 * Callback khi nhan du lieu MQTT.
 *
 * Doc 5 gia tri AI (heart_rate, spo2, flow, drop_rate, alarm bitmap da duoc
 * zigbee2mqtt_smart_iv_converter.js giai ma san thanh cac truong JSON ro
 * rang) va in ra man hinh.
 */
void on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg)
{
    (void)mosq;
    (void)userdata;

    char *payload;
    int heart_rate = 0, spo2 = 0, flow = 0, drop_rate = 0;
    int alarm = 0, signal_lost = 0, spo2_low = 0, heart_rate_abnormal = 0, line_blocked = 0, ae_alarm = 0;
    int hr_signal = 0, spo2_signal = 0, flow_signal = 0, drops_signal = 0;

    if (msg == NULL || msg->payload == NULL || msg->payloadlen <= 0) {
        return;
    }

    payload = (char *)malloc((size_t)msg->payloadlen + 1);
    if (payload == NULL) {
        printf("Loi cap phat bo nho\n");
        return;
    }

    memcpy(payload, msg->payload, (size_t)msg->payloadlen);
    payload[msg->payloadlen] = '\0';

    printf("\nNhan MQTT\n");
    printf("Topic: %s\n", msg->topic);
    printf("Payload: %s\n", payload);

    if (lay_gia_tri_so_tu_json(payload, "heart_rate", &heart_rate)) {
        printf("Nhip tim (HR)  : %d bpm\n", heart_rate);
    }
    if (lay_gia_tri_so_tu_json(payload, "spo2", &spo2)) {
        printf("SpO2           : %d %%\n", spo2);
    }
    if (lay_gia_tri_so_tu_json(payload, "flow", &flow)) {
        printf("Luu luong (Flow): %d %%\n", flow);
    }
    if (lay_gia_tri_so_tu_json(payload, "drop_rate", &drop_rate)) {
        printf("Toc do giot     : %d %%\n", drop_rate);
    }

    if (lay_gia_tri_bool_tu_json(payload, "alarm", &alarm)) {
        printf("Bao dong tong hop: %s\n", alarm ? "CO" : "khong");
        if (alarm) {
            if (lay_gia_tri_bool_tu_json(payload, "signal_lost", &signal_lost) && signal_lost) {
                printf("  - Mat tin hieu\n");
            }
            if (lay_gia_tri_bool_tu_json(payload, "spo2_low", &spo2_low) && spo2_low) {
                printf("  - SpO2 thap\n");
            }
            if (lay_gia_tri_bool_tu_json(payload, "heart_rate_abnormal", &heart_rate_abnormal) && heart_rate_abnormal) {
                printf("  - Nhip tim bat thuong\n");
            }
            if (lay_gia_tri_bool_tu_json(payload, "line_blocked", &line_blocked) && line_blocked) {
                printf("  - Duong truyen tac/free-flow\n");
            }
            if (lay_gia_tri_bool_tu_json(payload, "ae_alarm", &ae_alarm) && ae_alarm) {
                printf("  - Autoencoder vuot nguong\n");
            }
        }
    }

    lay_gia_tri_bool_tu_json(payload, "hr_signal", &hr_signal);
    lay_gia_tri_bool_tu_json(payload, "spo2_signal", &spo2_signal);
    lay_gia_tri_bool_tu_json(payload, "flow_signal", &flow_signal);
    lay_gia_tri_bool_tu_json(payload, "drops_signal", &drops_signal);
    printf("Tin hieu kenh   : HR=%s SpO2=%s Flow=%s Drops=%s\n",
           hr_signal ? "OK" : "mat", spo2_signal ? "OK" : "mat",
           flow_signal ? "OK" : "mat", drops_signal ? "OK" : "mat");

    his_send_bed_data(heart_rate, spo2, flow, drop_rate,
                      hr_signal, spo2_signal, flow_signal, drops_signal,
                      line_blocked, ae_alarm);

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
     * Cach chay tuy chon:
     * main.exe
     * main.exe 192.168.1.10
     * main.exe 192.168.1.10 1885
     * main.exe 192.168.1.10 1885 zigbee2mqtt/SmartIV-Sensor
     * main.exe 192.168.1.10 1885 zigbee2mqtt/SmartIV-Sensor <his_host> <his_port> <ma_giuong> <phong>
     *
     * <his_host>: IP/hostname may chay HIS Server (Server_FPT_upload), vi du
     * 192.168.1.20 hoac localhost neu cung may. Bo trong thi khong forward,
     * chi in ra man hinh nhu truoc.
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
            printf("Khong khoi tao duoc Winsock\n");
            return 1;
        }
    }
#endif

    /*
     * Tu dong chay start_services.bat truoc khi server C ket noi MQTT.
     */
    start_services_if_needed();

    printf("Server C nhan du lieu Smart IV dang chay...\n");
    printf("MQTT host : %s\n", mqtt_host);
    printf("MQTT port : %d\n", mqtt_port);
    printf("Topic     : %s\n", mqtt_topic);
    if (his_host != NULL) {
        printf("HIS Server: %s:%d (ma giuong=%s, phong=%s)\n", his_host, his_port, his_bed_id, his_room);
    } else {
        printf("HIS Server: khong forward (chi in man hinh)\n");
    }

    mosquitto_lib_init();

    mosq = mosquitto_new("server_c_zigbee_receiver", true, (void *)mqtt_topic);
    if (mosq == NULL) {
        printf("Khong tao duoc MQTT client\n");
        mosquitto_lib_cleanup();
        return 1;
    }

    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_disconnect_callback_set(mosq, on_disconnect);
    mosquitto_message_callback_set(mosq, on_message);

    /*
     * Tu dong ket noi lai neu mat broker.
     */
    mosquitto_reconnect_delay_set(mosq, 2, 10, true);

    rc = mosquitto_connect(mosq, mqtt_host, mqtt_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        printf("Khong ket noi duoc MQTT broker: %s\n", mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }

    while (running) {
        rc = mosquitto_loop(mosq, 1000, 1);

        if (rc != MOSQ_ERR_SUCCESS) {
            printf("Loi MQTT loop: %s\n", mosquitto_strerror(rc));
            printf("Thu ket noi lai...\n");

            /* Doi 1 giay truoc khi tu reconnect, tranh vong lap ket noi/ngat
             * lien tuc (spam client cung ID) neu broker/mang co van de tam
             * thoi lien tuc trong vai giay. */
#ifdef _WIN32
            Sleep(1000);
#else
            sleep(1);
#endif
            mosquitto_reconnect(mosq);
        }
    }

    printf("\nDang thoat chuong trinh...\n");

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