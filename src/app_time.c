/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/net/sntp.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/util.h>

#include "app_logging.h"
#include "app_time.h"

#define APP_TIME_SYNC_MIN_VALID_UNIX 1700000000LL
#define APP_SNTP_QUERY_TIMEOUT_MS 4000
#define APP_SNTP_RETRY_COUNT 3
#define APP_SNTP_RETRY_DELAY_MS 1200
#define APP_LOG_TIME_STR_LEN 24

static const char *const app_sntp_servers[] = {
	"ntp.aliyun.com",
	"cn.pool.ntp.org",
	"0.pool.ntp.org",
};

#define APP_SNTP_SERVER_COUNT ARRAY_SIZE(app_sntp_servers)

static const char *app_sntp_error_name(int err)
{
	switch (err) {
	case -ETIMEDOUT:
		return "ETIMEDOUT";
	case -ENETUNREACH:
		return "ENETUNREACH";
	case -ENETDOWN:
		return "ENETDOWN";
	case -EHOSTUNREACH:
		return "EHOSTUNREACH";
	default:
		return "UNKNOWN";
	}
}

static const char *app_sntp_error_hint(int err)
{
	switch (err) {
	case -ETIMEDOUT:
		return "NTP response timeout";
	case -ENETUNREACH:
		return "network route unavailable";
	case -ENETDOWN:
		return "network link not ready";
	case -EHOSTUNREACH:
		return "NTP host unreachable";
	default:
		return "refer to raw errno/status";
	}
}

void app_time_format_utc(char *buf, size_t len)
{
	struct timespec now = { 0 };
	struct tm *tm_utc;

	if (buf == NULL || len == 0U) {
		return;
	}

	if (sys_clock_gettime(SYS_CLOCK_REALTIME, &now) < 0 ||
	    now.tv_sec < APP_TIME_SYNC_MIN_VALID_UNIX) {
		snprintk(buf, len, "unsynced");
		return;
	}

	tm_utc = gmtime(&now.tv_sec);
	if (tm_utc == NULL) {
		snprintk(buf, len, "unsynced");
		return;
	}

	snprintk(buf, len, "%04d-%02d-%02dT%02d:%02d:%02dZ",
		 tm_utc->tm_year + 1900,
		 tm_utc->tm_mon + 1,
		 tm_utc->tm_mday,
		 tm_utc->tm_hour,
		 tm_utc->tm_min,
		 tm_utc->tm_sec);
}

int app_time_sync_for_tls(void)
{
	struct timespec now = { 0 };
	struct sntp_time ts = { 0 };
	char ts_text[APP_LOG_TIME_STR_LEN];
	struct timespec synced;
	int attempt;
	int ret;

	if (!IS_ENABLED(CONFIG_MBEDTLS_HAVE_TIME_DATE) ||
	    !IS_ENABLED(CONFIG_APP_MQTT_USE_TLS) ||
	    !IS_ENABLED(CONFIG_APP_MQTT_TLS_VERIFY_PEER)) {
		return 0;
	}

	ret = sys_clock_gettime(SYS_CLOCK_REALTIME, &now);
	if (ret == 0 && now.tv_sec >= APP_TIME_SYNC_MIN_VALID_UNIX) {
		return 0;
	}

	for (attempt = 0; attempt < APP_SNTP_RETRY_COUNT; attempt++) {
		const char *server = app_sntp_servers[attempt % APP_SNTP_SERVER_COUNT];

		ret = sntp_simple(server, APP_SNTP_QUERY_TIMEOUT_MS, &ts);
		if (ret != 0) {
			app_logf("SNTP sync attempt %d/%d failed on %s: %d (%s: %s)\n",
			       attempt + 1, APP_SNTP_RETRY_COUNT, server, ret,
			       app_sntp_error_name(ret), app_sntp_error_hint(ret));
			k_msleep(APP_SNTP_RETRY_DELAY_MS);
			continue;
		}

		synced.tv_sec = (time_t)ts.seconds;
		synced.tv_nsec = (long)(((uint64_t)ts.fraction * 1000000000ULL) >> 32);
		ret = sys_clock_settime(SYS_CLOCK_REALTIME, &synced);
		if (ret < 0) {
			app_logf("failed to set realtime clock after SNTP: %d\n", ret);
			return ret;
		}

		app_time_format_utc(ts_text, sizeof(ts_text));
		app_logf("realtime clock synced via SNTP server=%s (%llu, %s)\n",
		       server, (unsigned long long)ts.seconds, ts_text);
		return 0;
	}

	return ret;
}
