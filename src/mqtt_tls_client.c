/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "mqtt_tls_credentials.h"
#include "mqtt_tls_client.h"

#define APP_MQTT_DNS_RETRY_COUNT 5
#define APP_MQTT_DNS_RETRY_DELAY_MS 1500

struct mqtt_demo_ctx {
	struct mqtt_client client;
	struct sockaddr_storage broker;
	struct zsock_pollfd fds[1];
	uint8_t rx_buf[CONFIG_APP_MQTT_RX_BUF_SIZE];
	uint8_t tx_buf[CONFIG_APP_MQTT_TX_BUF_SIZE];
	char client_id[48];
	char if_name[CONFIG_NET_INTERFACE_NAME_LEN + 1];
	bool connected;
	bool sub_acked;
	int conn_result;
};

static void mqtt_log_tls_evidence(struct mqtt_demo_ctx *ctx)
{
	int sock;
	int ciphersuite = 0;
	net_socklen_t optlen = sizeof(ciphersuite);
	int ret;

	if (ctx->client.transport.type != MQTT_TRANSPORT_SECURE) {
		printk("[mqtt] transport: non-secure\n");
		return;
	}

	sock = ctx->client.transport.tls.sock;
	printk("[mqtt] transport: TLS (sock=%d)\n", sock);

	ret = zsock_getsockopt(sock, ZSOCK_SOL_TLS, ZSOCK_TLS_CIPHERSUITE_USED,
				      &ciphersuite, &optlen);
	if (ret == 0) {
		printk("[mqtt] TLS ciphersuite used: 0x%04x\n", (unsigned int)ciphersuite);
	} else {
		printk("[mqtt] TLS ciphersuite read failed: %d (errno=%d)\n", ret, errno);
	}
}

static void mqtt_prepare_fds(struct mqtt_demo_ctx *ctx)
{
	if (ctx->client.transport.type == MQTT_TRANSPORT_NON_SECURE) {
		ctx->fds[0].fd = ctx->client.transport.tcp.sock;
	} else {
		ctx->fds[0].fd = ctx->client.transport.tls.sock;
	}

	ctx->fds[0].events = ZSOCK_POLLIN;
}

