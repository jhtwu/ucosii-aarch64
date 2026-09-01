#ifndef UCOSII_ROUTER_CONFIG_H
#define UCOSII_ROUTER_CONFIG_H

/* Router network configuration. Keep these values in one place so the
 * protocol implementation, diagnostics, and test fixtures stay aligned. */
#define UCOSII_ROUTER_LAN_IP       {192u, 168u, 1u, 1u}
#define UCOSII_ROUTER_WAN_IP       {10u, 0u, 0u, 1u}
#define UCOSII_ROUTER_LAN_IP_STR   "192.168.1.1"
#define UCOSII_ROUTER_WAN_IP_STR   "10.0.0.1"

#endif /* UCOSII_ROUTER_CONFIG_H */
