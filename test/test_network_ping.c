#include <includes.h>
#include <asm/types.h>
#include <bsp.h>
#include <bsp_os.h>
#include <portable_libc.h>
#include <net.h>
#include <virtio_net.h>

#include <stdbool.h>
#include <string.h>

#include "test_support.h"

#define NET_TASK_PRIO    5u
#define NET_STACK_SIZE   4096u

#define HOST_IP0 192u
#define HOST_IP1 168u
#define HOST_IP2 1u
#define HOST_IP3 103u

#define GUEST_IP0 192u
#define GUEST_IP1 168u
#define GUEST_IP2 1u
#define GUEST_IP3 1u

#define PING_IDENTIFIER 0xBEEF
#define PING_PAYLOAD_LEN 16u
#define PING_ITERATIONS 4u

static OS_STK net_task_stack[NET_STACK_SIZE];

static volatile bool arp_completed = false;
static volatile bool ping_completed = false;
static volatile INT16U ping_sequence_observed = 0;
static INT16U ping_sequence_issued = 0;
static INT8U host_mac[6];
static volatile INT64U arp_response_cycles = 0;
static volatile INT64U ping_response_cycles = 0;

extern struct eth_device *ethdev;
extern struct virtio_net_dev *virtio_net_device;

static struct eth_device *select_device(void)
{
#ifdef CONFIG_VIRTIO_NET
    if (virtio_net_device != NULL) {
        return &virtio_net_device->eth_dev;
    }
#endif
    return ethdev;
}

static void build_arp_request(INT8U *frame, size_t *length, const INT8U *local_mac)
{
    static const size_t ETH_HDR = 14u;
    static const size_t ARP_SIZE = 28u;

    memset(frame, 0, ETH_HDR + ARP_SIZE);

    frame[0] = 0xFF; frame[1] = 0xFF; frame[2] = 0xFF; frame[3] = 0xFF; frame[4] = 0xFF; frame[5] = 0xFF;
    memcpy(&frame[6], local_mac, 6u);
    frame[12] = 0x08;
    frame[13] = 0x06;

    frame[14] = 0x00; frame[15] = 0x01;
    frame[16] = 0x08; frame[17] = 0x00;
    frame[18] = 0x06;
    frame[19] = 0x04;
    frame[20] = 0x00; frame[21] = 0x01;
    memcpy(&frame[22], local_mac, 6u);
    frame[28] = GUEST_IP0; frame[29] = GUEST_IP1; frame[30] = GUEST_IP2; frame[31] = GUEST_IP3;
    frame[38] = HOST_IP0; frame[39] = HOST_IP1; frame[40] = HOST_IP2; frame[41] = HOST_IP3;

    *length = ETH_HDR + ARP_SIZE;
}

static void build_icmp_request(INT8U *frame, size_t *length, const INT8U *local_mac, INT16U sequence)
{
    INT8U *eth = frame;
    INT8U *ip = frame + 14u;
    INT8U *icmp = ip + 20u;
    INT8U *payload = icmp + 8u;

    memcpy(&eth[0], host_mac, 6u);
    memcpy(&eth[6], local_mac, 6u);
    eth[12] = 0x08;
    eth[13] = 0x00;

    ip[0] = 0x45;
    ip[1] = 0x00;
    INT16U total_length = (INT16U)(20u + 8u + PING_PAYLOAD_LEN);
    test_store_be16(&ip[2], total_length);
    ip[4] = 0x00;
    ip[5] = 0x00;
    ip[6] = 0x00;
    ip[7] = 0x00;
    ip[8] = 64;
    ip[9] = 1;
    ip[10] = 0;
    ip[11] = 0;
    ip[12] = GUEST_IP0; ip[13] = GUEST_IP1; ip[14] = GUEST_IP2; ip[15] = GUEST_IP3;
    ip[16] = HOST_IP0; ip[17] = HOST_IP1; ip[18] = HOST_IP2; ip[19] = HOST_IP3;
    INT16U ip_sum = test_checksum16(ip, 20u);
    test_store_be16(&ip[10], ip_sum);

    icmp[0] = 8u;
    icmp[1] = 0u;
    icmp[2] = 0u;
    icmp[3] = 0u;
    test_store_be16(&icmp[4], PING_IDENTIFIER);
    test_store_be16(&icmp[6], sequence);
    for (INT32U i = 0; i < PING_PAYLOAD_LEN; ++i) {
        payload[i] = (INT8U)(0x30u + (i & 0x0Fu));
    }

    INT16U icmp_sum = test_checksum16(icmp, 8u + PING_PAYLOAD_LEN);
    test_store_be16(&icmp[2], icmp_sum);

    *length = 14u + total_length;
}

