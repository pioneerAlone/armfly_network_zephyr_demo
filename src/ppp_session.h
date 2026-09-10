/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PPP_SESSION_H_
#define PPP_SESSION_H_

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>

struct net_if *ppp_session_get_iface(void);
int ppp_session_bring_up(struct net_if *iface);
int ppp_session_bring_down(struct net_if *iface);
int ppp_session_wait_connected(struct net_if *iface, k_timeout_t timeout);
int ppp_session_wait_ipv4_ready(struct net_if *iface, k_timeout_t timeout);
int ppp_session_ping_ipv4(struct net_if *iface, const char *target_ipv4, k_timeout_t timeout);

#endif /* PPP_SESSION_H_ */
