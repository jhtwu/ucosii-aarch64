#include <includes.h>
#include <asm/types.h>
#include <bsp.h>
#include <bsp_os.h>
#include <portable_libc.h>
#include <virtio_net.h>
#include <net.h>

#define NET_TASK_PRIO    5u
#define NET_STACK_SIZE   4096u

static OS_STK net_task_stack[NET_STACK_SIZE];

extern int eth_init(void);

static void net_init_task(void *p_arg)
{
    INT8U err;

    (void)p_arg;

    printf("[BOOT] Starting scheduler\n\n");

    err = eth_init();
    if (err <= 0) {
        printf("[FAIL] eth_init() failed (code=%d)\n", err);
        goto wait_forever;
    }

    size_t device_count = virtio_net_get_device_count();
    if (device_count == 0u) {
        printf("[FAIL] No VirtIO network devices registered\n");
        goto wait_forever;
    }

    for (size_t i = 0u; i < device_count; ++i) {
        struct virtio_net_dev *dev = virtio_net_get_device(i);
        if (dev == NULL) {
            printf("[FAIL] virtio_net_get_device(%zu) returned NULL\n", i);
            goto wait_forever;
        }

        u32 status = virtio_mmio_read(dev, VIRTIO_MMIO_STATUS);
        if ((status & VIRTIO_STATUS_DRIVER_OK) == 0u) {
            printf("[FAIL] Device %zu status=0x%x (expected DRIVER_OK)\n",
                   i, status);
            goto wait_forever;
        }
    }

    printf("[PASS] VirtIO network initialization succeeded\n");

wait_forever:
    for (;;) {
        OSTimeDlyHMSM(0u, 0u, 1u, 0u);
    }
}

int main(void)
{
    INT8U err;

    printf("\n========================================\n");
    printf("Test Case: VirtIO Network Init Only\n");
    printf("========================================\n");

    CPU_Init();
    Mem_Init();
    BSP_Init();

    OSInit();

    err = OSTaskCreate(net_init_task,
                       0,
                       &net_task_stack[NET_STACK_SIZE - 1u],
                       NET_TASK_PRIO);
    if (err != OS_ERR_NONE) {
        printf("[ERROR] Failed to create net_init_task (err=%u)\n", err);
        return 1;
    }

    __asm__ volatile("msr daifclr, #0x2");

    OSStart();

    return 0;
}

