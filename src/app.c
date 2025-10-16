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
#include  <stddef.h>
#include  "virtio_net.h"
#include  "net_ping.h"
#include  "nat.h"

/* Enable NAT functionality - Full NAT Router */
#define ENABLE_NAT 1

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
extern struct eth_device *ethdev;
extern struct virtio_net_dev *virtio_net_device;
extern void net_enable_nat(void);

static const struct net_ping_target app_ping_targets[] = {
    {
        .name = "LAN",
        .guest_ip = {192u, 168u, 1u, 1u},
        .host_ip = {192u, 168u, 1u, 103u},
        .device_index = 0u,
    },
    {
        .name = "WAN",
        .guest_ip = {10u, 3u, 5u, 99u},
        .host_ip = {10u, 3u, 5u, 103u},
        .device_index = 1u,
    },
};

static void  AppTaskNetwork (void *p_arg)
{
    int rc;

    (void)p_arg;

    uart_puts("AppTaskNetwork init\n");

    uart_puts("Initializing network driver...\n");
    rc = eth_init();

    if (rc <= 0) {
        uart_puts("ERROR: Network initialization failed!\n");
        while (DEF_TRUE) {
            OSTimeDlyHMSM(0, 0, 5, 0);
        }
    }

    uart_puts("Network driver initialized successfully!\n");

    BSP_OS_TmrTickInit(1000);

#if ENABLE_NAT
    /* Enable NAT - Full Router Mode */
    uart_puts("\n========================================\n");
    uart_puts("NAT Router Mode\n");
    uart_puts("========================================\n");
    uart_puts("[NAT] Initializing NAT router...\n");
    net_enable_nat();
    uart_puts("[NAT] NAT router enabled for ICMP/TCP/UDP\n");
    uart_puts("[INFO] LAN: 192.168.1.1/24 -> WAN: 10.3.5.99\n");
    uart_puts("[INFO] Ready to forward traffic from LAN to WAN\n\n");
#endif

    for (size_t i = 0u; i < (sizeof(app_ping_targets) / sizeof(app_ping_targets[0])); ++i) {
        if (net_ping_run(&app_ping_targets[i], 4u, NULL) != 0) {
            printf("[FAIL] %s ping test encountered an error\n",
                   (app_ping_targets[i].name != NULL) ? app_ping_targets[i].name : "Network");
            while (DEF_TRUE) {
                OSTimeDlyHMSM(0, 0, 5, 0);
            }
        }
    }

#if ENABLE_NAT
    /* Print initial NAT statistics */
    uart_puts("\n========================================\n");
    uart_puts("NAT Statistics After Initialization\n");
    uart_puts("========================================\n");
    nat_print_table();
    uart_puts("\n[INFO] NAT router is running\n");
#endif

    uart_puts("Network initialization completed.\n");
    uart_puts("NAT router is ready to forward traffic.\n");

    while (DEF_TRUE) {
        OSTimeDlyHMSM(0, 0, 5, 0);
    }
}
