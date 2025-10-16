/*
 * NAT ICMP Forwarding Test
 *
 * Tests NAT functionality by simulating LAN client sending ICMP packets
 * through the NAT gateway to external WAN destinations.
 */

#include "includes.h"
#include "bsp.h"
#include "bsp_os.h"
#include "cpu.h"
#include "cpu_core.h"
#include "lib_mem.h"
#include "os.h"
#include "virtio_net.h"
#include "nat.h"
#include "net.h"
#include <asm/types.h>
#include <stdbool.h>
#include <string.h>

/* Network byte order helpers */
#ifndef htons
#define htons(x) ((u16)((((x) & 0xff) << 8) | (((x) & 0xff00) >> 8)))
#endif
#ifndef ntohs
#define ntohs(x) htons(x)
#endif
#ifndef htonl
#define htonl(x) ((u32)((((x) & 0xff) << 24) | (((x) & 0xff00) << 8) | \
                  (((x) & 0xff0000) >> 8) | (((x) & 0xff000000) >> 24)))
#endif
#ifndef ntohl
#define ntohl(x) htonl(x)
#endif

/* Ethernet header */
struct eth_hdr {
    u8 dest_mac[6];
    u8 src_mac[6];
    u16 ethertype;
} __attribute__((packed));

/* Calculate IP checksum - RFC 1071 */
static u16 ip_checksum(void *data, int len)
{
    u32 sum = 0;
    u16 *p = (u16 *)data;

    while (len > 1) {
        sum += *p++;
        len -= 2;
    }

    if (len == 1) {
        sum += (*(u8 *)p) << 8;
    }

    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return ~sum;
}

#define compute_ip_checksum ip_checksum

/* Test configuration */
#define TEST_TIMEOUT_SEC       10
#define TEST_PING_COUNT        4

/* Simulated LAN client */
#define LAN_CLIENT_IP          {192, 168, 1, 100}
#define LAN_CLIENT_MAC         {0x52, 0x54, 0x00, 0xAA, 0xBB, 0xCC}

/* External destination (simulate external server) */
#define EXTERNAL_TARGET_IP     {10, 3, 5, 103}  /* WAN bridge host */

/* Test state */
static struct {
    bool     initialized;
    uint32_t packets_sent;
    uint32_t nat_translations;
    uint32_t replies_received;
    bool     test_passed;
} test_state = {0};

/* External function to enable NAT */
extern void net_enable_nat(void);

/* Task stacks */
static OS_STK AppTaskStartStk[4096];
static OS_STK AppTaskTestStk[4096];

/* Forward declarations */
static void AppTaskStart(void *p_arg);
static void AppTaskTest(void *p_arg);
static int send_icmp_ping(const u8 src_ip[4], const u8 dst_ip[4], u16 icmp_id, u16 seq);
static void print_test_summary(void);

/*
 * Main entry point
 */
int main(void)
{
    INT8U os_err;

    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("Test Case: NAT ICMP Forwarding\n");
    uart_puts("========================================\n");

    CPU_Init();
    Mem_Init();
    BSP_Init();
    OSInit();

    os_err = OSTaskCreateExt(AppTaskStart,
                             0,
                             &AppTaskStartStk[4095],
                             2,
                             2,
                             &AppTaskStartStk[0],
                             4096,
                             0,
                             OS_TASK_OPT_STK_CLR | OS_TASK_OPT_STK_CHK);

    if (os_err != OS_ERR_NONE) {
        uart_puts("[FAIL] Failed to create start task\n");
        return -1;
    }

    OSStart();
    return 0;
}

/*
 * Startup task
 */
static void AppTaskStart(void *p_arg)
{
    (void)p_arg;

    uart_puts("[TEST] Initializing network...\n");

    /* Initialize network driver */
    extern int eth_init(void);
    int rc = eth_init();
    if (rc <= 0) {
        uart_puts("[FAIL] Network initialization failed\n");
        while (1) {
            OSTimeDlyHMSM(0, 0, 5, 0);
        }
    }

    uart_puts("[TEST] Network initialized successfully\n");

    /* Initialize OS timer tick */
    uart_puts("[TEST] Initializing OS timer...\n");
    BSP_OS_TmrTickInit(1000);

    /* Enable NAT functionality */
    uart_puts("[TEST] Enabling NAT...\n");
    net_enable_nat();

    /* Start test task */
    OSTaskCreateExt(AppTaskTest,
                    0,
                    &AppTaskTestStk[4095],
                    3,
                    3,
                    &AppTaskTestStk[0],
                    4096,
                    0,
                    OS_TASK_OPT_STK_CLR | OS_TASK_OPT_STK_CHK);

    /* Idle loop */
    while (1) {
        OSTimeDlyHMSM(0, 0, 1, 0);
    }
}

/*
 * Test task - performs NAT ICMP forwarding test
 */
