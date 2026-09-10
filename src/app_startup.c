/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/modem/modem_cellular.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/sntp.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/util.h>

#include "app_logging.h"
#include "app_startup.h"
#include "app_time.h"
#include "at_control_service.h"
#include "mqtt_tls_client.h"
#include "modem_device.h"
#include "ppp_session.h"

LOG_MODULE_REGISTER(app_startup, CONFIG_LOG_DEFAULT_LEVEL);

static uint8_t app_consecutive_failures;

#define APP_LIFECYCLE_RETRY_STEP_MS 200
#define APP_LIFECYCLE_RETRY_TIMEOUT_MS 10000
#define APP_SNTP_QUERY_TIMEOUT_MS 4000
#define APP_SNTP_RETRY_COUNT 3
#define APP_SNTP_RETRY_DELAY_MS 1200

static int app_sync_time_for_tls(void)
{
	return app_time_sync_for_tls();
}

static int app_bring_down_ppp_with_retry(struct net_if *iface)
{
	int ret;
	int elapsed_ms = 0;

	while (elapsed_ms <= APP_LIFECYCLE_RETRY_TIMEOUT_MS) {
		ret = ppp_session_bring_down(iface);
		if (ret == 0) {
			return 0;
		}
		if (ret != -EAGAIN) {
			return ret;
		}

		k_msleep(APP_LIFECYCLE_RETRY_STEP_MS);
		elapsed_ms += APP_LIFECYCLE_RETRY_STEP_MS;
	}

	return -EAGAIN;
}

static int app_suspend_modem_with_retry(const struct device *modem)
{
	int ret;
	int elapsed_ms = 0;

	while (elapsed_ms <= APP_LIFECYCLE_RETRY_TIMEOUT_MS) {
		ret = modem_device_suspend(modem);
		if (ret == 0) {
			return 0;
		}
		if (ret != -EAGAIN) {
			return ret;
		}

		k_msleep(APP_LIFECYCLE_RETRY_STEP_MS);
		elapsed_ms += APP_LIFECYCLE_RETRY_STEP_MS;
	}

	return -EAGAIN;
}

static void app_resume_periodic_script_if_needed(const struct device *modem,
							 bool *periodic_paused)
{
	int ret;

	if (!periodic_paused || !*periodic_paused) {
		return;
	}

	ret = cellular_modem_resume_periodic_script(modem);
	*periodic_paused = false;
	if (ret < 0 && ret != -EINVAL && ret != -ENOTSUP) {
		app_logf("failed to resume periodic script: %d\n", ret);
	}
}

static int app_pause_periodic_script_if_possible(const struct device *modem)
{
	int ret;

	ret = cellular_modem_pause_periodic_script(modem);
	if (ret == 0 || ret == -EINVAL) {
		return 1;
	}
	if (ret != -ENOTSUP) {
		app_logf("failed to pause periodic script: %d\n", ret);
	}

	return 0;
}

