#define _CRT_SECURE_NO_WARNINGS
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <mosquitto.h>
#include "gateway_config.h"
#include "server_client.h"

#define DEFAULT_MQTT_HOST "127.0.0.1"
#define DEFAULT_MQTT_PORT 1885
#define DEFAULT_BASE_TOPIC "zigbee2mqtt"
#define KEEPALIVE_SECONDS 60

static volatile sig_atomic_t running = 1;
static const char *base_topic = DEFAULT_BASE_TOPIC;

static void sleep_one_second(void)
{
#ifdef _WIN32
    Sleep(1000);
#else
    sleep(1);
#endif
}

static void get_local_time(const time_t *now, struct tm *result)
{
#ifdef _WIN32
    localtime_s(result, now);
#else
    localtime_r(now, result);
#endif
}
static void handle_signal(int signal_number)
{
    (void)signal_number;
    running = 0;
}

static const char *get_env_or_default(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value != NULL && value[0] != '\0' ? value : fallback;
}

static int get_port_from_env(void)
{
    const char *value = getenv("MQTT_PORT");
    if (value == NULL || value[0] == '\0') {
        return DEFAULT_MQTT_PORT;
    }

    char *end = NULL;
    errno = 0;
    long port = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "MQTT_PORT khong hop le: %s\n", value);
        exit(EXIT_FAILURE);
    }

    return (int)port;
}

static bool extract_json_number(
    const void *payload,
    int payload_length,
    const char *field,
    double *value)
{
    if (payload == NULL || payload_length <= 0 || field == NULL || value == NULL) {
        return false;
    }

    char *json = malloc((size_t)payload_length + 1U);
    if (json == NULL) {
        return false;
    }

    memcpy(json, payload, (size_t)payload_length);
    json[payload_length] = '\0';

    char key[64];
    int key_length = snprintf(key, sizeof(key), "\"%s\"", field);
    if (key_length < 0 || (size_t)key_length >= sizeof(key)) {
        free(json);
        return false;
    }

    char *position = strstr(json, key);
    if (position == NULL || (position = strchr(position + key_length, ':')) == NULL) {
        free(json);
        return false;
    }

    char *end = NULL;
    errno = 0;
    double parsed = strtod(position + 1, &end);
    bool valid = end != position + 1 && errno != ERANGE;
    if (valid) {
        *value = parsed;
    }

    free(json);
    return valid;
}

static bool payload_has_clinical_data(const void *payload, int payload_length)
{
    static const char *fields[] = {
        "\"heart_rate\"", "\"spo2\"", "\"flow\"", "\"drop_rate\"",
        "\"drops_per_min\"", "\"weight_g\"", "\"alert_level\"", "\"final_alert_level\"",
        "\"monitoring\"", "\"drop_training_samples\"", "\"vitals_training_samples\"",
        "\"alerts_armed\"", "\"drop_interval_ms\"", "\"drop_event_count\"",
        "\"server_drop_level\"", "\"vitals_test_mode\"",
        "\"ai_input_heart_rate\"", "\"ai_input_spo2\"", "\"vitals_level\""
    };
    char *json;
    size_t index;
    bool found = false;

    if (payload == NULL || payload_length <= 0) return false;
    json = malloc((size_t)payload_length + 1U);
    if (json == NULL) return false;
    memcpy(json, payload, (size_t)payload_length);
    json[payload_length] = '\0';
    for (index = 0; index < sizeof(fields) / sizeof(fields[0]); index++) {
        if (strstr(json, fields[index]) != NULL) {
            found = true;
            break;
        }
    }
    free(json);
    return found;
}

static void handle_zigbee_data(
    const char *device_name,
    const char *topic,
    const void *payload,
    int payload_length)
{
    time_t now = time(NULL);
    struct tm local_time;
    get_local_time(&now, &local_time);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local_time);

    double temperature_celsius;
    bool has_temperature = extract_json_number(
        payload, payload_length, "temperature", &temperature_celsius);

    printf("[%s] device=%s topic=%s", timestamp, device_name, topic);
    if (has_temperature) {
        printf(" temperature=%.2f C", temperature_celsius);
    }
    printf(" payload=");
    if (payload != NULL && payload_length > 0) {
        fwrite(payload, 1, (size_t)payload_length, stdout);
    }
    putchar('\n');
    fflush(stdout);
    if (payload_has_clinical_data(payload, payload_length)) {
        server_client_send_reading(device_name, payload, payload_length);
    }
}

