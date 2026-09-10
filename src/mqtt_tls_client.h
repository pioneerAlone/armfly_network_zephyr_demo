/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MQTT_TLS_CLIENT_H_
#define MQTT_TLS_CLIENT_H_

#include <zephyr/net/net_if.h>

int mqtt_tls_client_run(struct net_if *iface);

#endif /* MQTT_TLS_CLIENT_H_ */