int app_startup_run(void)
{
	const struct device *modem = modem_device_get();
	struct net_if *iface = ppp_session_get_iface();
	bool modem_resumed = false;
	bool ppp_brought_up = false;
	bool soft_reset_requested = false;
	bool periodic_paused = false;
	bool mqtt_stage_reached = false;
	int ret;

	/*
	 * The PPP interface is created by the modem PPP backend. If it is missing,
	 * no amount of modem bring-up will make the validation path succeed.
	 */
	if (iface == NULL) {
		LOG_ERR("PPP interface not found");
		return -ENODEV;
	}

	app_logger_init();
	LOG_INF("logger timestamp source initialized using SYS_CLOCK_REALTIME");
	APP_LOG_PHASE("modem", "resume modem and prepare UART/CMUX backend");
	ret = modem_device_resume(modem);
	if (ret < 0) {
		LOG_ERR("failed to resume modem: %d", ret);
		return ret;
	}
	modem_resumed = true;

	/*
	 * Bringing the network interface up allows the modem driver to complete the
	 * CMUX + PPP path and then obtain an IPv4 address asynchronously.
	 */
	APP_LOG_PHASE("network", "bring up PPP interface");
	ret = ppp_session_bring_up(iface);
	if (ret < 0) {
		LOG_ERR("failed to bring up PPP interface: %d", ret);
		goto fail;
	}
	ppp_brought_up = true;

	ret = ppp_session_wait_connected(iface, K_SECONDS(120));
	if (ret < 0) {
		LOG_ERR("PPP phase not running in time: %d", ret);
		goto fail;
	}

	ret = ppp_session_wait_ipv4_ready(iface, K_SECONDS(30));
	if (ret < 0) {
		LOG_ERR("PPP IPv4 not ready in time: %d", ret);
		goto fail;
	}

	APP_LOG_PHASE("time", "synchronize realtime clock for TLS certificate validation");
	ret = app_sync_time_for_tls();
	if (ret < 0) {
		LOG_ERR("SNTP clock sync failed, abort TLS validation path: %d", ret);
		goto fail;
	}

	APP_LOG_PHASE("network", "PPP data path is up");
	LOG_INF("PPP connected, modem data path is up");

	/*
	 * Validate real data connectivity from STM32 side.
	 * ICMP may be blocked on some operator networks, so a failure here is still
	 * useful as a visibility signal, but not always a modem/PPP defect.
	 */
	APP_LOG_PHASE("network", "validate PPP connectivity to the public internet");
	ret = ppp_session_ping_ipv4(iface, "8.8.8.8", K_SECONDS(8));
	if (ret < 0) {
		LOG_WRN("PPP ping check failed: %d, continue validation with AT/MQTT", ret);
	}

	if (ret == 0) {
		LOG_INF("PPP connectivity check passed");
	}

	APP_LOG_PHASE("modem", "run AT health and registration checks");
	ret = at_control_service_health_check();
	if (ret < 0) {
		LOG_ERR("AT health check failed: %d", ret);
		goto fail;
	}

	ret = at_control_service_query_registration();
	if (ret < 0) {
		LOG_ERR("AT registration query failed: %d", ret);
		goto fail;
	}

	periodic_paused = app_pause_periodic_script_if_possible(modem);

	APP_LOG_PHASE("mqtt", "establish MQTT session over TLS");
	mqtt_stage_reached = true;
	ret = mqtt_tls_client_run(iface);
	if (ret < 0) {
		LOG_ERR("MQTT over TLS demo failed: %d", ret);
		goto fail;
	}

	app_resume_periodic_script_if_needed(modem, &periodic_paused);

	/*
	 * Demonstrate one full PPP over UART lifecycle:
	 * first release PPP/CMUX cleanly, then suspend modem and restart.
	 * The modem driver's shutdown path handles vendor-specific power-off/reset sequence.
	 */
	APP_LOG_PHASE("lifecycle", "complete validation and release modem resources");
	if (ppp_brought_up) {
		int down_ret = app_bring_down_ppp_with_retry(iface);

		if (down_ret < 0) {
			LOG_ERR("failed to bring down PPP interface: %d", down_ret);
		}
	}

	if (modem_resumed) {
		int suspend_ret = app_suspend_modem_with_retry(modem);

		if (suspend_ret < 0) {
			LOG_ERR("failed to suspend modem after success path: %d", suspend_ret);
		}
	}

	k_sleep(K_SECONDS(10));
	LOG_INF("MQTT over TLS demo passed");
	LOG_INF("lifecycle cycle complete, restarting next loop");
	app_consecutive_failures = 0;
	return -EAGAIN;

fail:
	app_resume_periodic_script_if_needed(modem, &periodic_paused);

	if (app_consecutive_failures < UINT8_MAX) {
		app_consecutive_failures++;
	}

	if (mqtt_stage_reached) {
		LOG_WRN("MQTT stage failed, keep modem managed by driver recovery");
		return ret;
	}

	if (ppp_brought_up) {
		(void)ppp_session_bring_down(iface);
	}

	if (IS_ENABLED(CONFIG_APP_MODEM_SOFT_RESET_ON_RECOVERY) &&
	    app_consecutive_failures >= CONFIG_APP_MODEM_SOFT_RESET_THRESHOLD) {
		LOG_WRN("failure threshold reached, requesting AT+CFUN=1,1 soft reset");
		soft_reset_requested = true;
		app_consecutive_failures = 0;
	}

	if (soft_reset_requested) {
		int soft_reset_ret = at_control_service_soft_reset();

		if (soft_reset_ret < 0) {
			LOG_ERR("AT+CFUN=1,1 request failed: %d, fallback to modem suspend",
				soft_reset_ret);
			if (modem_resumed) {
				(void)modem_device_suspend(modem);
			}
		} else {
			LOG_INF("AT+CFUN=1,1 accepted, waiting for modem reboot");
			k_sleep(K_SECONDS(3));
		}

		return ret;
	}

	if (modem_resumed) {
		(void)modem_device_suspend(modem);
	}

	return ret;
}
