#include <includes.h>
#include <asm/types.h>
#include <bsp.h>
#include <bsp_os.h>
#include <portable_libc.h>
#include <net.h>
#include <virtio_net.h>

#include <stdbool.h>

#include "net_ping.h"

#define NET_TASK_PRIO    5u
#define NET_STACK_SIZE   4096u

static OS_STK net_task_stack[NET_STACK_SIZE];

// Use QEMU user-mode networking default gateway
// Guest IP: 10.0.2.15 (QEMU user-mode default)
// Host IP: 10.0.2.2 (QEMU user-mode gateway)
static const struct net_ping_target lan_target = {
    .name = "LAN Gateway",
    .guest_ip = {10u, 0u, 2u, 15u},
    .host_ip = {10u, 0u, 2u, 2u},
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

    printf("Testing LAN connectivity by pinging gateway at 10.0.2.2\n");
    if (net_ping_run(&lan_target, 4u, NULL) == 0) {
        printf("[PASS] LAN ping test completed - Gateway reachable\n");
    } else {
        printf("[FAIL] LAN ping test failed - Gateway unreachable\n");
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
    printf("Test Case: LAN Ping (User-mode Network)\n");
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
