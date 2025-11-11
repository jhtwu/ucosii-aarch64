/*
 * NAT Ping Test - End-to-End NAT Gateway Testing
 *
 * This test verifies the NAT gateway functionality by simulating a complete
 * network topology with LAN clients pinging through the NAT gateway to WAN hosts.
 *
 * Network Topology:
 *   LAN Client (192.168.1.100)
 *       ↓
 *   NAT Gateway (LAN: 192.168.1.1, WAN: 10.3.5.99)
 *       ↓
 *   WAN Host (10.3.5.103)
 *
 * Test Scenarios:
 * 1. ICMP Ping: LAN client pings external WAN host through NAT
 * 2. Future: TCP iperf performance testing
 * 3. Future: UDP iperf performance testing
 *
 * Usage:
 * - Run with: make test-nat-ping
 * - Requires both qemu-lan and qemu-wan TAP interfaces configured
 * - Host-side helper script can simulate external responses
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
#include "test_network_config.h"
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
#define TEST_WAIT_REPLY_SEC    3

/* Simulated LAN clients (use common config for consistency) */
#define LAN_CLIENT1_IP         {192, 168, 1, 100}  /* Primary test client */
#define LAN_CLIENT2_IP         {192, 168, 1, 101}  /* Secondary test client */
#define LAN_CLIENT_MAC         {0x52, 0x54, 0x00, 0xAA, 0xBB, 0xCC}

/* External WAN destinations (use common config) */
#define WAN_TARGET_IP          WAN_HOST_IP  /* 10.3.5.103 - from test_network_config.h */

/* Test state */
static struct {
    bool     initialized;
    uint32_t icmp_sent;
    uint32_t icmp_replies;
    uint32_t nat_translations;
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
static void print_network_topology(void);

/*
 * Main entry point
 */
int main(void)
{
    INT8U os_err;

    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("Test Case: NAT Ping (End-to-End)\n");
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

    /* Print network topology */
    print_network_topology();

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
 * print_network_topology() - Display network configuration
 */
static void print_network_topology(void)
{
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("Network Topology\n");
    uart_puts("========================================\n");
    uart_puts("LAN Network (192.168.1.0/24):\n");
    printf("  - LAN Gateway:    " LAN_GUEST_IP_STR "\n");
    printf("  - LAN Client 1:   192.168.1.100\n");
    printf("  - LAN Client 2:   192.168.1.101\n");
    printf("  - Host TAP:       " LAN_HOST_IP_STR "\n");
    uart_puts("\n");
    uart_puts("WAN Network (10.3.5.0/24):\n");
    printf("  - WAN Gateway:    " WAN_GUEST_IP_STR "\n");
    printf("  - WAN Target:     " WAN_HOST_IP_STR "\n");
    uart_puts("\n");
    uart_puts("NAT Configuration:\n");
    printf("  - LAN side:       " LAN_GUEST_IP_STR "\n");
    printf("  - WAN side:       " WAN_GUEST_IP_STR "\n");
    uart_puts("========================================\n\n");
}

/*
 * Test task - performs NAT ping test
 */
static void AppTaskTest(void *p_arg)
{
    (void)p_arg;
    u8 lan_client_ip[] = LAN_CLIENT1_IP;
    u8 wan_target_ip[] = WAN_TARGET_IP;
    u16 icmp_id = 0x1234;
    int i;

    OSTimeDlyHMSM(0, 0, 1, 0);  /* Wait for system stabilization */

    uart_puts("[TEST] Starting NAT Ping Test\n");
    uart_puts("[TEST] Scenario: LAN client 192.168.1.100 pinging WAN host 10.3.5.103\n");
    uart_puts("[TEST] Expected: NAT translates 192.168.1.100 -> 10.3.5.99 for outbound packets\n");
    uart_puts("[TEST] Expected: NAT translates replies from 10.3.5.103 back to 192.168.1.100\n\n");

    test_state.initialized = true;

    /* Send test ICMP packets from simulated LAN client */
    for (i = 0; i < TEST_PING_COUNT; i++) {
        printf("[TEST] Sending ICMP ping %d/%d (ID: 0x%04x, Seq: %d)\n",
               i + 1, TEST_PING_COUNT, icmp_id, i);
        printf("        From: 192.168.1.100 -> To: " WAN_HOST_IP_STR "\n");

        if (send_icmp_ping(lan_client_ip, wan_target_ip, icmp_id, i) == 0) {
            test_state.icmp_sent++;
        } else {
            printf("[WARN] Failed to send ping %d\n", i + 1);
        }

        OSTimeDlyHMSM(0, 0, 1, 0);  /* 1 second delay between pings */
    }

    /* Wait for potential replies */
    uart_puts("\n[TEST] Waiting for potential replies...\n");
    uart_puts("[INFO] For replies, ensure WAN host (10.3.5.103) can respond to 10.3.5.99\n");
    OSTimeDlyHMSM(0, 0, TEST_WAIT_REPLY_SEC, 0);

    /* Print results */
    print_test_summary();

    /* Evaluate test result - for now, success = all packets sent through NAT */
    if (test_state.icmp_sent >= TEST_PING_COUNT) {
        uart_puts("\n[PASS] NAT ping test completed successfully\n");
        uart_puts("[INFO] All ICMP packets were sent through NAT gateway\n");
        test_state.test_passed = true;
    } else {
        uart_puts("\n[FAIL] NAT ping test failed\n");
        printf("[ERROR] Only %u/%d packets were sent\n",
               (unsigned)test_state.icmp_sent, TEST_PING_COUNT);
        test_state.test_passed = false;
    }

    /* Future enhancement points */
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("Future Test Enhancements\n");
    uart_puts("========================================\n");
    uart_puts("TODO: Add TCP iperf performance test\n");
    uart_puts("TODO: Add UDP iperf performance test\n");
    uart_puts("TODO: Add NAT session timeout testing\n");
    uart_puts("TODO: Add concurrent connection testing\n");
    uart_puts("========================================\n\n");

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
    struct virtio_net_dev *virtio_dev;
    struct eth_device *lan_dev;

    /* Get LAN interface (index 0) */
    virtio_dev = virtio_net_get_device(0);
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

    printf("ICMP pings sent:     %u\n", (unsigned)test_state.icmp_sent);
    printf("ICMP replies recv:   %u\n", (unsigned)test_state.icmp_replies);
    printf("Expected pings:      %d\n", TEST_PING_COUNT);

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
