/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "mqtt_tls_credentials.h"
#include "mqtt_tls_credentials_data.h"

#if defined(CONFIG_APP_MQTT_USE_TLS)
static sec_tag_t app_mqtt_tls_runtime_tags[2];
static uint32_t app_mqtt_tls_runtime_tag_count;

static int mqtt_tls_credentials_add_once(sec_tag_t tag, enum tls_credential_type type,
						 const unsigned char *data, size_t len)
{
	int ret;

	ret = tls_credential_add(tag, type, data, len);
	if (ret == -EEXIST) {
		return 0;
	}

	return ret;
}

static bool mqtt_tls_credentials_present(const unsigned char *data, size_t len)
{
	return data != NULL && len > 1;
}
#endif

int mqtt_tls_credentials_configure(struct mqtt_sec_config *config)
{
	int ret;

	if (config == NULL) {
		return -EINVAL;
	}

	config->cipher_list = NULL;
	config->hostname = CONFIG_APP_MQTT_HOSTNAME;

#if !defined(CONFIG_APP_MQTT_USE_TLS)
	config->peer_verify = TLS_PEER_VERIFY_NONE;
	config->sec_tag_list = NULL;
	config->sec_tag_count = 0;
	return 0;
#else
	config->peer_verify = IS_ENABLED(CONFIG_APP_MQTT_TLS_VERIFY_PEER)
				      ? TLS_PEER_VERIFY_REQUIRED
				      : TLS_PEER_VERIFY_NONE;
	app_mqtt_tls_runtime_tag_count = 0;

	if (IS_ENABLED(CONFIG_APP_MQTT_TLS_VERIFY_PEER) &&
	    !mqtt_tls_credentials_present(app_mqtt_ca_certificate,
					   sizeof(app_mqtt_ca_certificate))) {
		printk("[mqtt] broker CA certificate is required when peer verification is enabled\n");
		return -ENOENT;
	}

	if (mqtt_tls_credentials_present(app_mqtt_ca_certificate,
					 sizeof(app_mqtt_ca_certificate))) {
		ret = mqtt_tls_credentials_add_once(CONFIG_APP_MQTT_TLS_CA_SEC_TAG,
						   TLS_CREDENTIAL_CA_CERTIFICATE,
						   app_mqtt_ca_certificate,
						   sizeof(app_mqtt_ca_certificate));
		if (ret < 0) {
			printk("[mqtt] failed to add broker CA certificate: %d\n", ret);
			return ret;
		}

		app_mqtt_tls_runtime_tags[app_mqtt_tls_runtime_tag_count++] =
			CONFIG_APP_MQTT_TLS_CA_SEC_TAG;
	}

	if (IS_ENABLED(CONFIG_APP_MQTT_TLS_MUTUAL_AUTH)) {
		if (!mqtt_tls_credentials_present(app_mqtt_client_certificate,
						 sizeof(app_mqtt_client_certificate)) ||
		    !mqtt_tls_credentials_present(app_mqtt_private_key,
						 sizeof(app_mqtt_private_key))) {
			printk("[mqtt] mutual TLS requires both client certificate and private key\n");
			return -ENOENT;
		}

		ret = mqtt_tls_credentials_add_once(CONFIG_APP_MQTT_TLS_ID_SEC_TAG,
						   TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
						   app_mqtt_client_certificate,
						   sizeof(app_mqtt_client_certificate));
		if (ret < 0) {
			printk("[mqtt] failed to add client certificate: %d\n", ret);
			return ret;
		}

		ret = mqtt_tls_credentials_add_once(CONFIG_APP_MQTT_TLS_ID_SEC_TAG,
						   TLS_CREDENTIAL_PRIVATE_KEY,
						   app_mqtt_private_key,
						   sizeof(app_mqtt_private_key));
		if (ret < 0) {
			printk("[mqtt] failed to add client private key: %d\n", ret);
			return ret;
		}

		app_mqtt_tls_runtime_tags[app_mqtt_tls_runtime_tag_count++] =
			CONFIG_APP_MQTT_TLS_ID_SEC_TAG;
	}

	config->sec_tag_list = app_mqtt_tls_runtime_tag_count > 0U ? app_mqtt_tls_runtime_tags : NULL;
	config->sec_tag_count = app_mqtt_tls_runtime_tag_count;
	return 0;
#endif
}