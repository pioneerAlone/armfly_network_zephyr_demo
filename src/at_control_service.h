/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AT_CONTROL_SERVICE_H_
#define AT_CONTROL_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>

int at_control_service_health_check(void);
int at_control_service_query_registration(void);
int at_control_service_send(const char *cmd, bool wait_for_ok, uint16_t settle_ms);
int at_control_service_soft_reset(void);

#endif /* AT_CONTROL_SERVICE_H_ */
