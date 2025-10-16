#include "net_ping.h"

#include <includes.h>
#include <virtio_net.h>
#include <net.h>
#include <portable_libc.h>
#include <stdbool.h>
#include <string.h>

#define NET_PING_MAX_ITERATIONS 16u

extern struct virtio_net_dev *virtio_net_device;

struct net_ping_state {
    volatile bool arp_completed;
    volatile bool ping_completed;
    volatile uint16_t ping_sequence_observed;
    volatile uint16_t ping_sequence_issued;
    volatile uint64_t arp_response_cycles;
    volatile uint64_t ping_response_cycles;
    volatile uint8_t host_mac[6];
};

struct net_ping_context {
    struct net_ping_target target;
    uint32_t iterations;
    struct net_ping_stats stats;
    struct net_ping_state state;
    struct virtio_net_dev *virt_dev;
    struct eth_device *device;
    uint32_t irq_base;
    uint32_t rx_base;
    uint8_t local_mac[6];
};

static struct net_ping_context g_ping_ctx;
static struct net_ping_context *volatile g_active_ctx = NULL;

static uint64_t net_ping_timer_frequency_hz(void)
{
    static uint64_t cached_freq = 0u;

    if (cached_freq == 0u) {
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(cached_freq));
        if (cached_freq == 0u) {
            cached_freq = 1u;
        }
    }

    return cached_freq;
}

static uint64_t net_ping_read_cycles(void)
{
    uint64_t cycles;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(cycles));
    return cycles;
}

static uint32_t net_ping_cycles_to_us(uint64_t start_cycles, uint64_t end_cycles)
{
    uint64_t freq = net_ping_timer_frequency_hz();
    uint64_t delta = end_cycles - start_cycles;

    uint64_t numerator = delta * 1000000u;
    uint64_t rounded = numerator + (freq / 2u);
    return (uint32_t)(rounded / freq);
}

static uint16_t net_ping_checksum16(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0u;

    while (length > 1u) {
        sum += (uint32_t)(((uint32_t)bytes[0] << 8) | bytes[1]);
        bytes += 2u;
        length -= 2u;
    }

    if (length == 1u) {
        sum += (uint32_t)bytes[0] << 8;
    }

    while ((sum >> 16u) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16u);
    }

    return (uint16_t)(~sum);
}

static void net_ping_store_be16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)(value & 0xFFu);
}

static bool net_ping_mac_is_zero(const uint8_t mac[6])
{
    for (size_t i = 0; i < 6u; ++i) {
        if (mac[i] != 0u) {
            return false;
        }
    }
    return true;
}

