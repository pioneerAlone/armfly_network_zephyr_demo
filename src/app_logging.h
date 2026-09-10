/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_LOGGING_H_
#define APP_LOGGING_H_

#include <stddef.h>

void app_format_realtime(char *buf, size_t len);
void app_logf(const char *fmt, ...);
void app_log_phase(const char *name, const char *detail);
void app_logger_init(void);

#define APP_LOG_PHASE(name, detail) LOG_INF("phase[%s]: %s", name, detail)
#define APP_LOG_INFO(...) LOG_INF(__VA_ARGS__)
#define APP_LOG_WARN(...) LOG_WRN(__VA_ARGS__)
#define APP_LOG_ERR(...) LOG_ERR(__VA_ARGS__)

#endif /* APP_LOGGING_H_ */
