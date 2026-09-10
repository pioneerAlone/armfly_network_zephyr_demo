/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include <zephyr/net/icmp.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/ppp.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>

#include "ppp_session.h"

struct net_if *ppp_session_get_iface(void)
{
	/* The cellular modem backend registers a PPP L2 interface that becomes the
	 * verification surface for end-to-end UART -> CMUX -> PPP bring-up.
	 */
	return net_if_get_first_by_type(&NET_L2_GET_NAME(PPP));
}

int ppp_session_bring_up(struct net_if *iface)
{
	int ret;

	if (iface == NULL) {
		printk("PPP interface not found\n");
		return -ENODEV;
	}

	printk("bring up PPP interface\n");
	ret = net_if_up(iface);
	if (ret == -EALREADY) {
		printk("PPP interface already up\n");
		return 0;
	}

	return ret;
}

int ppp_session_bring_down(struct net_if *iface)
{
	int ret;

	if (iface == NULL) {
		printk("PPP interface not found\n");
		return -ENODEV;
	}

	printk("bring down PPP interface\n");
	ret = net_if_down(iface);
	if (ret == -EALREADY) {
		printk("PPP interface already down\n");
		return 0;
	}

	return ret;
}

int ppp_session_wait_connected(struct net_if *iface, k_timeout_t timeout)
{
	struct ppp_context *ctx;
	int ret;

	if (iface == NULL) {
		printk("PPP interface not found\n");
		return -ENODEV;
	}

	ctx = (struct ppp_context *)net_if_l2_data(iface);
	if (ctx == NULL) {
		printk("PPP context not found\n");
		return -ENODEV;
	}

	printk("wait for PPP phase running\n");

	/* Check if PPP is already running (avoid race condition) */
	if (ctx->phase == PPP_RUNNING) {
		printk("PPP already in running phase\n");
		return 0;
	}

	/* PPP not running yet, wait for the event */
	ret = net_mgmt_event_wait_on_iface(iface, NET_EVENT_PPP_PHASE_RUNNING,
					    NULL, NULL, NULL, timeout);

	/* Even if wait times out, check current state one more time
	 * in case phase changed after timeout was reached
	 */
	if ((ret < 0) && (ctx->phase == PPP_RUNNING)) {
		printk("PPP reached running phase (final check after timeout)\n");
		return 0;
	}

	return ret;
}

int ppp_session_wait_ipv4_ready(struct net_if *iface, k_timeout_t timeout)
{
	int64_t remain_ms;
	int ret;

	if (iface == NULL) {
		printk("PPP interface not found\n");
		return -ENODEV;
	}

	printk("wait for PPP IPv4 address\n");

	remain_ms = k_ticks_to_ms_floor64(timeout.ticks);
	if (remain_ms < 0) {
		remain_ms = INT64_MAX;
	}

	while (1) {
		const struct in_addr *addr;
		int32_t step_ms;

		addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
		if (addr != NULL) {
			char ip_buf[NET_IPV4_ADDR_LEN];

			net_addr_ntop(AF_INET, addr, ip_buf, sizeof(ip_buf));
			printk("PPP IPv4 ready: %s\n", ip_buf);
			return 0;
		}

		if (remain_ms == 0) {
			printk("PPP IPv4 address timeout\n");
			return -ETIMEDOUT;
		}

		step_ms = (remain_ms > 1000) ? 1000 : (int32_t)remain_ms;
		ret = net_mgmt_event_wait_on_iface(iface, NET_EVENT_IPV4_ADDR_ADD,
						   NULL, NULL, NULL,
						   K_MSEC(step_ms));
		if ((ret < 0) && (ret != -EAGAIN) && (ret != -ETIMEDOUT)) {
			return ret;
		}

		if (remain_ms != INT64_MAX) {
			remain_ms -= step_ms;
		}
	}
}

struct ppp_ping_wait_ctx {
	struct k_sem sem;
	struct in_addr expected;
	bool replied;
};

static enum net_verdict ppp_session_ping_reply_handler(struct net_icmp_ctx *ctx,
						       struct net_pkt *pkt,
						       struct net_icmp_ip_hdr *ip_hdr,
						       struct net_icmp_hdr *icmp_hdr,
						       void *user_data)
{
	struct ppp_ping_wait_ctx *wait_ctx = (struct ppp_ping_wait_ctx *)user_data;

	ARG_UNUSED(ctx);
	ARG_UNUSED(pkt);
	ARG_UNUSED(icmp_hdr);

	if ((wait_ctx == NULL) || (ip_hdr == NULL) || (ip_hdr->family != AF_INET) ||
	    (ip_hdr->ipv4 == NULL)) {
		return NET_DROP;
	}

	if (memcmp(&ip_hdr->ipv4->src, &wait_ctx->expected, sizeof(wait_ctx->expected)) != 0) {
		return NET_CONTINUE;
	}

	wait_ctx->replied = true;
	k_sem_give(&wait_ctx->sem);

	return NET_OK;
}

int ppp_session_ping_ipv4(struct net_if *iface, const char *target_ipv4, k_timeout_t timeout)
{
	struct net_icmp_ctx icmp_ctx;
	struct ppp_ping_wait_ctx wait_ctx;
	struct net_icmp_ping_params params = {0};
	struct sockaddr_in target = {0};
	int ret;

	if ((iface == NULL) || (target_ipv4 == NULL)) {
		return -EINVAL;
	}

	target.sin_family = AF_INET;
	ret = net_addr_pton(AF_INET, target_ipv4, &target.sin_addr);
	if (ret < 0) {
		printk("invalid ping target: %s\n", target_ipv4);
		return ret;
	}

	k_sem_init(&wait_ctx.sem, 0, 1);
	wait_ctx.expected = target.sin_addr;
	wait_ctx.replied = false;

	ret = net_icmp_init_ctx(&icmp_ctx, NET_AF_INET, NET_ICMPV4_ECHO_REPLY, 0,
				ppp_session_ping_reply_handler);
	if (ret < 0) {
		printk("net_icmp_init_ctx failed: %d\n", ret);
		return ret;
	}

	params.identifier = 0x4295;
	params.sequence = 1;
	params.data = "stm32_modem_demo";
	params.data_size = strlen((const char *)params.data);

	printk("ping %s ...\n", target_ipv4);
	ret = net_icmp_send_echo_request(&icmp_ctx, iface, (struct sockaddr *)&target, &params,
					 &wait_ctx);
	if (ret < 0) {
		printk("ping send failed: %d\n", ret);
		net_icmp_cleanup_ctx(&icmp_ctx);
		return ret;
	}

	ret = k_sem_take(&wait_ctx.sem, timeout);
	net_icmp_cleanup_ctx(&icmp_ctx);
	if ((ret < 0) || !wait_ctx.replied) {
		printk("ping %s timeout\n", target_ipv4);
		return -ETIMEDOUT;
	}

	printk("ping %s success\n", target_ipv4);
	return 0;
}