static void net_ping_print_mac(const char *label, const uint8_t mac[6])
{
    printf("[DEBUG] %s %02X:%02X:%02X:%02X:%02X:%02X\n",
           label,
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void net_ping_print_ip(const char *label, const uint8_t ip[4])
{
    printf("[DEBUG] %s %u.%u.%u.%u\n",
           label,
           ip[0], ip[1], ip[2], ip[3]);
}

static void net_ping_build_arp_request(struct net_ping_context *ctx,
                                       uint8_t *frame,
                                       size_t *length)
{
    static const size_t ETH_HDR = 14u;
    static const size_t ARP_SIZE = 28u;

    memset(frame, 0, ETH_HDR + ARP_SIZE);

    frame[0] = 0xFF; frame[1] = 0xFF; frame[2] = 0xFF; frame[3] = 0xFF; frame[4] = 0xFF; frame[5] = 0xFF;
    memcpy(&frame[6], ctx->local_mac, 6u);
    frame[12] = 0x08;
    frame[13] = 0x06;

    frame[14] = 0x00; frame[15] = 0x01;
    frame[16] = 0x08; frame[17] = 0x00;
    frame[18] = 0x06;
    frame[19] = 0x04;
    frame[20] = 0x00; frame[21] = 0x01;
    memcpy(&frame[22], ctx->local_mac, 6u);
    frame[28] = ctx->target.guest_ip[0];
    frame[29] = ctx->target.guest_ip[1];
    frame[30] = ctx->target.guest_ip[2];
    frame[31] = ctx->target.guest_ip[3];
    frame[38] = ctx->target.host_ip[0];
    frame[39] = ctx->target.host_ip[1];
    frame[40] = ctx->target.host_ip[2];
    frame[41] = ctx->target.host_ip[3];

    *length = ETH_HDR + ARP_SIZE;
}

static void net_ping_build_icmp_request(struct net_ping_context *ctx,
                                        uint8_t *frame,
                                        size_t *length,
                                        uint16_t sequence)
{
    uint8_t *eth = frame;
    uint8_t *ip = frame + 14u;
    uint8_t *icmp = ip + 20u;
    uint8_t *payload = icmp + 8u;

    uint8_t host_mac_copy[6];
    for (size_t i = 0; i < 6u; ++i) {
        host_mac_copy[i] = ctx->state.host_mac[i];
    }

    memcpy(&eth[0], host_mac_copy, 6u);
    memcpy(&eth[6], ctx->local_mac, 6u);
    eth[12] = 0x08;
    eth[13] = 0x00;

    ip[0] = 0x45;
    ip[1] = 0x00;
    uint16_t total_length = (uint16_t)(20u + 8u + 16u);
    net_ping_store_be16(&ip[2], total_length);
    ip[4] = 0x00;
    ip[5] = 0x00;
    ip[6] = 0x00;
    ip[7] = 0x00;
    ip[8] = 64;
    ip[9] = 1;
    ip[10] = 0;
    ip[11] = 0;
    ip[12] = ctx->target.guest_ip[0];
    ip[13] = ctx->target.guest_ip[1];
    ip[14] = ctx->target.guest_ip[2];
    ip[15] = ctx->target.guest_ip[3];
    ip[16] = ctx->target.host_ip[0];
    ip[17] = ctx->target.host_ip[1];
    ip[18] = ctx->target.host_ip[2];
    ip[19] = ctx->target.host_ip[3];
    uint16_t ip_sum = net_ping_checksum16(ip, 20u);
    net_ping_store_be16(&ip[10], ip_sum);

    icmp[0] = 8u;
    icmp[1] = 0u;
    icmp[2] = 0u;
    icmp[3] = 0u;
    net_ping_store_be16(&icmp[4], 0xBEEF);
    net_ping_store_be16(&icmp[6], sequence);
    for (uint32_t i = 0; i < 16u; ++i) {
        payload[i] = (uint8_t)(0x30u + (i & 0x0Fu));
    }

    uint16_t icmp_sum = net_ping_checksum16(icmp, 8u + 16u);
    net_ping_store_be16(&icmp[2], icmp_sum);

    *length = 14u + total_length;
}

void test_net_on_frame(uint8_t *pkt, int len)
{
    struct net_ping_context *ctx = (struct net_ping_context *)g_active_ctx;

    if (ctx == NULL) {
        return;
    }

    if (len < 14) {
        return;
    }

    uint16_t ethertype = (uint16_t)((pkt[12] << 8) | pkt[13]);

    if (ethertype == 0x0806 && len >= 42) {
        uint16_t op = (uint16_t)((pkt[20] << 8) | pkt[21]);
        uint8_t *target_ip = &pkt[38];
        if (op == 0x0002 &&
            target_ip[0] == ctx->target.guest_ip[0] &&
            target_ip[1] == ctx->target.guest_ip[1] &&
            target_ip[2] == ctx->target.guest_ip[2] &&
            target_ip[3] == ctx->target.guest_ip[3] &&
            !ctx->state.arp_completed) {
            for (size_t i = 0; i < 6u; ++i) {
                ctx->state.host_mac[i] = pkt[22 + i];
            }
            ctx->state.arp_completed = true;
            ctx->state.arp_response_cycles = net_ping_read_cycles();
            printf("[DEBUG] Received ARP reply from %u.%u.%u.%u\n",
                   pkt[28], pkt[29], pkt[30], pkt[31]);
            net_ping_print_mac("Peer MAC:", &pkt[22]);
        }
        return;
    }

    if (ethertype == 0x0800 && len >= 42) {
        uint8_t ihl = pkt[14] & 0x0Fu;
        uint8_t protocol = pkt[23];
        uint8_t *src_ip = &pkt[26];
        uint8_t *dst_ip = &pkt[30];
        if (protocol == 1u &&
            ihl >= 5u &&
            src_ip[0] == ctx->target.host_ip[0] &&
            src_ip[1] == ctx->target.host_ip[1] &&
            src_ip[2] == ctx->target.host_ip[2] &&
            src_ip[3] == ctx->target.host_ip[3] &&
            dst_ip[0] == ctx->target.guest_ip[0] &&
            dst_ip[1] == ctx->target.guest_ip[1] &&
            dst_ip[2] == ctx->target.guest_ip[2] &&
            dst_ip[3] == ctx->target.guest_ip[3]) {
            uint8_t *icmp = &pkt[14 + (ihl * 4u)];
            if (icmp[0] == 0u) {
                uint16_t seq = (uint16_t)((icmp[6] << 8) | icmp[7]);
                if (!ctx->state.ping_completed && seq == ctx->state.ping_sequence_issued) {
                    ctx->state.ping_completed = true;
                    ctx->state.ping_sequence_observed = seq;
                    ctx->state.ping_response_cycles = net_ping_read_cycles();
                    printf("[DEBUG] ICMP echo reply seq=%u received\n", (unsigned)seq);
                }
            }
        }
    }
}

int net_ping_run(const struct net_ping_target *target,
                 uint32_t iterations,
                 struct net_ping_stats *stats)
{
    struct net_ping_context *ctx = &g_ping_ctx;
    struct virtio_net_dev *virt_dev = NULL;
    struct eth_device *dev = NULL;
    uint8_t frame[256];
    size_t frame_len = 0u;
    uint32_t requested_iterations = (iterations == 0u) ? 1u : iterations;
    uint32_t ping_latencies[NET_PING_MAX_ITERATIONS];

    if (requested_iterations > NET_PING_MAX_ITERATIONS) {
        requested_iterations = NET_PING_MAX_ITERATIONS;
    }

    virt_dev = virtio_net_get_device(target->device_index);
    if (virt_dev != NULL) {
        dev = &virt_dev->eth_dev;
    }

#ifdef CONFIG_VIRTIO_NET
    if (dev == NULL && virtio_net_device != NULL) {
        dev = &virtio_net_device->eth_dev;
        virt_dev = virtio_net_device;
    }
#endif

    if (dev == NULL) {
        dev = eth_get_dev();
    }

    if (dev == NULL || dev->send == NULL || dev->recv == NULL) {
        printf("[FAIL] No active Ethernet device available\n");
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->target = *target;
    ctx->iterations = requested_iterations;
    ctx->virt_dev = virt_dev;
    ctx->device = dev;
    memcpy(ctx->local_mac, dev->enetaddr, sizeof(ctx->local_mac));

    printf("[TEST] %s ping start\n", (target->name != NULL) ? target->name : "Network");
    net_ping_print_mac("Local MAC:", ctx->local_mac);
    net_ping_print_ip("Guest IP:", ctx->target.guest_ip);
    net_ping_print_ip("Target IP:", ctx->target.host_ip);

    g_active_ctx = ctx;

    if (ctx->virt_dev != NULL) {
        ctx->irq_base = ctx->virt_dev->irq_count;
        ctx->rx_base = ctx->virt_dev->rx_packet_count;
    } else {
        ctx->irq_base = 0u;
        ctx->rx_base = 0u;
    }

    net_ping_build_arp_request(ctx, frame, &frame_len);
    uint64_t arp_start_cycles = net_ping_read_cycles();
    printf("[DEBUG] Sending ARP request (len=%u)\n", (unsigned)frame_len);
    if (dev->send(dev, frame, (int)frame_len) < 0) {
        printf("[ERROR] Failed to transmit ARP request\n");
    }

    bool arp_wait_logged = false;
    for (uint32_t waited = 0u; waited < 2000u && !ctx->state.arp_completed; waited += 10u) {
#ifndef CONFIG_VIRTIO_NET_ENABLE_IRQS
        dev->recv(dev);
#endif
        if (!ctx->state.arp_completed && !arp_wait_logged && waited >= 500u) {
            printf("[DEBUG] Still waiting for ARP reply... (%u ms elapsed)\n", waited);
            arp_wait_logged = true;
        }
        OSTimeDlyHMSM(0u, 0u, 0u, 10u);
    }

    if (!ctx->state.arp_completed) {
        printf("[FAIL] ARP response timeout\n");
        g_active_ctx = NULL;
        return -1;
    }

    if (net_ping_mac_is_zero((const uint8_t *)ctx->state.host_mac)) {
        printf("[FAIL] ARP response contained invalid MAC\n");
        net_ping_print_mac("Peer MAC (invalid):", ctx->state.host_mac);
        g_active_ctx = NULL;
        return -1;
    }
    net_ping_print_mac("Resolved peer MAC:", ctx->state.host_mac);

    ctx->stats.arp_latency_us = net_ping_cycles_to_us(arp_start_cycles, ctx->state.arp_response_cycles);

    uint32_t successful_pings = 0u;
    for (uint32_t attempt = 0u; attempt < ctx->iterations; ++attempt) {
        ctx->state.ping_completed = false;
        ctx->state.ping_sequence_observed = 0u;
        ctx->state.ping_response_cycles = 0u;
        ctx->state.ping_sequence_issued++;

        net_ping_build_icmp_request(ctx, frame, &frame_len, ctx->state.ping_sequence_issued);
        uint64_t ping_start_cycles = net_ping_read_cycles();
        printf("[DEBUG] Sending ICMP echo request seq=%u (len=%u)\n",
               (unsigned)ctx->state.ping_sequence_issued,
               (unsigned)frame_len);
        if (dev->send(dev, frame, (int)frame_len) < 0) {
            printf("[ERROR] Failed to transmit ICMP request seq=%u\n",
                   (unsigned)ctx->state.ping_sequence_issued);
        }

        bool ping_wait_logged = false;
        for (uint32_t waited = 0u; waited < 2000u; waited += 10u) {
#ifndef CONFIG_VIRTIO_NET_ENABLE_IRQS
            dev->recv(dev);
#endif
            if (ctx->state.ping_completed &&
                ctx->state.ping_sequence_observed == ctx->state.ping_sequence_issued &&
                ctx->state.ping_response_cycles != 0u) {
                ping_latencies[successful_pings++] =
                    net_ping_cycles_to_us(ping_start_cycles, ctx->state.ping_response_cycles);
                printf("[DEBUG] ICMP echo reply seq=%u latency=%u us\n",
                       (unsigned)ctx->state.ping_sequence_observed,
                       ping_latencies[successful_pings - 1u]);
                break;
            }
            if (!ctx->state.ping_completed && !ping_wait_logged && waited >= 500u) {
                printf("[DEBUG] Still waiting for ICMP reply seq=%u... (%u ms elapsed)\n",
                       (unsigned)ctx->state.ping_sequence_issued,
                       waited);
                ping_wait_logged = true;
            }
            OSTimeDlyHMSM(0u, 0u, 0u, 10u);
        }

        if (successful_pings != attempt + 1u) {
            printf("[RESULT] arp_us=%u ping_us=timeout seq=%u\n",
                   ctx->stats.arp_latency_us,
                   (unsigned)ctx->state.ping_sequence_issued);
            printf("[FAIL] ICMP echo reply missing\n");
            net_ping_print_mac("Peer MAC (cached):", ctx->state.host_mac);
            g_active_ctx = NULL;
            return -1;
        }
    }

    if (successful_pings > 0u) {
        uint32_t ping_min = ping_latencies[0];
        uint32_t ping_max = ping_latencies[0];
        uint32_t ping_sum = 0u;

        for (uint32_t i = 0u; i < successful_pings; ++i) {
            if (ping_latencies[i] < ping_min) {
                ping_min = ping_latencies[i];
            }
            if (ping_latencies[i] > ping_max) {
                ping_max = ping_latencies[i];
            }
            ping_sum += ping_latencies[i];
        }

        ctx->stats.ping_min_us = ping_min;
        ctx->stats.ping_max_us = ping_max;
        ctx->stats.ping_avg_us = (ping_sum + (successful_pings / 2u)) / successful_pings;
        ctx->stats.ping_count = successful_pings;
    }

    if (target->name != NULL) {
        printf("[INFO] Ping target: %s\n", target->name);
    }

    printf("[RESULT] arp_us=%u ping_us[min=%u max=%u avg=%u] count=%u\n",
           ctx->stats.arp_latency_us,
           ctx->stats.ping_min_us,
           ctx->stats.ping_max_us,
           ctx->stats.ping_avg_us,
           ctx->stats.ping_count);
    printf("[PASS] Ping response statistics captured\n");

#ifdef CONFIG_VIRTIO_NET_ENABLE_IRQS
    if (ctx->virt_dev != NULL) {
        uint32_t irq_delta = ctx->virt_dev->irq_count - ctx->irq_base;
        uint32_t rx_delta = ctx->virt_dev->rx_packet_count - ctx->rx_base;
        printf("[INFO] IRQ delta=%u RX packets=%u\n", irq_delta, rx_delta);
        if (irq_delta == 0u || rx_delta == 0u) {
            printf("[FAIL] No interrupt activity detected\n");
            g_active_ctx = NULL;
            return -1;
        }
    }
#endif

    if (stats != NULL) {
        *stats = ctx->stats;
    }

    g_active_ctx = NULL;
    return 0;
}
