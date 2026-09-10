/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/modem/at/user_pipe.h>
#include <zephyr/modem/chat.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "at_control_service.h"

#define AT_CTRL_RX_BUF_SIZE 128
#define AT_CTRL_ARGV_SIZE 16
#define AT_CTRL_CMD_BUF_SIZE 96
#define AT_CTRL_EXPECTED_BUF_SIZE 16

#define AT_CTRL_CHAT_TIMEOUT_S 5
#define AT_CTRL_PIPE_CLAIM_TIMEOUT K_SECONDS(2)
#define AT_CTRL_SOFT_RESET_SETTLE_MS 2500

static struct k_mutex at_ctrl_lock;
static bool at_ctrl_init_done;

struct at_ctrl_ctx {
	struct modem_chat chat;
	uint8_t chat_rx_buf[AT_CTRL_RX_BUF_SIZE];
	uint8_t *chat_argv[AT_CTRL_ARGV_SIZE];

	struct modem_chat_script script;
	struct modem_chat_script_chat script_chat;
	struct modem_chat_match script_matches[2];
	char cmd_buf[AT_CTRL_CMD_BUF_SIZE];
	char expected_buf[AT_CTRL_EXPECTED_BUF_SIZE];
};

static struct at_ctrl_ctx at_ctrl_ctx;

MODEM_CHAT_MATCH_DEFINE(at_ctrl_ok_match, "OK", "", NULL);
MODEM_CHAT_MATCHES_DEFINE(at_ctrl_abort_matches,
			  MODEM_CHAT_MATCH("ERROR", "", NULL),
			  MODEM_CHAT_MATCH("+CME ERROR", "", NULL));

static void at_ctrl_print_any_match(struct modem_chat *chat, char **argv, uint16_t argc,
				    void *user_data)
{
	ARG_UNUSED(chat);
	ARG_UNUSED(user_data);

	if (argc == 2) {
		printk("[at] resp: %s\n", argv[1]);
	} else if (argc == 1) {
		printk("[at] resp: %s\n", argv[0]);
	}
}

static void at_ctrl_print_match(struct modem_chat *chat, char **argv, uint16_t argc,
				 void *user_data)
{
	ARG_UNUSED(chat);
	ARG_UNUSED(user_data);

	if (argc == 1) {
		printk("[at] resp: %s\n", argv[0]);
	}
}

static int at_control_service_init(void)
{
	struct modem_chat_config chat_cfg = {
		.user_data = NULL,
		.receive_buf = at_ctrl_ctx.chat_rx_buf,
		.receive_buf_size = sizeof(at_ctrl_ctx.chat_rx_buf),
		.delimiter = (const uint8_t *)"\r",
		.delimiter_size = 1,
		.filter = (const uint8_t *)"\n",
		.filter_size = 1,
		.argv = at_ctrl_ctx.chat_argv,
		.argv_size = ARRAY_SIZE(at_ctrl_ctx.chat_argv),
		.unsol_matches = modem_chat_empty_matches,
		.unsol_matches_size = 0,
	};

	if (at_ctrl_init_done) {
		return 0;
	}

	k_mutex_init(&at_ctrl_lock);
	modem_chat_init(&at_ctrl_ctx.chat, &chat_cfg);

	modem_chat_script_init(&at_ctrl_ctx.script);
	modem_chat_script_set_name(&at_ctrl_ctx.script, "app_at_ctrl_script");
	modem_chat_script_set_timeout(&at_ctrl_ctx.script, AT_CTRL_CHAT_TIMEOUT_S);
	modem_chat_script_set_abort_matches(&at_ctrl_ctx.script,
					    at_ctrl_abort_matches,
					    ARRAY_SIZE(at_ctrl_abort_matches));

	modem_chat_script_chat_init(&at_ctrl_ctx.script_chat);

	/* Print intermediate response lines and complete the script when expected final line is seen. */
	modem_chat_match_init(&at_ctrl_ctx.script_matches[0]);
	modem_chat_match_set_match(&at_ctrl_ctx.script_matches[0], "");
	modem_chat_match_set_separators(&at_ctrl_ctx.script_matches[0], "");
	modem_chat_match_set_callback(&at_ctrl_ctx.script_matches[0], at_ctrl_print_any_match);
	modem_chat_match_set_partial(&at_ctrl_ctx.script_matches[0], true);
	modem_chat_match_enable_wildcards(&at_ctrl_ctx.script_matches[0], false);

	modem_chat_match_init(&at_ctrl_ctx.script_matches[1]);
	modem_chat_match_set_match(&at_ctrl_ctx.script_matches[1], "OK");
	modem_chat_match_set_separators(&at_ctrl_ctx.script_matches[1], "");
	modem_chat_match_set_callback(&at_ctrl_ctx.script_matches[1], at_ctrl_print_match);
	modem_chat_match_set_partial(&at_ctrl_ctx.script_matches[1], false);
	modem_chat_match_enable_wildcards(&at_ctrl_ctx.script_matches[1], false);

	modem_chat_script_set_script_chats(&at_ctrl_ctx.script, &at_ctrl_ctx.script_chat, 1);

	at_ctrl_init_done = true;
	return 0;
}

