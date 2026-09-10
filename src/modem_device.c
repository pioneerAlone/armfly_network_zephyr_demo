/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/printk.h>

#include "modem_device.h"

const struct device *modem_device_get(void)
{
	return DEVICE_DT_GET(DT_ALIAS(modem));
}

int modem_device_resume(const struct device *modem)
{
	enum pm_device_state state;
	int ret;

	if (!device_is_ready(modem)) {
		printk("modem device is not ready\n");
		return -ENODEV;
	}

	ret = pm_device_state_get(modem, &state);
	if ((ret == 0) && (state == PM_DEVICE_STATE_ACTIVE)) {
		printk("resume modem (already active)\n");
		return 0;
	}

	/* Resume kicks the modem_cellular state machine, which handles power-up,
	 * AT initialization, CMUX setup, and PPP carrier activation.
	 */
	printk("resume modem\n");
	ret = pm_device_action_run(modem, PM_DEVICE_ACTION_RESUME);
	if ((ret == -EALREADY) || (ret == -EBUSY)) {
		printk("resume modem in progress/already done\n");
		return 0;
	}

	return ret;
}

int modem_device_suspend(const struct device *modem)
{
	enum pm_device_state state;
	int ret;

	if (!device_is_ready(modem)) {
		printk("modem device is not ready\n");
		return -ENODEV;
	}

	ret = pm_device_state_get(modem, &state);
	if ((ret == 0) && ((state == PM_DEVICE_STATE_SUSPENDED) || (state == PM_DEVICE_STATE_OFF))) {
		printk("suspend modem (already suspended/off)\n");
		return 0;
	}

	printk("suspend modem\n");
	ret = pm_device_action_run(modem, PM_DEVICE_ACTION_SUSPEND);
	if ((ret == -EALREADY) || (ret == -EBUSY)) {
		printk("suspend modem in progress/already done\n");
		return 0;
	}

	return ret;
}
