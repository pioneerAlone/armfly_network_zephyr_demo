/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MODEM_DEVICE_H_
#define MODEM_DEVICE_H_

#include <zephyr/device.h>

const struct device *modem_device_get(void);
int modem_device_resume(const struct device *modem);
int modem_device_suspend(const struct device *modem);

#endif /* MODEM_DEVICE_H_ */