void test_net_on_frame(INT8U *pkt, int len)
{
    if (len < 14) {
        return;
    }

    INT16U ethertype = (INT16U)((pkt[12] << 8) | pkt[13]);

    if (ethertype == 0x0806 && len >= 42) {
        INT16U op = (INT16U)((pkt[20] << 8) | pkt[21]);
        INT8U *target_ip = &pkt[38];
        if (op == 0x0002 &&
            target_ip[0] == GUEST_IP0 &&
            target_ip[1] == GUEST_IP1 &&
            target_ip[2] == GUEST_IP2 &&
            target_ip[3] == GUEST_IP3 &&
            !arp_completed) {
            memcpy(host_mac, &pkt[22], 6u);
            arp_completed = true;
            arp_response_cycles = test_timer_read_cycles();
        }
        return;
    }

    if (ethertype == 0x0800 && len >= 42) {
        INT8U ihl = pkt[14] & 0x0Fu;
        INT8U protocol = pkt[23];
        INT8U *src_ip = &pkt[26];
        INT8U *dst_ip = &pkt[30];
        if (protocol == 1u &&
            ihl >= 5u &&
            src_ip[0] == HOST_IP0 && src_ip[1] == HOST_IP1 &&
            src_ip[2] == HOST_IP2 && src_ip[3] == HOST_IP3 &&
            dst_ip[0] == GUEST_IP0 && dst_ip[1] == GUEST_IP1 &&
            dst_ip[2] == GUEST_IP2 && dst_ip[3] == GUEST_IP3) {
            INT8U *icmp = &pkt[14 + (ihl * 4u)];
            if (icmp[0] == 0u) {
                INT16U seq = (INT16U)((icmp[6] << 8) | icmp[7]);
                if (!ping_completed && seq == ping_sequence_issued) {
                    ping_completed = true;
                    ping_sequence_observed = seq;
                    ping_response_cycles = test_timer_read_cycles();
                }
            }
        }
    }
}

static void net_test_task(void *p_arg)
{
    (void)p_arg;

    printf("[TEST] Network ping start\n");

    arp_completed = false;
    ping_completed = false;
    arp_response_cycles = 0u;
    ping_response_cycles = 0u;
    ping_sequence_observed = 0u;
    ping_sequence_issued = 0u;
    memset(host_mac, 0, sizeof(host_mac));

    BSP_OS_TmrTickInit(1000u);

    if (eth_init() <= 0) {
        printf("[FAIL] eth_init() failed\n");
        goto wait_forever;
    }

    struct eth_device *dev = select_device();
    if (dev == NULL || dev->send == NULL || dev->recv == NULL) {
        printf("[FAIL] No active Ethernet device after init\n");
        goto wait_forever;
    }

    INT8U local_mac[6];
    memcpy(local_mac, dev->enetaddr, sizeof(local_mac));

    INT8U frame[256];
    size_t frame_len = 0u;
    build_arp_request(frame, &frame_len, local_mac);

    INT64U arp_start_cycles = test_timer_read_cycles();
    dev->send(dev, frame, (int)frame_len);

    for (INT32U waited = 0; waited < 2000u && !arp_completed; waited += 10u) {
        dev->recv(dev);
        OSTimeDlyHMSM(0u, 0u, 0u, 10u);
    }

    if (!arp_completed) {
        printf("[FAIL] ARP response timeout\n");
        goto wait_forever;
    }

    if (test_mac_is_zero(host_mac)) {
        printf("[FAIL] ARP response contained invalid MAC\n");
        goto wait_forever;
    }

    INT32U arp_latency_us = test_cycles_to_us(arp_start_cycles, arp_response_cycles);

    INT32U ping_latencies[PING_ITERATIONS];
    INT32U ping_success = 0u;

    for (INT32U attempt = 0u; attempt < PING_ITERATIONS; ++attempt) {
        ping_completed = false;
        ping_sequence_observed = 0u;
        ping_response_cycles = 0u;
        ++ping_sequence_issued;

        build_icmp_request(frame, &frame_len, local_mac, ping_sequence_issued);
        INT64U ping_start_cycles = test_timer_read_cycles();
        dev->send(dev, frame, (int)frame_len);

        for (INT32U waited = 0; waited < 2000u; waited += 10u) {
            dev->recv(dev);
            if (ping_completed &&
                ping_sequence_observed == ping_sequence_issued &&
                ping_response_cycles != 0u) {
                ping_latencies[ping_success++] =
                    test_cycles_to_us(ping_start_cycles, ping_response_cycles);
                break;
            }
            OSTimeDlyHMSM(0u, 0u, 0u, 10u);
        }

        if (ping_success != attempt + 1u) {
            printf("[RESULT] arp_us=%u ping_us=timeout seq=%u\n",
                   arp_latency_us,
                   (unsigned)ping_sequence_issued);
            printf("[FAIL] ICMP echo reply missing\n");
            goto wait_forever;
        }
    }

    INT32U ping_min = ping_latencies[0];
    INT32U ping_max = ping_latencies[0];
    INT32U ping_sum = 0u;
    for (INT32U i = 0u; i < ping_success; ++i) {
        if (ping_latencies[i] < ping_min) {
            ping_min = ping_latencies[i];
        }
        if (ping_latencies[i] > ping_max) {
            ping_max = ping_latencies[i];
        }
        ping_sum += ping_latencies[i];
    }
    INT32U ping_avg = (ping_sum + (ping_success / 2u)) / ping_success;

    printf("[RESULT] arp_us=%u ping_us[min=%u max=%u avg=%u] count=%u\n",
           arp_latency_us,
           ping_min,
           ping_max,
           ping_avg,
           ping_success);
    printf("[PASS] Ping response statistics captured\n");

wait_forever:
    for (;;) {
        OSTimeDlyHMSM(0u, 0u, 1u, 0u);
    }
}

int main(void)
{
    INT8U err;

    printf("\n========================================\n");
    printf("Test Case 2: Network Ping\n");
    printf("========================================\n");

    CPU_Init();
    Mem_Init();
    BSP_Init();

    OSInit();

    err = OSTaskCreate(net_test_task,
                       0,
                       &net_task_stack[NET_STACK_SIZE - 1u],
                       NET_TASK_PRIO);
    if (err != OS_ERR_NONE) {
        printf("[ERROR] Failed to create network test task (err=%u)\n", err);
        return 1;
    }

    __asm__ volatile("msr daifclr, #0x2");

    printf("[BOOT] Starting scheduler\n\n");
    OSStart();

    printf("[ERROR] Returned from OSStart()\n");
    return 1;
}
