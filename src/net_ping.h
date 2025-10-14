#ifndef NET_PING_H
#define NET_PING_H

#include <stdint.h>
#include <stddef.h>

struct net_ping_target {
    const char *name;
    uint8_t guest_ip[4];
    uint8_t host_ip[4];
    uint8_t device_index;
};

struct net_ping_stats {
    uint32_t arp_latency_us;
    uint32_t ping_min_us;
    uint32_t ping_max_us;
    uint32_t ping_avg_us;
    uint32_t ping_count;
};

int net_ping_run(const struct net_ping_target *target,
                 uint32_t iterations,
                 struct net_ping_stats *stats);

#endif /* NET_PING_H */