static int mqtt_wait_and_input(struct mqtt_demo_ctx *ctx, int32_t timeout_ms)
{
	int ret;

	ret = zsock_poll(ctx->fds, ARRAY_SIZE(ctx->fds), timeout_ms);
	if (ret < 0) {
		printk("[mqtt] poll failed: %d\n", errno);
		return -errno;
	}

	if (ret > 0 && (ctx->fds[0].revents & ZSOCK_POLLIN)) {
		ret = mqtt_input(&ctx->client);
		if (ret < 0) {
			printk("[mqtt] mqtt_input failed: %d\n", ret);
			return ret;
		}
	}

	ret = mqtt_live(&ctx->client);
	if (ret < 0 && ret != -EAGAIN) {
		printk("[mqtt] mqtt_live failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static void mqtt_event_handler(struct mqtt_client *const client, const struct mqtt_evt *evt)
{
	struct mqtt_demo_ctx *ctx = client->user_data;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		ctx->conn_result = (evt->result == 0) ? 0 : -EIO;
		if (evt->result == 0) {
			ctx->connected = true;
			printk("[mqtt] CONNACK ok\n");
		} else {
			printk("[mqtt] CONNACK failed: %d\n", evt->result);
		}
		break;

	case MQTT_EVT_DISCONNECT:
		ctx->connected = false;
		printk("[mqtt] disconnected: %d\n", evt->result);
		break;

	case MQTT_EVT_SUBACK:
		if (evt->result != MQTT_SUBACK_FAILURE) {
			ctx->sub_acked = true;
			printk("[mqtt] SUBACK message_id=%u\n", evt->param.suback.message_id);
		} else {
			printk("[mqtt] SUBACK failed: %d\n", evt->result);
		}
		break;

	case MQTT_EVT_PUBLISH: {
		const struct mqtt_publish_param *p = &evt->param.publish;
		char payload[CONFIG_APP_MQTT_PAYLOAD_BUF_SIZE];
		int bytes_read;
		size_t copy_len;

		bytes_read = mqtt_read_publish_payload(client, payload, sizeof(payload) - 1);
		if (bytes_read < 0) {
			printk("[mqtt] payload read failed: %d\n", bytes_read);
			break;
		}

		copy_len = (size_t)bytes_read;
		payload[copy_len] = '\0';

		printk("[mqtt] RX topic='%.*s' payload='%s'\n",
		       p->message.topic.topic.size,
		       p->message.topic.topic.utf8,
		       payload);

		if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
			const struct mqtt_puback_param ack = {
				.message_id = p->message_id,
			};

			(void)mqtt_publish_qos1_ack(client, &ack);
		}
		break;
	}

	case MQTT_EVT_PINGRESP:
		printk("[mqtt] PINGRESP\n");
		break;

	default:
		break;
	}
}

static int mqtt_resolve_broker(struct sockaddr_storage *broker)
{
	struct zsock_addrinfo *result;
	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	int ret = DNS_EAI_FAIL;
	int attempt;

	for (attempt = 1; attempt <= APP_MQTT_DNS_RETRY_COUNT; attempt++) {
		result = NULL;
		errno = 0;

		ret = zsock_getaddrinfo(CONFIG_APP_MQTT_HOSTNAME, CONFIG_APP_MQTT_PORT,
					    &hints, &result);
		if (ret == 0) {
			break;
		}

		printk("[mqtt] DNS resolve attempt %d/%d failed: %d (%s), errno=%d\n",
		       attempt,
		       APP_MQTT_DNS_RETRY_COUNT,
		       ret,
		       zsock_gai_strerror(ret),
		       errno);

		if (ret != DNS_EAI_AGAIN && ret != DNS_EAI_SYSTEM && ret != DNS_EAI_CANCELED) {
			return (ret == DNS_EAI_NONAME || ret == DNS_EAI_NODATA) ? -ENOENT : -EHOSTUNREACH;
		}

		if (attempt < APP_MQTT_DNS_RETRY_COUNT) {
			k_msleep(APP_MQTT_DNS_RETRY_DELAY_MS);
		}
	}

	if (ret != 0) {
		return -EHOSTUNREACH;
	}

	if (result == NULL || result->ai_addr == NULL) {
		if (result != NULL) {
			zsock_freeaddrinfo(result);
		}
		return -ENOENT;
	}

	memset(broker, 0, sizeof(*broker));
	memcpy(broker, result->ai_addr,
	       MIN((size_t)result->ai_addrlen, sizeof(*broker)));
	zsock_freeaddrinfo(result);

	return 0;
}

static int mqtt_client_setup(struct mqtt_demo_ctx *ctx, struct net_if *iface)
{
	int ret;

	mqtt_client_init(&ctx->client);

	snprintk(ctx->client_id, sizeof(ctx->client_id), "%s-%u",
		 CONFIG_APP_MQTT_CLIENT_ID_PREFIX,
		 (uint32_t)k_uptime_get_32());

	ret = net_if_get_name(iface, ctx->if_name, sizeof(ctx->if_name));
	if (ret < 0) {
		ctx->if_name[0] = '\0';
	}

	ctx->client.broker = &ctx->broker;
	ctx->client.evt_cb = mqtt_event_handler;
	ctx->client.user_data = ctx;
	ctx->client.client_id.utf8 = (uint8_t *)ctx->client_id;
	ctx->client.client_id.size = strlen(ctx->client_id);
	ctx->client.password = NULL;
	ctx->client.user_name = NULL;
	ctx->client.protocol_version = MQTT_VERSION_3_1_1;

	ctx->client.rx_buf = ctx->rx_buf;
	ctx->client.rx_buf_size = sizeof(ctx->rx_buf);
	ctx->client.tx_buf = ctx->tx_buf;
	ctx->client.tx_buf_size = sizeof(ctx->tx_buf);
	ctx->client.transport.if_name = (ctx->if_name[0] != '\0') ? ctx->if_name : NULL;

	if (IS_ENABLED(CONFIG_APP_MQTT_USE_TLS)) {
		ctx->client.transport.type = MQTT_TRANSPORT_SECURE;
		ret = mqtt_tls_credentials_configure(&ctx->client.transport.tls.config);
		if (ret < 0) {
			return ret;
		}
	} else {
		ctx->client.transport.type = MQTT_TRANSPORT_NON_SECURE;
	}

	return 0;
}

static int mqtt_wait_connack(struct mqtt_demo_ctx *ctx, k_timeout_t timeout)
{
	int64_t remaining_ms = k_ticks_to_ms_floor64(timeout.ticks);

	while (remaining_ms > 0) {
		int32_t step_ms = (remaining_ms > 1000) ? 1000 : (int32_t)remaining_ms;
		int ret;

		ret = mqtt_wait_and_input(ctx, step_ms);
		if (ret < 0) {
			return ret;
		}

		if (ctx->connected) {
			return 0;
		}

		if (ctx->conn_result != 0) {
			return ctx->conn_result;
		}

		remaining_ms -= step_ms;
	}

	return -ETIMEDOUT;
}

static int mqtt_subscribe_topic(struct mqtt_demo_ctx *ctx)
{
	struct mqtt_topic topic = {
		.topic = {
			.utf8 = (uint8_t *)CONFIG_APP_MQTT_TOPIC,
			.size = strlen(CONFIG_APP_MQTT_TOPIC),
		},
		.qos = MQTT_QOS_0_AT_MOST_ONCE,
	};
	struct mqtt_subscription_list sub_list = {
		.list = &topic,
		.list_count = 1,
		.message_id = 1,
	};

	ctx->sub_acked = false;
	return mqtt_subscribe(&ctx->client, &sub_list);
}

static int mqtt_wait_suback(struct mqtt_demo_ctx *ctx, k_timeout_t timeout)
{
	int64_t remaining_ms = k_ticks_to_ms_floor64(timeout.ticks);

	while (remaining_ms > 0) {
		int32_t step_ms = (remaining_ms > 1000) ? 1000 : (int32_t)remaining_ms;
		int ret;

		ret = mqtt_wait_and_input(ctx, step_ms);
		if (ret < 0) {
			return ret;
		}

		if (ctx->sub_acked) {
			return 0;
		}

		remaining_ms -= step_ms;
	}

	return -ETIMEDOUT;
}

static int mqtt_pump_for_ms(struct mqtt_demo_ctx *ctx, int32_t duration_ms)
{
	int32_t remaining_ms = duration_ms;

	while (remaining_ms > 0 && ctx->connected) {
		int32_t step_ms = (remaining_ms > 1000) ? 1000 : remaining_ms;
		int ret;

		ret = mqtt_wait_and_input(ctx, step_ms);
		if (ret < 0) {
			return ret;
		}

		remaining_ms -= step_ms;
	}

	return ctx->connected ? 0 : -ENOTCONN;
}

static int mqtt_publish_once(struct mqtt_demo_ctx *ctx, uint8_t publish_index)
{
	char payload[CONFIG_APP_MQTT_PAYLOAD_BUF_SIZE];
	struct mqtt_publish_param msg = { 0 };
	uint16_t msg_id = (uint16_t)(k_uptime_get_32() & 0xFFFFu);
	int len;

	len = snprintk(payload, sizeof(payload),
		       "{\"board\":\"armfly_stm32_v6\",\"seq\":%u,\"uptime_ms\":%u}",
		       publish_index,
		       (uint32_t)k_uptime_get_32());
	if (len < 0 || len >= sizeof(payload)) {
		return -ENOMEM;
	}

	msg.message.topic.topic.utf8 = (uint8_t *)CONFIG_APP_MQTT_TOPIC;
	msg.message.topic.topic.size = strlen(CONFIG_APP_MQTT_TOPIC);
	msg.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
	msg.message.payload.data = payload;
	msg.message.payload.len = (size_t)len;
	msg.message_id = msg_id;
	msg.dup_flag = 0U;
	/* Demo mode: retain the latest payload so late subscribers can still receive it. */
	msg.retain_flag = 1U;

	printk("[mqtt] TX topic='%s' payload='%s'\n", CONFIG_APP_MQTT_TOPIC, payload);
	return mqtt_publish(&ctx->client, &msg);
}

int mqtt_tls_client_run(struct net_if *iface)
{
	static struct mqtt_demo_ctx ctx;
	int32_t publish_interval_ms;
	uint8_t publish_count;
	int ret;
	uint8_t publish_index;

	if (iface == NULL) {
		return -ENODEV;
	}

	memset(&ctx, 0, sizeof(ctx));

	ret = mqtt_resolve_broker(&ctx.broker);
	if (ret < 0) {
		printk("[mqtt] broker resolve failed: %d\n", ret);
		return ret;
	}

	ret = mqtt_client_setup(&ctx, iface);
	if (ret < 0) {
		printk("[mqtt] client setup failed: %d\n", ret);
		return ret;
	}

	ret = mqtt_connect(&ctx.client);
	if (ret < 0) {
		printk("[mqtt] mqtt_connect failed: %d\n", ret);
		return ret;
	}

	if (IS_ENABLED(CONFIG_APP_MQTT_USE_TLS)) {
		mqtt_log_tls_evidence(&ctx);
	}

	mqtt_prepare_fds(&ctx);

	ret = mqtt_wait_connack(&ctx, K_SECONDS(20));
	if (ret < 0) {
		printk("[mqtt] wait CONNACK failed: %d\n", ret);
		(void)mqtt_abort(&ctx.client);
		return ret;
	}

	ret = mqtt_subscribe_topic(&ctx);
	if (ret < 0) {
		printk("[mqtt] subscribe failed: %d\n", ret);
		(void)mqtt_disconnect(&ctx.client, NULL);
		return ret;
	}

	ret = mqtt_wait_suback(&ctx, K_SECONDS(10));
	if (ret < 0) {
		printk("[mqtt] wait SUBACK failed: %d\n", ret);
		(void)mqtt_disconnect(&ctx.client, NULL);
		return ret;
	}

	publish_count = (uint8_t)CONFIG_APP_MQTT_PUBLISH_COUNT;
	publish_interval_ms = (CONFIG_APP_MQTT_LOOP_SECONDS * 1000) / publish_count;
	if (publish_interval_ms <= 0) {
		publish_interval_ms = 1000;
	}

	for (publish_index = 1; publish_index <= publish_count && ctx.connected; publish_index++) {
		ret = mqtt_publish_once(&ctx, publish_index);
		if (ret < 0) {
			printk("[mqtt] publish failed: %d\n", ret);
			(void)mqtt_disconnect(&ctx.client, NULL);
			return ret;
		}

		if (publish_index < publish_count) {
			ret = mqtt_pump_for_ms(&ctx, publish_interval_ms);
			if (ret < 0) {
				printk("[mqtt] session pump failed: %d\n", ret);
				break;
			}
		}
	}

	if (ret >= 0 && ctx.connected) {
		ret = mqtt_pump_for_ms(&ctx, 1000);
		if (ret == -ENOTCONN) {
			ret = 0;
		}
	}

	if (ctx.connected) {
		ret = mqtt_disconnect(&ctx.client, NULL);
		if (ret < 0) {
			printk("[mqtt] disconnect failed: %d\n", ret);
		}
	}

	if (!ctx.sub_acked) {
		printk("[mqtt] warning: no SUBACK observed during demo window\n");
	}

	if (ret < 0) {
		return ret;
	}

	printk("[mqtt] demo finished\n");
	return 0;
}
