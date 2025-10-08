/*
*********************************************************************************************************
*                                            EXAMPLE CODE
*
*                          (c) Copyright 2009-2015; Micrium, Inc.; Weston, FL
*
*               All rights reserved.  Protected by international copyright laws.
*
*               Please feel free to use any application code labeled as 'EXAMPLE CODE' in
*               your application products.  Example code may be used as is, in whole or in
*               part, or may be used as a reference only.
*
*               Please help us continue to provide the Embedded community with the finest
*               software available.  Your honesty is greatly appreciated.
*
*               You can contact us at www.micrium.com.
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*
*                                          APPLICATION CODE
*
*                                             IMX6SX-SDB
*
* Filename      : app.c
* Version       : V1.00
* Programmer(s) : JBL
*********************************************************************************************************
* Note(s)       : none.
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                            INCLUDE FILES
*********************************************************************************************************
*/


#include  <bsp.h>
#include  <bsp_os.h>
#include  <cpu.h>
#include  <cpu_core.h>
#include  <lib_mem.h>
#include  <os.h>
#include  <includes.h>
#include  <asm/types.h>
#include  <net.h>
#include  "virtio_net.h"

/*
*********************************************************************************************************
*                                       LOCAL GLOBAL VARIABLES
*********************************************************************************************************
*/

#define  APP_CFG_TASK_START_PRIO      2u
#define  APP_CFG_TASK_START_STK_SIZE  4096

static  OS_STK  AppTaskStartStk[APP_CFG_TASK_START_STK_SIZE];
static  OS_STK  AppTaskPrintStk[APP_CFG_TASK_START_STK_SIZE];
static  OS_STK  AppTaskNetworkStk[APP_CFG_TASK_START_STK_SIZE];

/*
*********************************************************************************************************
*                                      LOCAL FUNCTION PROTOTYPES
*********************************************************************************************************
*/

static  void  AppTaskStart   (void *p_arg);
static  void  AppTaskPrint   (void *p_arg);
static  void  AppTaskNetwork (void *p_arg);

/*
*********************************************************************************************************
*                                               main()
*
* Description : Entry point for C code.
*
* Arguments   : none.
*
* Returns     : none.
*********************************************************************************************************
*/
int main (void)
{
    INT8U os_err;

    uart_puts("main\n");

    CPU_Init();
    Mem_Init();
    BSP_Init();

    OSInit();

    os_err = OSTaskCreateExt(AppTaskStart,
                             0,
                             &AppTaskStartStk[APP_CFG_TASK_START_STK_SIZE - 1],
                             APP_CFG_TASK_START_PRIO,
                             APP_CFG_TASK_START_PRIO,
                             &AppTaskStartStk[0],
                             APP_CFG_TASK_START_STK_SIZE,
                             0,
                             OS_TASK_OPT_STK_CLR | OS_TASK_OPT_STK_CHK);

    if (os_err != OS_ERR_NONE) {
        return -1;
    }

    OSStart();

    return 0;
}
/*
*********************************************************************************************************
*                                           App_TaskStart()
*
* Description : Startup task example code.
*
* Arguments   : p_arg       Argument passed by 'OSTaskCreate()'.
*
* Returns     : none.
*
* Created by  : main().
*
* Notes       : (1) The ticker MUST be initialised AFTER multitasking has started.
*********************************************************************************************************
*/

static void  AppTaskStart (void *p_arg)
{
    (void)p_arg;

    uart_puts("AppTaskStart init\n");

    OSTaskCreateExt(AppTaskPrint,
                    0,
                    &AppTaskPrintStk[APP_CFG_TASK_START_STK_SIZE - 1],
                    APP_CFG_TASK_START_PRIO + 1,
                    APP_CFG_TASK_START_PRIO + 1,
                    &AppTaskPrintStk[0],
                    APP_CFG_TASK_START_STK_SIZE,
                    0,
                    OS_TASK_OPT_STK_CLR | OS_TASK_OPT_STK_CHK);

    OSTaskCreateExt(AppTaskNetwork,
                    0,
                    &AppTaskNetworkStk[APP_CFG_TASK_START_STK_SIZE - 1],
                    APP_CFG_TASK_START_PRIO + 2,
                    APP_CFG_TASK_START_PRIO + 2,
                    &AppTaskNetworkStk[0],
                    APP_CFG_TASK_START_STK_SIZE,
                    0,
                    OS_TASK_OPT_STK_CLR | OS_TASK_OPT_STK_CHK);

    BSP_OS_TmrTickInit(1000);

    while (DEF_TRUE) {
        uart_puts("Task 1: tick\n");
        OSTimeDlyHMSM(0, 0, 1, 0);
    }
}

static void  AppTaskPrint (void *p_arg)
{
    (void)p_arg;

    uart_puts("AppTaskPrint init\n");

    while (DEF_TRUE) {
        uart_puts("Task 2: tick\n");
        OSTimeDlyHMSM(0, 0, 1, 0);
    }
}

extern int eth_init(void);
extern void send_icmp(void);
extern struct eth_device *ethdev;
extern struct virtio_net_dev *virtio_net_device;

static void  AppTaskNetwork (void *p_arg)
{
    int rc;
    int test_count = 0;
    struct eth_device *dev;

    (void)p_arg;

    uart_puts("AppTaskNetwork init\n");

    /* Initialize network driver (VirtIO Net or SMC911x) */
    uart_puts("Initializing network driver...\n");
    rc = eth_init();

    if (rc <= 0) {
        uart_puts("ERROR: Network initialization failed!\n");
        while (DEF_TRUE) {
            OSTimeDlyHMSM(0, 0, 5, 0);
        }
    }

    uart_puts("Network driver initialized successfully!\n");

    /* Get device pointer - check both VirtIO and SMC911x */
#ifdef CONFIG_VIRTIO_NET
    if (virtio_net_device) {
        dev = &virtio_net_device->eth_dev;
        uart_puts("Using VirtIO Net device\n");
    } else
#endif
    if (ethdev) {
        dev = ethdev;
        uart_puts("Using SMC911x device\n");
    } else {
        uart_puts("ERROR: No network device found!\n");
        while (DEF_TRUE) {
            OSTimeDlyHMSM(0, 0, 5, 0);
        }
    }

    uart_puts("Network TX/RX test starting...\n");

    /* Wait for network to stabilize */
    OSTimeDlyHMSM(0, 0, 2, 0);

    while (DEF_TRUE) {
        test_count++;

        uart_puts("\n=== Network Test Iteration ");
        printf("%d", test_count);
        uart_puts(" ===\n");

        /* Test TX: Send ARP request packet */
        uart_puts("Testing TX: Sending ARP request packet...\n");
        if (dev->send) {
            send_icmp();
        } else {
            uart_puts("ERROR: send function not available\n");
        }

        uart_puts("TX test completed. Packet sent.\n");
        uart_puts("RX: Waiting for incoming packets (handled by interrupt)...\n");

        /* Wait 5 seconds between tests */
        OSTimeDlyHMSM(0, 0, 5, 0);
    }
}
