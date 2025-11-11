#ifndef TEST_NETWORK_CONFIG_H
#define TEST_NETWORK_CONFIG_H

/*
 * Network Configuration for All Tests
 *
 * This header defines common network settings used across all test cases
 * to ensure consistency and avoid hardcoded values.
 */

// ============================================================================
// LAN Network Configuration (192.168.1.0/24)
// ============================================================================

// Guest IP address (ucOS-II running in QEMU)
#ifndef LAN_GUEST_IP
#define LAN_GUEST_IP {192u, 168u, 1u, 1u}
#endif

// Host IP address (Linux host TAP interface)
#ifndef LAN_HOST_IP
#define LAN_HOST_IP {192u, 168u, 1u, 103u}
#endif

// Readable string versions for printing
#define LAN_GUEST_IP_STR "192.168.1.1"
#define LAN_HOST_IP_STR  "192.168.1.103"

// ============================================================================
// WAN Network Configuration (10.3.5.0/24)
// ============================================================================

// Guest IP address (ucOS-II WAN interface)
#ifndef WAN_GUEST_IP
#define WAN_GUEST_IP {10u, 3u, 5u, 99u}
#endif

// Host IP address (Linux host WAN TAP interface)
#ifndef WAN_HOST_IP
#define WAN_HOST_IP {10u, 3u, 5u, 103u}
#endif

// Readable string versions for printing
#define WAN_GUEST_IP_STR "10.3.5.99"
#define WAN_HOST_IP_STR  "10.3.5.103"

#endif /* TEST_NETWORK_CONFIG_H */
