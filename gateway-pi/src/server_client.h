#ifndef SERVER_CLIENT_H
#define SERVER_CLIENT_H

#include <stdbool.h>

struct mosquitto;

bool server_client_start(struct mosquitto *mqtt, const char *mqtt_base_topic);
void server_client_stop(void);
void server_client_send_reading(
    const char *device_id,
    const void *payload,
    int payload_length);
void server_client_handle_mqtt_message(
    const char *topic,
    const void *payload,
    int payload_length);

#endif
