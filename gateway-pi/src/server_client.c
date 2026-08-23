#define _CRT_SECURE_NO_WARNINGS
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "server_client.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mosquitto/libmosquitto.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET socket_handle;
#define INVALID_SOCKET_HANDLE INVALID_SOCKET
#define CLOSE_SOCKET closesocket
#else
#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_handle;
#define INVALID_SOCKET_HANDLE (-1)
#define CLOSE_SOCKET close
#endif

#define DEFAULT_SERVER_HOST "127.0.0.1"
#define DEFAULT_SERVER_PORT "5000"
#define DEFAULT_BED_ID "BED-01"
#define DEFAULT_ROOM "Unknown room"
#define RECONNECT_DELAY_MS 3000
#define COMMAND_BUFFER_SIZE 2048

static volatile int client_running = 0;
static socket_handle active_socket = INVALID_SOCKET_HANDLE;
static struct mosquitto *mqtt_client = NULL;
static char mqtt_topic[128] = "zigbee2mqtt";
static char current_device[128] = "";
static char announced_device[128] = "";
static char pending_ota_device[128] = "";

static size_t json_escape(const char *source, char *destination, size_t capacity);

#ifdef _WIN32
static HANDLE client_thread_handle = NULL;
static CRITICAL_SECTION client_lock;
#define LOCK() EnterCriticalSection(&client_lock)
#define UNLOCK() LeaveCriticalSection(&client_lock)
#else
static pthread_t client_thread_handle;
static int client_thread_started = 0;
static pthread_mutex_t client_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&client_lock)
#define UNLOCK() pthread_mutex_unlock(&client_lock)
#endif

static const char *env_or_default(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value != NULL && value[0] != '\0' ? value : fallback;
}

static void sleep_milliseconds(unsigned milliseconds)
{
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000U);
    delay.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    nanosleep(&delay, NULL);
#endif
}

static void close_active_socket(socket_handle expected)
{
    LOCK();
    if (active_socket == expected) {
        CLOSE_SOCKET(active_socket);
        active_socket = INVALID_SOCKET_HANDLE;
    }
    UNLOCK();
}

static socket_handle connect_to_server(void)
{
    const char *host = env_or_default("HIS_SERVER_HOST", DEFAULT_SERVER_HOST);
    const char *port = env_or_default("HIS_SERVER_PORT", DEFAULT_SERVER_PORT);
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    socket_handle connected = INVALID_SOCKET_HANDLE;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, port, &hints, &addresses) != 0) {
        return INVALID_SOCKET_HANDLE;
    }

    for (address = addresses; address != NULL; address = address->ai_next) {
        connected = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (connected == INVALID_SOCKET_HANDLE) {
            continue;
        }
        if (connect(connected, address->ai_addr, (int)address->ai_addrlen) == 0) {
            break;
        }
        CLOSE_SOCKET(connected);
        connected = INVALID_SOCKET_HANDLE;
    }

    freeaddrinfo(addresses);
    return connected;
}

static bool json_string(const char *json, const char *field, char *output, size_t output_size)
{
    char key[96];
    const char *start;
    const char *end;

    if (snprintf(key, sizeof(key), "\"%s\"", field) < 0) return false;
    start = strstr(json, key);
    if (start == NULL || (start = strchr(start + strlen(key), ':')) == NULL) return false;
    do { start++; } while (*start == ' ' || *start == '\t');
    if (*start != '"') return false;
    start++;
    end = strchr(start, '"');
    if (end == NULL || (size_t)(end - start) >= output_size) return false;
    memcpy(output, start, (size_t)(end - start));
    output[end - start] = '\0';
    return true;
}

static bool json_integer(const char *json, const char *field, long *value)
{
    char key[96];
    const char *start;
    char *end;

    if (snprintf(key, sizeof(key), "\"%s\"", field) < 0) return false;
    start = strstr(json, key);
    if (start == NULL || (start = strchr(start + strlen(key), ':')) == NULL) return false;
    errno = 0;
    *value = strtol(start + 1, &end, 10);
    return end != start + 1 && errno != ERANGE;
}

static bool json_number(const char *json, const char *field, double *value)
{
    char key[96];
    const char *start;
    char *end;

    if (snprintf(key, sizeof(key), "\"%s\"", field) < 0) return false;
    start = strstr(json, key);
    if (start == NULL || (start = strchr(start + strlen(key), ':')) == NULL) return false;
    errno = 0;
    *value = strtod(start + 1, &end);
    return end != start + 1 && errno != ERANGE;
}

