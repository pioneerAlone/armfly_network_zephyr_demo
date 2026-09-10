/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_TIME_H_
#define APP_TIME_H_

#include <stddef.h>

void app_time_format_utc(char *buf, size_t len);
int app_time_sync_for_tls(void);

#endif /* APP_TIME_H_ */
