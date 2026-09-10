/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MQTT_TLS_CREDENTIALS_H_
#define MQTT_TLS_CREDENTIALS_H_

#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>

int mqtt_tls_credentials_configure(struct mqtt_sec_config *config);

#endif /* MQTT_TLS_CREDENTIALS_H_ */