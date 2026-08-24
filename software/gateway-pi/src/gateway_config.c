#define _CRT_SECURE_NO_WARNINGS
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "gateway_config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_CONFIG_PATH "gateway.conf"
#define MAX_CONFIG_LINE 1024

static const char *supported_keys[] = {
    "HIS_SERVER_HOST",
    "HIS_SERVER_PORT",
    "MQTT_HOST",
    "MQTT_PORT",
    "MQTT_USER",
    "MQTT_PASSWORD",
    "Z2M_BASE_TOPIC",
    "BED_ID",
    "BED_ROOM",
    "ZIGBEE_DEVICE_ID"
};

static char *trim(char *text)
{
    char *end;
    while (isspace((unsigned char)*text)) text++;
    if (*text == '\0') return text;
    end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return text;
}

static int set_environment(const char *name, const char *value, int overwrite)
{
    const char *existing = getenv(name);
    if (!overwrite && existing != NULL && existing[0] != '\0') return 0;
#ifdef _WIN32
    return _putenv_s(name, value);
#else
    return setenv(name, value, 1);
#endif
}

static int is_supported_key(const char *key)
{
    size_t index;
    for (index = 0; index < sizeof(supported_keys) / sizeof(supported_keys[0]); index++) {
        if (strcmp(key, supported_keys[index]) == 0) return 1;
    }
    return 0;
}

static int load_config_file(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[MAX_CONFIG_LINE];
    unsigned line_number = 0;

    if (file == NULL) {
        if (errno == ENOENT) {
            fprintf(stderr, "Canh bao: khong tim thay %s; dung cau hinh mac dinh.\n", path);
            return 0;
        }
        fprintf(stderr, "Khong doc duoc file cau hinh %s.\n", path);
        return -1;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *key;
        char *value;
        char *separator;
        size_t value_length;
        line_number++;
        key = trim(line);
        if (*key == '\0' || *key == '#' || *key == ';') continue;
        separator = strchr(key, '=');
        if (separator == NULL) {
            fprintf(stderr, "%s:%u thieu dau '='.\n", path, line_number);
            fclose(file);
            return -1;
        }
        *separator = '\0';
        key = trim(key);
        value = trim(separator + 1);
        value_length = strlen(value);
        if (value_length >= 2U
            && ((value[0] == '"' && value[value_length - 1U] == '"')
                || (value[0] == '\'' && value[value_length - 1U] == '\''))) {
            value[value_length - 1U] = '\0';
            value++;
        }
        if (!is_supported_key(key)) {
            fprintf(stderr, "%s:%u khoa khong hop le: %s\n", path, line_number, key);
            fclose(file);
            return -1;
        }
        if (set_environment(key, value, 0) != 0) {
            fprintf(stderr, "Khong ap dung duoc cau hinh %s.\n", key);
            fclose(file);
            return -1;
        }
    }

    if (ferror(file)) {
        fprintf(stderr, "Loi khi doc file cau hinh %s.\n", path);
        fclose(file);
        return -1;
    }
    fclose(file);
    printf("Da nap cau hinh: %s\n", path);
    return 0;
}

static void print_usage(const char *program)
{
    printf("Cach dung:\n");
    printf("  %s\n", program);
    printf("  %s <IP-server>\n", program);
    printf("  %s --server <IP-server> [--config <file>]\n", program);
}

int gateway_config_initialize(int argc, char **argv)
{
    const char *config_path = getenv("GATEWAY_CONFIG");
    const char *server_override = NULL;
    int index;

    if (config_path == NULL || config_path[0] == '\0') config_path = DEFAULT_CONFIG_PATH;

    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            print_usage(argv[0]);
            return 1;
        }
        if (strcmp(argv[index], "--config") == 0) {
            if (++index >= argc) {
                fprintf(stderr, "--config can mot duong dan.\n");
                return -1;
            }
            config_path = argv[index];
            continue;
        }
        if (strcmp(argv[index], "--server") == 0) {
            if (++index >= argc) {
                fprintf(stderr, "--server can mot dia chi IP hoac hostname.\n");
                return -1;
            }
            server_override = argv[index];
            continue;
        }
        if (argv[index][0] == '-') {
            fprintf(stderr, "Tuy chon khong hop le: %s\n", argv[index]);
            print_usage(argv[0]);
            return -1;
        }
        if (server_override != NULL) {
            fprintf(stderr, "Chi duoc nhap mot dia chi server.\n");
            return -1;
        }
        server_override = argv[index];
    }

    if (load_config_file(config_path) != 0) return -1;
    if (server_override != NULL && server_override[0] != '\0'
        && set_environment("HIS_SERVER_HOST", server_override, 1) != 0) {
        fprintf(stderr, "Khong ap dung duoc dia chi server: %s\n", server_override);
        return -1;
    }
    return 0;
}