int at_control_service_send(const char *cmd, bool wait_for_ok, uint16_t settle_ms)
{
	int ret;

	if (cmd == NULL || strlen(cmd) == 0) {
		return -EINVAL;
	}

	ret = at_control_service_init();
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&at_ctrl_lock, K_FOREVER);

	snprintk(at_ctrl_ctx.cmd_buf, sizeof(at_ctrl_ctx.cmd_buf), "%s", cmd);

	ret = modem_chat_script_chat_set_request(&at_ctrl_ctx.script_chat, at_ctrl_ctx.cmd_buf);
	if (ret < 0) {
		k_mutex_unlock(&at_ctrl_lock);
		return ret;
	}

	snprintk(at_ctrl_ctx.expected_buf, sizeof(at_ctrl_ctx.expected_buf), "%s", "OK");
	ret = modem_chat_match_set_match(&at_ctrl_ctx.script_matches[1], at_ctrl_ctx.expected_buf);
	if (ret < 0) {
		k_mutex_unlock(&at_ctrl_lock);
		return ret;
	}

	ret = modem_chat_script_chat_set_response_matches(&at_ctrl_ctx.script_chat,
					 at_ctrl_ctx.script_matches,
					 ARRAY_SIZE(at_ctrl_ctx.script_matches));
	if (ret < 0) {
		k_mutex_unlock(&at_ctrl_lock);
		return ret;
	}

	if (wait_for_ok) {
		modem_chat_script_chat_set_timeout(&at_ctrl_ctx.script_chat, 0);
	} else {
		modem_chat_script_chat_set_timeout(&at_ctrl_ctx.script_chat, settle_ms);
	}

	ret = modem_at_user_pipe_claim(&at_ctrl_ctx.chat, AT_CTRL_PIPE_CLAIM_TIMEOUT);
	if (ret < 0) {
		printk("[at] pipe claim failed: %d\n", ret);
		k_mutex_unlock(&at_ctrl_lock);
		return ret;
	}

	ret = modem_chat_run_script(&at_ctrl_ctx.chat, &at_ctrl_ctx.script);
	modem_at_user_pipe_release();
	k_mutex_unlock(&at_ctrl_lock);

	if (!wait_for_ok && ret == -ETIMEDOUT) {
		printk("[at] command settle timeout accepted: %s\n", cmd);
		return 0;
	}

	if (ret < 0) {
		printk("[at] command failed (%s): %d\n", cmd, ret);
		return ret;
	}

	printk("[at] command ok: %s\n", cmd);
	return 0;
}

int at_control_service_health_check(void)
{
	return at_control_service_send("AT", true, 0);
}

int at_control_service_query_registration(void)
{
	/* +CEREG response line is modem-specific; this call verifies modem command path health. */
	return at_control_service_send("AT+CEREG?", true, 0);
}

int at_control_service_soft_reset(void)
{
	/* AT+CFUN=1,1 may not return a clean final response before rebooting the modem. */
	return at_control_service_send("AT+CFUN=1,1", false, AT_CTRL_SOFT_RESET_SETTLE_MS);
}