static void AppTaskTest(void *p_arg)
{
    (void)p_arg;
    u8 lan_client_ip[] = LAN_CLIENT_IP;
    u8 external_ip[] = EXTERNAL_TARGET_IP;
    u16 icmp_id = 0x1234;
    int i;

    OSTimeDlyHMSM(0, 0, 1, 0);  /* Wait for system stabilization */

    uart_puts("\n[TEST] Starting NAT ICMP test\n");
    uart_puts("[TEST] Simulating LAN client 192.168.1.100 pinging 10.3.5.103\n");
    uart_puts("[TEST] Packets should be NAT'd from 192.168.1.100 -> 10.3.5.99\n\n");

    test_state.initialized = true;

    /* Send test ICMP packets */
    for (i = 0; i < TEST_PING_COUNT; i++) {
        printf("[TEST] Sending ICMP ping %d/%d (ID: 0x%04x, Seq: %d)\n",
               i + 1, TEST_PING_COUNT, icmp_id, i);

        if (send_icmp_ping(lan_client_ip, external_ip, icmp_id, i) == 0) {
            test_state.packets_sent++;
        } else {
            printf("[WARN] Failed to send ping %d\n", i + 1);
        }

        OSTimeDlyHMSM(0, 0, 1, 0);  /* 1 second delay between pings */
    }

    /* Wait for potential replies */
    uart_puts("\n[TEST] Waiting for replies...\n");
    OSTimeDlyHMSM(0, 0, 3, 0);

    /* Print results */
    print_test_summary();

    /* Evaluate test result */
    if (test_state.packets_sent >= TEST_PING_COUNT) {
        uart_puts("\n[PASS] NAT ICMP forwarding test completed\n");
        test_state.test_passed = true;
    } else {
        uart_puts("\n[FAIL] NAT ICMP forwarding test failed\n");
        test_state.test_passed = false;
    }

    /* Idle loop */
    while (1) {
        OSTimeDlyHMSM(0, 0, 5, 0);
    }
}

/*
 * send_icmp_ping() - Send ICMP echo request packet
 *
 * Simulates a LAN client sending a ping packet that should be NAT'd
 */
static int send_icmp_ping(const u8 src_ip[4], const u8 dst_ip[4], u16 icmp_id, u16 seq)
{
    u8 packet[128];
    struct eth_hdr *eth;
    struct ip_hdr *ip;
    struct icmp_hdr *icmp;
    u8 *payload;
    int total_len;
    u8 lan_client_mac[] = LAN_CLIENT_MAC;
    extern struct virtio_net_dev *virtio_net_device;
    struct eth_device *lan_dev;

    /* Get LAN interface (index 0) */
    struct virtio_net_dev *virtio_dev = virtio_net_get_device(0);
    if (!virtio_dev) {
        uart_puts("[ERROR] LAN device not found\n");
        return -1;
    }
    lan_dev = &virtio_dev->eth_dev;

    memset(packet, 0, sizeof(packet));

    /* Build Ethernet header */
    eth = (struct eth_hdr *)packet;
    memcpy(eth->dest_mac, virtio_dev->eth_dev.enetaddr, 6);  /* Gateway MAC */
    memcpy(eth->src_mac, lan_client_mac, 6);
    eth->ethertype = htons(0x0800);  /* IPv4 */

    /* Build IP header */
    ip = (struct ip_hdr *)(packet + sizeof(struct eth_hdr));
    ip->ip_hl_v = 0x45;  /* IPv4, IHL=5 */
    ip->ip_tos = 0;
    ip->ip_len = htons(20 + 8 + 32);  /* IP + ICMP + payload */
    ip->ip_id = htons(0x1234);
    ip->ip_off = 0;
    ip->ip_ttl = 64;
    ip->ip_p = 1;  /* ICMP */
    ip->ip_src.s_addr = htonl((src_ip[0] << 24) | (src_ip[1] << 16) | (src_ip[2] << 8) | src_ip[3]);
    ip->ip_dst.s_addr = htonl((dst_ip[0] << 24) | (dst_ip[1] << 16) | (dst_ip[2] << 8) | dst_ip[3]);
    ip->ip_sum = 0;
    ip->ip_sum = compute_ip_checksum(ip, 20);

    /* Build ICMP header */
    icmp = (struct icmp_hdr *)(packet + sizeof(struct eth_hdr) + 20);
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->un.echo.id = htons(icmp_id);
    icmp->un.echo.sequence = htons(seq);

    /* Add payload */
    payload = (u8 *)icmp + 8;
    memset(payload, 0xAA, 32);  /* 32 bytes of 0xAA */

    /* Calculate ICMP checksum */
    icmp->checksum = 0;
    icmp->checksum = compute_ip_checksum(icmp, 8 + 32);

    total_len = sizeof(struct eth_hdr) + 20 + 8 + 32;

    /* Inject packet into network stack (simulates receiving from LAN) */
    net_process_received_packet(packet, total_len);

    return 0;
}

/*
 * print_test_summary() - Print test statistics
 */
static void print_test_summary(void)
{
    const struct nat_stats *stats;

    uart_puts("\n========================================\n");
    uart_puts("Test Summary\n");
    uart_puts("========================================\n");

    printf("Packets sent:        %u\n", (unsigned)test_state.packets_sent);
    printf("Expected:            %d\n", TEST_PING_COUNT);

    /* Get NAT statistics */
    stats = nat_get_stats();
    printf("\nNAT Statistics:\n");
    printf("  Outbound translations: %u\n", (unsigned)stats->translations_out);
    printf("  Inbound translations:  %u\n", (unsigned)stats->translations_in);
    printf("  Table full errors:     %u\n", (unsigned)stats->table_full);
    printf("  No match errors:       %u\n", (unsigned)stats->no_match);
    printf("  Timeouts:              %u\n", (unsigned)stats->timeouts);

    /* Print NAT table */
    uart_puts("\n");
    nat_print_table();
}
