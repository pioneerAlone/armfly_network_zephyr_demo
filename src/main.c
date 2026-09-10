/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app_startup.h"

int main(void)
{
	printk("[app] stm32_modem_demo boot, console alive\n");

	while (1) {
		int ret = app_startup_run();

		if (ret == -EAGAIN) {
			continue;
		}

		if (ret == 0) {
			break;
		}

		/* Validation often fails for transient reasons during hardware bring-up.
		 * Retry after a short delay so a power-cycle or SIM/network change does not
		 * require a full MCU reset.
		 */
		printk("[app] modem startup failed: %d, retrying\n", ret);
		k_sleep(K_SECONDS(10));
	}

	while (1) {
		k_sleep(K_SECONDS(5));
	}
}
