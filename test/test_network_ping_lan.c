#include <includes.h>
#include <asm/types.h>
#include <bsp.h>
#include <bsp_os.h>
#include <portable_libc.h>
#include <net.h>
#include <virtio_net.h>

#include <stdbool.h>

#include "net_ping.h"
#include "test_network_config.h"

#define NET_TASK_PRIO    5u
#define NET_STACK_SIZE   4096u

static OS_STK net_task_stack[NET_STACK_SIZE];

// LAN network configuration (TAP bridge mode)
// Guest: 192.168.1.1 (ucOS-II in QEMU)
// Host:  192.168.1.103 (Linux TAP interface)
static const struct net_ping_target lan_target = {
    .name = "LAN",
    .guest_ip = LAN_GUEST_IP,
    .host_ip = LAN_HOST_IP,
    .device_index = 0u,
};

extern int eth_init(void);

static void net_test_task(void *p_arg)
{
    INT8U err;

    (void)p_arg;

    printf("[BOOT] Starting scheduler\n\n");

    err = eth_init();
    if (err <= 0) {
        printf("[FAIL] eth_init() failed\n");
        goto wait_forever;
    }

    BSP_OS_TmrTickInit(1000u);

    printf("Testing LAN connectivity (Guest: " LAN_GUEST_IP_STR " -> Host: " LAN_HOST_IP_STR ")\n");
    if (net_ping_run(&lan_target, 4u, NULL) == 0) {
        printf("[PASS] LAN ping test completed - " LAN_HOST_IP_STR " is reachable\n");
    } else {
        printf("[FAIL] LAN ping test failed - " LAN_HOST_IP_STR " is unreachable\n");
    }

wait_forever:
    for (;;) {
        OSTimeDlyHMSM(0u, 0u, 1u, 0u);
    }
}

int main(void)
{
    INT8U err;

    printf("\n========================================\n");
    printf("Test Case: LAN Ping (" LAN_GUEST_IP_STR " -> " LAN_HOST_IP_STR ")\n");
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

    OSStart();

    return 0;
}