static bool json_boolean(const char *json, const char *field, bool *value)
{
    char key[96];
    const char *start;

    if (snprintf(key, sizeof(key), "\"%s\"", field) < 0) return false;
    start = strstr(json, key);
    if (start == NULL || (start = strchr(start + strlen(key), ':')) == NULL) return false;
    do { start++; } while (*start == ' ' || *start == '\t');
    if (strncmp(start, "true", 4) == 0) {
        *value = true;
        return true;
    }
    if (strncmp(start, "false", 5) == 0) {
        *value = false;
        return true;
    }
    return false;
}

static void send_ota_status(
    const char *device,
    const char *state,
    int progress,
    int remaining,
    const char *message,
    int installed_version,
    int latest_version)
{
    char escaped_device[256];
    char escaped_state[64];
    char escaped_message[512];
    char line[1200];
    int length;
    socket_handle server_socket;
    size_t sent = 0;

    if (device == NULL || device[0] == '\0' || state == NULL || state[0] == '\0') return;
    json_escape(device, escaped_device, sizeof(escaped_device));
    json_escape(state, escaped_state, sizeof(escaped_state));
    json_escape(message != NULL ? message : "", escaped_message, sizeof(escaped_message));
    length = snprintf(line, sizeof(line),
        "{\"type\":\"ota_status\",\"deviceId\":\"%s\",\"state\":\"%s\"," 
        "\"progress\":%d,\"remainingSeconds\":%d,\"message\":\"%s\"," 
        "\"installedVersion\":%d,\"latestVersion\":%d}\n",
        escaped_device, escaped_state, progress, remaining, escaped_message,
        installed_version, latest_version);
    if (length <= 0 || (size_t)length >= sizeof(line)) return;

    LOCK();
    server_socket = active_socket;
    while (server_socket != INVALID_SOCKET_HANDLE && sent < (size_t)length) {
        int result = send(server_socket, line + sent, (int)((size_t)length - sent), 0);
        if (result <= 0) break;
        sent += (size_t)result;
    }
    UNLOCK();
    if (sent == (size_t)length) {
        printf("OTA -> HIS: device=%s state=%s progress=%d installed=%d latest=%d\n",
            device, state, progress, installed_version, latest_version);
        fflush(stdout);
    } else if (server_socket != INVALID_SOCKET_HANDLE) {
        fprintf(stderr, "Khong gui duoc trang thai OTA cua %s len HIS server\n", device);
    }
}

static void publish_command(const char *line)
{
    char command[64];
    char device[128];
    char topic[384];
    char payload[256];
    long value = 0;
    bool ota_command = false;
    bool ota_update = false;
    const char *suffix = "set";
    const char *configured_device = getenv("ZIGBEE_DEVICE_ID");

    if (!json_string(line, "cmd", command, sizeof(command))) return;

    LOCK();
    snprintf(device, sizeof(device), "%s",
        configured_device != NULL && configured_device[0] != '\0'
            ? configured_device : current_device);
    UNLOCK();
    (void)json_string(line, "deviceId", device, sizeof(device));

    if (strcmp(command, "rescan_devices") == 0) {
        snprintf(topic, sizeof(topic), "%s/bridge/request/devices", mqtt_topic);
        snprintf(payload, sizeof(payload), "{}");
    } else if (strcmp(command, "ota_check") == 0 || strcmp(command, "ota_update") == 0) {
        const char *action = strcmp(command, "ota_check") == 0 ? "check" : "update";
        if (device[0] == '\0') return;
        ota_command = true;
        ota_update = strcmp(command, "ota_update") == 0;
        snprintf(topic, sizeof(topic), "%s/bridge/request/device/ota_update/%s", mqtt_topic, action);
        snprintf(payload, sizeof(payload), "{\"id\":\"%s\"}", device);
    } else {
        if (device[0] == '\0') return;
        snprintf(topic, sizeof(topic), "%s/%s/%s", mqtt_topic, device, suffix);
        if (strcmp(command, "set_target_flow_ml_h") == 0 && json_integer(line, "value", &value)) {
            snprintf(payload, sizeof(payload), "{\"target_flow_ml_h\":%ld}", value);
        } else if (strcmp(command, "set_target_drops_per_min") == 0 && json_integer(line, "value", &value)) {
            snprintf(payload, sizeof(payload), "{\"target_drops_per_min\":%ld}", value);
        } else if (strcmp(command, "set_monitoring") == 0 && json_integer(line, "value", &value)) {
            snprintf(payload, sizeof(payload), "{\"monitoring\":%s}", value != 0 ? "true" : "false");
        } else if (strcmp(command, "reset_tare") == 0) {
            snprintf(payload, sizeof(payload), "{\"reset_tare\":true}");
        } else if (strcmp(command, "recalibrate_hr_baseline") == 0) {
            snprintf(payload, sizeof(payload), "{\"recalibrate_hr_baseline\":true}");
        } else {
            return;
        }
    }

    if (mosquitto_publish(mqtt_client, NULL, topic, (int)strlen(payload), payload, 0, false)
        == MOSQ_ERR_SUCCESS) {
        if (ota_command) {
            LOCK();
            snprintf(pending_ota_device, sizeof(pending_ota_device), "%s", device);
            UNLOCK();
            if (ota_update) {
                send_ota_status(device, "starting", -1, -1, "Update requested", -1, -1);
            }
        }
        printf("Lenh server -> MQTT: %s %s\n", topic, payload);
        fflush(stdout);
    }
}

