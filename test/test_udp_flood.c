#include <includes.h>
#include <bsp.h>
#include <bsp_os.h>
#include <portable_libc.h>
#include <virtio_net.h>

#include <stdbool.h>
#include <string.h>

#include "test_support.h"

#define NET_TASK_PRIO        5u
#define NET_STACK_SIZE       4096u
#define FLOOD_PACKET_COUNT   2000u
#define PAYLOAD_SIZE         512u
#define IDLE_SAMPLE_DELAY_MS 10u

static OS_STK net_task_stack[NET_STACK_SIZE];

extern struct virtio_net_dev *virtio_net_device;
extern volatile INT32U OSIdleCtr;

#define ETH_HEADER_LEN 14u
#define IPV4_HEADER_LEN 20u
#define UDP_HEADER_LEN 8u

struct udp_frame {
    uint8_t data[ETH_HEADER_LEN + IPV4_HEADER_LEN + UDP_HEADER_LEN + PAYLOAD_SIZE];
    size_t len;
};

static void build_udp_broadcast_frame(struct udp_frame *frame,
                                      const uint8_t local_mac[6],
                                      uint16_t src_port,
                                      uint16_t dst_port)
{
    uint8_t *eth = frame->data;
    uint8_t *ip = eth + ETH_HEADER_LEN;
    uint8_t *udp = ip + IPV4_HEADER_LEN;
    uint8_t *payload = udp + UDP_HEADER_LEN;

    /* Ethernet header */
    memset(eth, 0xFF, 6u);                   /* Broadcast destination */
    memcpy(eth + 6u, local_mac, 6u);         /* Source */
    eth[12] = 0x08;
    eth[13] = 0x00;

    /* IPv4 header */
    ip[0] = 0x45;
    ip[1] = 0;
    uint16_t total_length = (uint16_t)(20u + 8u + PAYLOAD_SIZE);
    test_store_be16(&ip[2], total_length);
    ip[4] = 0;
    ip[5] = 0;
    ip[6] = 0x40;                            /* Don't fragment */
    ip[7] = 0;
    ip[8] = 64;                              /* TTL */
    ip[9] = 17;                              /* UDP */
    ip[10] = 0;
    ip[11] = 0;
    /* Use link-local style addresses */
    ip[12] = 169; ip[13] = 254; ip[14] = 1; ip[15] = 1;
    ip[16] = 169; ip[17] = 254; ip[18] = 1; ip[19] = 255;
    uint16_t ip_chk = test_checksum16(ip, 20u);
    test_store_be16(&ip[10], ip_chk);

    /* UDP header */
    test_store_be16(&udp[0], src_port);
    test_store_be16(&udp[2], dst_port);
    test_store_be16(&udp[4], (uint16_t)(8u + PAYLOAD_SIZE));
    udp[6] = 0;
    udp[7] = 0;                              /* Checksum optional */

    /* Payload */
    test_fill_pattern(payload, PAYLOAD_SIZE, 0x30);

    frame->len = ETH_HEADER_LEN + total_length;
}

static void net_test_task(void *p_arg)
{
    (void)p_arg;

    printf("[TEST] UDP flood start\n");

    BSP_OS_TmrTickInit(1000u);

    if (eth_init() <= 0) {
        printf("[FAIL] eth_init() failed\n");
        goto wait_forever;
    }

    if (virtio_net_device == NULL) {
        printf("[FAIL] VirtIO device not initialized\n");
        goto wait_forever;
    }

    struct udp_frame frame;
    build_udp_broadcast_frame(&frame, virtio_net_device->eth_dev.enetaddr, 12345u, 54321u);

    uint64_t test_start_cycles = test_timer_read_cycles();
    INT32U idle_start = OSIdleCtr;

    uint32_t max_depth = 0u;
    uint32_t tx_failures = 0u;

    for (uint32_t i = 0; i < FLOOD_PACKET_COUNT; ++i) {
        if (virtio_net_send(&virtio_net_device->eth_dev, frame.data, (int)frame.len) != 0) {
            tx_failures++;
        }

        uint32_t current_depth =
            (uint32_t)(virtio_net_device->tx_avail->idx - virtio_net_device->tx_last_used);
        if (current_depth > max_depth) {
            max_depth = current_depth;
        }

        if ((i & 0x3F) == 0x3F) {
            OSTimeDlyHMSM(0u, 0u, 0u, IDLE_SAMPLE_DELAY_MS);
        }
    }

    /* Allow outstanding completions to drain */
    OSTimeDlyHMSM(0u, 0u, 0u, 50u);

    uint64_t test_end_cycles = test_timer_read_cycles();
    INT32U idle_end = OSIdleCtr;

    uint32_t duration_us = test_cycles_to_us(test_start_cycles, test_end_cycles);
    uint32_t idle_delta = idle_end - idle_start;
    uint32_t final_depth =
        (uint32_t)(virtio_net_device->tx_avail->idx - virtio_net_device->tx_last_used);

    printf("[RESULT] packets=%u duration_us=%u max_tx_depth=%u final_depth=%u tx_fail=%u idle_ticks=%u\n",
           FLOOD_PACKET_COUNT,
           duration_us,
           max_depth,
           final_depth,
           tx_failures,
           idle_delta);
    printf("[PASS] UDP flood statistics recorded\n");

wait_forever:
    for (;;) {
        OSTimeDlyHMSM(0u, 0u, 1u, 0u);
    }
}

int main(void)
{
    INT8U err;

    printf("\n========================================\n");
    printf("Test Case 3: UDP Flood Stress\n");
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
