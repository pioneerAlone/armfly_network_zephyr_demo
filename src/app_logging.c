/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <time.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_output_custom.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/printk.h>

#include "app_logging.h"
#include "app_time.h"

#define APP_LOG_TIME_STR_LEN 24

LOG_MODULE_REGISTER(app_logging, CONFIG_LOG_DEFAULT_LEVEL);

static log_timestamp_t app_log_timestamp_get(void)
{
	struct timespec now = { 0 };

	if (sys_clock_gettime(SYS_CLOCK_REALTIME, &now) == 0) {
		return (log_timestamp_t)now.tv_sec;
	}

	return 0U;
}

static int app_log_custom_timestamp(const struct log_output *output,
					const log_timestamp_t timestamp,
					const log_timestamp_printer_t printer)
{
	time_t unix_time = (time_t)timestamp;
	struct tm *tm_utc = gmtime(&unix_time);

	if (tm_utc == NULL) {
		return printer(output, "unsynced");
	}

	return printer(output, "%04d-%02d-%02dT%02d:%02d:%02dZ",
		      tm_utc->tm_year + 1900,
		      tm_utc->tm_mon + 1,
		      tm_utc->tm_mday,
		      tm_utc->tm_hour,
		      tm_utc->tm_min,
		      tm_utc->tm_sec);
}

void app_logger_init(void)
{
	if (log_set_timestamp_func(app_log_timestamp_get, 1000U) < 0) {
		LOG_WRN("logger timestamp hook init failed");
	}

	if (IS_ENABLED(CONFIG_LOG_OUTPUT_FORMAT_CUSTOM_TIMESTAMP)) {
		log_custom_timestamp_set(app_log_custom_timestamp);
	}
}

void app_format_realtime(char *buf, size_t len)
{
	app_time_format_utc(buf, len);
}

void app_logf(const char *fmt, ...)
{
	char ts[APP_LOG_TIME_STR_LEN];
	char msg[192];
	va_list args;

	app_format_realtime(ts, sizeof(ts));
	va_start(args, fmt);
	vsnprintk(msg, sizeof(msg), fmt, args);
	va_end(args);
	printk("[app][%s] %s", ts, msg);
}

void app_log_phase(const char *name, const char *detail)
{
	LOG_INF("phase[%s]: %s", name, detail);
}