void server_client_handle_mqtt_message(
    const char *topic,
    const void *payload,
    int payload_length)
{
    char *json;
    char prefix[192];
    const char *relative;

    if (topic == NULL || payload == NULL || payload_length <= 0) return;
    if (snprintf(prefix, sizeof(prefix), "%s/", mqtt_topic) < 0) return;
    if (strncmp(topic, prefix, strlen(prefix)) != 0) return;
    relative = topic + strlen(prefix);

    json = malloc((size_t)payload_length + 1U);
    if (json == NULL) return;
    memcpy(json, payload, (size_t)payload_length);
    json[payload_length] = '\0';

    if (strncmp(relative, "bridge/response/device/ota_update/", 34) == 0) {
        char device[128] = "";
        char status[32] = "";
        char error[384] = "";
        bool available = false;
        bool has_available;

        (void)json_string(json, "id", device, sizeof(device));
        (void)json_string(json, "status", status, sizeof(status));
        (void)json_string(json, "error", error, sizeof(error));
        if (device[0] == '\0') {
            LOCK();
            snprintf(device, sizeof(device), "%s", pending_ota_device);
            UNLOCK();
        }

        if (device[0] != '\0' && strcmp(status, "error") == 0) {
            send_ota_status(device, "failed", -1, -1,
                error[0] != '\0' ? error : "Zigbee2MQTT OTA request failed", -1, -1);
        } else if (device[0] != '\0' && strstr(relative, "/check") != NULL) {
            has_available = json_boolean(json, "update_available", &available)
                || json_boolean(json, "updateAvailable", &available);
            if (has_available) {
                send_ota_status(device, available ? "available" : "upToDate",
                    -1, -1, "", -1, -1);
            } else {
                send_ota_status(device, "failed", -1, -1,
                    "Zigbee2MQTT OTA response did not contain update_available", -1, -1);
            }
        } else if (device[0] != '\0' && strstr(relative, "/update") != NULL) {
            send_ota_status(device, "done", 100, 0, "Update finished", -1, -1);
        }
        free(json);
        return;
    }

    /* A direct device state carries the only continuous OTA progress stream:
     * {"update":{"state":"updating","progress":13.37,...}}. Forward it
     * even when the rest of the payload is ordinary sensor data. */
    if (strchr(relative, '/') == NULL && strcmp(relative, "bridge") != 0) {
        const char *update = strstr(json, "\"update\"");
        char state[32] = "";
        double number;
        int progress = -1;
        int remaining = -1;
        int installed = -1;
        int latest = -1;

        if (update != NULL && json_string(update, "state", state, sizeof(state))) {
            if (json_number(update, "progress", &number)) progress = (int)(number + 0.5);
            if (json_number(update, "remaining", &number)) remaining = (int)(number + 0.5);
            if (json_number(update, "installed_version", &number)) installed = (int)number;
            if (json_number(update, "latest_version", &number)) latest = (int)number;
            send_ota_status(relative, state, progress, remaining, "", installed, latest);
        }
    }
    free(json);
}

static void receive_commands(socket_handle server_socket)
{
    char buffer[COMMAND_BUFFER_SIZE];
    size_t used = 0;

    while (client_running) {
        int received = recv(server_socket, buffer + used, (int)(sizeof(buffer) - used - 1U), 0);
        char *line_start;
        char *newline;
        if (received <= 0) break;
        used += (size_t)received;
        buffer[used] = '\0';

        line_start = buffer;
        while ((newline = strchr(line_start, '\n')) != NULL) {
            *newline = '\0';
            if (newline > line_start && newline[-1] == '\r') newline[-1] = '\0';
            publish_command(line_start);
            line_start = newline + 1;
        }
        used -= (size_t)(line_start - buffer);
        memmove(buffer, line_start, used);
        if (used == sizeof(buffer) - 1U) used = 0;
    }
}