static void extract_device_name(const char *topic, char *output, size_t output_size)
{
    size_t prefix_length = strlen(base_topic);
    const char *start = topic;

    if (strncmp(topic, base_topic, prefix_length) == 0 && topic[prefix_length] == '/') {
        start = topic + prefix_length + 1;
    }

    const char *end = strchr(start, '/');
    size_t length = end != NULL ? (size_t)(end - start) : strlen(start);
    if (length >= output_size) {
        length = output_size - 1;
    }

    memcpy(output, start, length);
    output[length] = '\0';
}

static void on_connect(
    struct mosquitto *mosq,
    void *userdata,
    int result_code)
{
    (void)userdata;

    if (result_code != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Ket noi MQTT that bai, ma loi=%d\n", result_code);
        return;
    }

    char topic_filter[256];
    int written = snprintf(topic_filter, sizeof(topic_filter), "%s/#", base_topic);
    if (written < 0 || (size_t)written >= sizeof(topic_filter)) {
        fprintf(stderr, "Base topic qua dai\n");
        running = 0;
        return;
    }

    int rc = mosquitto_subscribe(mosq, NULL, topic_filter, 0);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Subscribe %s that bai, ma loi=%d\n", topic_filter, rc);
        running = 0;
        return;
    }

    printf("Da ket noi MQTT, dang nghe %s\n", topic_filter);
    fflush(stdout);
}

static void on_disconnect(
    struct mosquitto *mosq,
    void *userdata,
    int result_code)
{
    (void)mosq;
    (void)userdata;

    if (result_code != MOSQ_ERR_SUCCESS && running) {
        fprintf(stderr, "Mat ket noi MQTT, ma loi=%d; client se tu ket noi lai\n", result_code);
    }
}

static void on_message(
    struct mosquitto *mosq,
    void *userdata,
    const struct mosquitto_message *message)
{
    (void)mosq;
    (void)userdata;

    char device_name[128];
    const char *tail;
    server_client_handle_mqtt_message(
        message->topic, message->payload, message->payloadlen);
    extract_device_name(message->topic, device_name, sizeof(device_name));
    tail = message->topic + strlen(base_topic);
    if (*tail == '/') tail++;
    if (strchr(tail, '/') == NULL && strcmp(device_name, "bridge") != 0) {
        handle_zigbee_data(device_name, message->topic, message->payload, message->payloadlen);
    }
}

int main(int argc, char **argv)
{
    int config_result = gateway_config_initialize(argc, argv);
    if (config_result != 0) return config_result > 0 ? EXIT_SUCCESS : EXIT_FAILURE;

    const char *host = get_env_or_default("MQTT_HOST", DEFAULT_MQTT_HOST);
    const char *username = getenv("MQTT_USER");
    const char *password = getenv("MQTT_PASSWORD");
    base_topic = get_env_or_default("Z2M_BASE_TOPIC", DEFAULT_BASE_TOPIC);
    int port = get_port_from_env();

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int rc = mosquitto_lib_init();
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Khoi tao libmosquitto that bai, ma loi=%d\n", rc);
        return EXIT_FAILURE;
    }

    struct mosquitto *mosq = mosquitto_new(NULL, true, NULL);
    if (mosq == NULL) {
        fprintf(stderr, "Khong tao duoc MQTT client\n");
        mosquitto_lib_cleanup();
        return EXIT_FAILURE;
    }

    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_disconnect_callback_set(mosq, on_disconnect);
    mosquitto_message_callback_set(mosq, on_message);
    mosquitto_reconnect_delay_set(mosq, 1, 30, true);

    if (username != NULL && username[0] != '\0') {
        rc = mosquitto_username_pw_set(mosq, username, password);
        if (rc != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "Cau hinh tai khoan MQTT that bai, ma loi=%d\n", rc);
            mosquitto_destroy(mosq);
            mosquitto_lib_cleanup();
            return EXIT_FAILURE;
        }
    }

    printf("Dang ket noi MQTT %s:%d...\n", host, port);
    rc = mosquitto_connect_async(mosq, host, port, KEEPALIVE_SECONDS);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Khong the bat dau ket noi MQTT, ma loi=%d\n", rc);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return EXIT_FAILURE;
    }

    rc = mosquitto_loop_start(mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Khong the chay MQTT loop, ma loi=%d\n", rc);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return EXIT_FAILURE;
    }

    if (!server_client_start(mosq, base_topic)) {
        fprintf(stderr, "Canh bao: khong khoi dong duoc ket noi HIS server\n");
    }

    while (running) {
        sleep_one_second();
    }

    mosquitto_disconnect(mosq);
    server_client_stop();
    mosquitto_loop_stop(mosq, false);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    puts("Da dung MQTT client.");
    return EXIT_SUCCESS;
}