#ifdef _WIN32
static DWORD WINAPI client_thread(void *unused)
#else
static void *client_thread(void *unused)
#endif
{
    (void)unused;
    while (client_running) {
        socket_handle connected = connect_to_server();
        if (connected == INVALID_SOCKET_HANDLE) {
            sleep_milliseconds(RECONNECT_DELAY_MS);
            continue;
        }

        LOCK();
        active_socket = connected;
        announced_device[0] = '\0';
        UNLOCK();
        printf("Da ket noi HIS server %s:%s\n",
            env_or_default("HIS_SERVER_HOST", DEFAULT_SERVER_HOST),
            env_or_default("HIS_SERVER_PORT", DEFAULT_SERVER_PORT));
        fflush(stdout);

        receive_commands(connected);
        close_active_socket(connected);
        if (client_running) {
            fprintf(stderr, "Mat ket noi HIS server; se tu ket noi lai\n");
        }
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

bool server_client_start(struct mosquitto *mqtt, const char *mqtt_base_topic)
{
#ifdef _WIN32
    WSADATA winsock_data;
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) return false;
    InitializeCriticalSection(&client_lock);
#endif
    mqtt_client = mqtt;
    snprintf(mqtt_topic, sizeof(mqtt_topic), "%s", mqtt_base_topic);
    client_running = 1;
#ifdef _WIN32
    client_thread_handle = CreateThread(NULL, 0, client_thread, NULL, 0, NULL);
    if (client_thread_handle == NULL) {
        client_running = 0;
        DeleteCriticalSection(&client_lock);
        WSACleanup();
        return false;
    }
#else
    if (pthread_create(&client_thread_handle, NULL, client_thread, NULL) != 0) {
        client_running = 0;
        return false;
    }
    client_thread_started = 1;
#endif
    return true;
}

void server_client_stop(void)
{
    if (!client_running) return;
    client_running = 0;
    LOCK();
    if (active_socket != INVALID_SOCKET_HANDLE) {
#ifdef _WIN32
        shutdown(active_socket, SD_BOTH);
#else
        shutdown(active_socket, SHUT_RDWR);
#endif
    }
    UNLOCK();
#ifdef _WIN32
    WaitForSingleObject(client_thread_handle, INFINITE);
    CloseHandle(client_thread_handle);
    client_thread_handle = NULL;
    DeleteCriticalSection(&client_lock);
    WSACleanup();
#else
    if (client_thread_started) pthread_join(client_thread_handle, NULL);
    client_thread_started = 0;
#endif
}

static size_t json_escape(const char *source, char *destination, size_t capacity)
{
    size_t used = 0;
    while (*source != '\0' && used + 2U < capacity) {
        if (*source == '"' || *source == '\\') destination[used++] = '\\';
        destination[used++] = *source++;
    }
    destination[used] = '\0';
    return used;
}

void server_client_send_reading(const char *device_id, const void *payload, int payload_length)
{
    const char *bed_id = env_or_default("BED_ID", DEFAULT_BED_ID);
    const char *room = env_or_default("BED_ROOM", DEFAULT_ROOM);
    char escaped_bed[128];
    char escaped_room[128];
    char escaped_device[256];
    char *line;
    int prefix_length;
    size_t capacity;
    socket_handle server_socket;
    char announce[512];

    if (payload == NULL || payload_length < 2 || ((const char *)payload)[0] != '{') return;
    json_escape(bed_id, escaped_bed, sizeof(escaped_bed));
    json_escape(room, escaped_room, sizeof(escaped_room));
    json_escape(device_id, escaped_device, sizeof(escaped_device));

    capacity = (size_t)payload_length + strlen(escaped_bed) + strlen(escaped_room)
        + strlen(escaped_device) + 64U;
    line = malloc(capacity);
    if (line == NULL) return;
    prefix_length = snprintf(line, capacity,
        "{\"bedId\":\"%s\",\"room\":\"%s\",\"deviceId\":\"%s\",",
        escaped_bed, escaped_room, escaped_device);
    if (prefix_length < 0) {
        free(line);
        return;
    }
    memcpy(line + prefix_length, (const char *)payload + 1, (size_t)payload_length - 1U);
    line[prefix_length + payload_length - 1] = '\n';
    line[prefix_length + payload_length] = '\0';

    LOCK();
    snprintf(current_device, sizeof(current_device), "%s", device_id);
    server_socket = active_socket;
    if (server_socket != INVALID_SOCKET_HANDLE) {
        if (strcmp(announced_device, device_id) != 0) {
            int announce_length = snprintf(announce, sizeof(announce),
                "{\"type\":\"device_announce\",\"deviceId\":\"%s\",\"friendlyName\":\"%s\"}\n",
                escaped_device, escaped_device);
            if (announce_length > 0
                && send(server_socket, announce, announce_length, 0) == announce_length) {
                snprintf(announced_device, sizeof(announced_device), "%s", device_id);
            }
        }
        size_t total = (size_t)prefix_length + (size_t)payload_length;
        size_t sent = 0;
        while (sent < total) {
            int result = send(server_socket, line + sent, (int)(total - sent), 0);
            if (result <= 0) break;
            sent += (size_t)result;
        }
    }
    UNLOCK();
    free(line);
}
