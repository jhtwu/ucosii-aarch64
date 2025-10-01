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


#include  <stdio.h>
#include  <string.h>
#include  <stdarg.h>
#include  <app_cfg.h>
#include  <lib_mem.h>

#include  <bsp.h>
#include  <bsp_int.h>
#include  <bsp_os.h>
#include  <bsp_cache.h>
#include  <bsp_ser.h>

#include  <cpu.h>
#include  <cpu_core.h>
#include  <cpu_cache.h>

#include  <os.h>
#include  <includes.h>
#include <sys/stat.h>
#include <stddef.h>
#include <stdlib.h> 

/*
*********************************************************************************************************
*                                       LOCAL GLOBAL VARIABLES
*********************************************************************************************************
*/

#define  APP_CFG_TASK_START_PRIO                          2u
#define APP_CFG_TASK_START_STK_SIZE 4096
static  OS_STK       AppTaskStartStk[APP_CFG_TASK_START_STK_SIZE];
static  OS_STK       AppTaskStartStk2[APP_CFG_TASK_START_STK_SIZE];
static  OS_STK       AppTaskStartStk3[APP_CFG_TASK_START_STK_SIZE];
static  OS_STK       AppTaskStartStk4[APP_CFG_TASK_START_STK_SIZE];
static  OS_STK       AppTaskStartStk5[APP_CFG_TASK_START_STK_SIZE];

/*
*********************************************************************************************************
*                                      LOCAL FUNCTION PROTOTYPES
*********************************************************************************************************
*/

static  void  AppTaskStart              (void        *p_arg);
static  void  AppTaskStart2              (void        *p_arg);
static  void  AppTaskStart3              (void        *p_arg);
static  void  AppTaskStart4              (void        *p_arg);
static  void  AppTaskStart5              (void        *p_arg);
static  void  AppTaskCreate             (void);

extern const char stackSTART __attribute__((section(".stack")));
extern const char stackEND __attribute__((section(".stack")));
extern const char stackSVC_START __attribute__((section(".stack")));
extern const char stackSVC_END __attribute__((section(".stack")));
extern const char stackUND_START __attribute__((section(".stack")));
extern const char stackUND_END __attribute__((section(".stack")));
extern const char stackABT_START __attribute__((section(".stack")));
extern const char stackABT_END __attribute__((section(".stack")));
extern const char stackIRQ_START __attribute__((section(".stack")));
extern const char stackIRQ_END __attribute__((section(".stack")));
extern const char stackFIQ_START __attribute__((section(".stack")));
extern const char stackFIQ_END __attribute__((section(".stack")));
extern const char stackUSR_START __attribute__((section(".stack")));
extern const char stackUSR_END __attribute__((section(".stack")));
void dump_stack()
{

    static const char * ptr=&stackSTART;  /* pointer to head of heap */
	uart_puts("stackSTART at 0x%x-",ptr);
	ptr=&stackEND; uart_puts("0x%x\n",ptr);
	ptr=&stackSVC_START; uart_puts("stackSVC_START at 0x%x-",ptr);
	ptr=&stackSVC_END; uart_puts("0x%x\n",ptr);
	ptr=&stackUND_START; uart_puts("stackUND_START at 0x%x-",ptr);
	ptr=&stackUND_END; uart_puts("0x%x\n",ptr);
	ptr=&stackABT_START; uart_puts("stackABT_START at 0x%x-",ptr);
	ptr=&stackABT_END; uart_puts("0x%x\n",ptr);
	ptr=&stackIRQ_START; uart_puts("stackIRQ_START at 0x%x-",ptr);
	ptr=&stackIRQ_END; uart_puts("0x%x\n",ptr);
	ptr=&stackFIQ_START; uart_puts("stackFIQ_START at 0x%x-",ptr);
	ptr=&stackFIQ_END; uart_puts("0x%x\n",ptr);
	ptr=&stackUSR_START; uart_puts("stackUSR_START at 0x%x-",ptr);
	ptr=&stackUSR_END; uart_puts("0x%x\n",ptr);
	
}

static  void  AppTaskCreate (void)
{


	// uart_puts("-----AppTaskCreate, create task AppTaskStart2 ----\n");
	OSTaskCreateExt((void (*)(void *)) AppTaskStart2,           /* Create the start task                                */
                    (void           *) 0,
                    (OS_STK         *)&AppTaskStartStk2[APP_CFG_TASK_START_STK_SIZE - 1],
                    (INT8U           ) APP_CFG_TASK_START_PRIO+1,
                    (INT16U          ) APP_CFG_TASK_START_PRIO+1,
                    (OS_STK         *)&AppTaskStartStk2[0],
                    (INT32U          ) APP_CFG_TASK_START_STK_SIZE,
                    (void           *) 0,
                    (INT16U          )(OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR));
	OSTaskCreateExt((void (*)(void *)) AppTaskStart3,           /* Create the start task                                */
                    (void           *) 0,
                    (OS_STK         *)&AppTaskStartStk3[APP_CFG_TASK_START_STK_SIZE - 1],
                    (INT8U           ) APP_CFG_TASK_START_PRIO+2,
                    (INT16U          ) APP_CFG_TASK_START_PRIO+2,
                    (OS_STK         *)&AppTaskStartStk3[0],
                    (INT32U          ) APP_CFG_TASK_START_STK_SIZE,
                    (void           *) 0,
                    (INT16U          )(OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR));

	OSTaskCreateExt((void (*)(void *)) AppTaskStart4,           /* Create the start task                                */
                    (void           *) 0,
                    (OS_STK         *)&AppTaskStartStk4[APP_CFG_TASK_START_STK_SIZE - 1],
                    (INT8U           ) APP_CFG_TASK_START_PRIO+3,
                    (INT16U          ) APP_CFG_TASK_START_PRIO+3,
                    (OS_STK         *)&AppTaskStartStk4[0],
                    (INT32U          ) APP_CFG_TASK_START_STK_SIZE,
                    (void           *) 0,
                    (INT16U          )(OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR));

	OSTaskCreateExt((void (*)(void *)) AppTaskStart5,           /* Create the start task                                */
                    (void           *) 0,
                    (OS_STK         *)&AppTaskStartStk5[APP_CFG_TASK_START_STK_SIZE - 1],
                    (INT8U           ) APP_CFG_TASK_START_PRIO+4,
                    (INT16U          ) APP_CFG_TASK_START_PRIO+4,
                    (OS_STK         *)&AppTaskStartStk5[0],
                    (INT32U          ) APP_CFG_TASK_START_STK_SIZE,
                    (void           *) 0,
                    (INT16U          )(OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR));
}

void test()
{
	unsigned long long value=0x8000000000;
	uart_puts("=======================================\n");
	uart_puts("&value=%x\n",&value);
	uart_puts("%x\n",value);
	uart_puts("%x\n",(long) value);
	uart_puts("%llx\n",value);

	exit(0);
}

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
int main ()
{
    INT8U os_err;
	uart_puts("main\n");
	// dump(__func__,__LINE__);
	// dump_stack();

	//Ruby added
	//tick_init();
                
	//Ruby marked                                                /* Scatter loading is complete. Now the caches can be activated.*/
    //BSP_BranchPredictorEn();                                    /* Enable branch prediction.                            */
    //BSP_L2C310Config();                                         /* Configure the L2 cache controller.                   */
    //BSP_CachesEn();                                             /* Enable L1 I&D caches + L2 unified cache.             */

	// test();

    CPU_Init();

    Mem_Init();

    BSP_Init();


	// dump(__func__,__LINE__);
    OSInit();

    os_err = OSTaskCreateExt((void (*)(void *)) AppTaskStart,   /* Create the start task.                               */
                             (void          * ) 0,
                             (OS_STK        * )&AppTaskStartStk[4096 - 1],
                             (INT8U           ) APP_CFG_TASK_START_PRIO,
                             (INT16U          ) APP_CFG_TASK_START_PRIO,
                             (OS_STK        * )&AppTaskStartStk[0],
                             (INT32U          ) 4096,
                             (void          * )0,
                             (INT16U          )(OS_TASK_OPT_STK_CLR | OS_TASK_OPT_STK_CHK));

    if (os_err != OS_ERR_NONE) {
        ; /* Handle error. */
    }
    //CPU_IntEn();

    OSStart();
}



void delay(volatile int count)
{
    count *= 50000;
    while (count--);
}

void busy_loop(void *str)
{
    while (1) {
        uart_puts(str);
        uart_puts(": Running...\n");
        delay(5000);
    }
}

static  void  AppTaskStart2 (void *p_arg)
{
	CPU_INT32U  cpu_clk_freq;
	CPU_INT32U  cnts;


	(void)p_arg;
	// uart_puts("AppTaskStart2-----------------\n");
	uart_puts("create AppTaskStart2 ...\n");

	while (DEF_TRUE) {                                          /* Task body, always written as an infinite loop.       */
		// uart_puts( CYAN "\t\t\t[%s:%d] 33333333333333  AppTask2 OSTimeDlyHMSM 1sec\n" NONE, __func__,__LINE__);
	//	delay(4000);
		uart_puts(LIGHT_CYAN "in AppTaskStart2 while , now calling delay! \n" NONE);
		OSTimeDlyHMSM(0, 0, 0, 200);                            /* Delay for 100 milliseconds                           */
	}
}

static  void  AppTaskStart3 (void *p_arg)
{
	CPU_INT32U  cpu_clk_freq;
	CPU_INT32U  cnts;


	(void)p_arg;
	// uart_puts("AppTaskStart2-----------------\n");
	uart_puts("create AppTaskStart3 ...\n");

	while (DEF_TRUE) {                                          /* Task body, always written as an infinite loop.       */
		// uart_puts( CYAN "\t\t\t[%s:%d] 33333333333333  AppTask2 OSTimeDlyHMSM 1sec\n" NONE, __func__,__LINE__);
	//	delay(4000);
		uart_puts(LIGHT_PURPLE "in AppTaskStart3 while......, now calling delay ! \n" NONE);
		OSTimeDlyHMSM(0, 0, 0, 300);                            /* Delay for 100 milliseconds                           */
	}
}
static  void  AppTaskStart4 (void *p_arg)
{
	CPU_INT32U  cpu_clk_freq;
	CPU_INT32U  cnts;


	(void)p_arg;
	// uart_puts("AppTaskStart2-----------------\n");
	uart_puts("create AppTaskStart4 ...\n");

	while (DEF_TRUE) {                                          /* Task body, always written as an infinite loop.       */
		// uart_puts( CYAN "\t\t\t[%s:%d] 33333333333333  AppTask2 OSTimeDlyHMSM 1sec\n" NONE, __func__,__LINE__);
	//	delay(4000);
		uart_puts(YELLOW "in AppTaskStart4 while, now calling delay!\n" NONE);
		OSTimeDlyHMSM(0, 0, 0, 400);                            /* Delay for 100 milliseconds                           */
	}
}
static  void  AppTaskStart5 (void *p_arg)
{
	CPU_INT32U  cpu_clk_freq;
	CPU_INT32U  cnts;


	(void)p_arg;
	// uart_puts("AppTaskStart2-----------------\n");
	uart_puts("create AppTaskStart5 ...\n");

	while (DEF_TRUE) {                                          /* Task body, always written as an infinite loop.       */
		// uart_puts( CYAN "\t\t\t[%s:%d] 33333333333333  AppTask2 OSTimeDlyHMSM 1sec\n" NONE, __func__,__LINE__);
	//	delay(4000);
		uart_puts(WHITE "  in AppTaskStart5 while, now calling delay!\n" NONE);
		OSTimeDlyHMSM(0, 0, 1, 1);                            /* Delay for 100 milliseconds                           */
	}
}

void test_libc(void)
{
	char array[100];
	printf("now calling memset\n");
	memset(array,0,sizeof(array));
	printf("strlen of (array) is %d, array is [%s]\n",strlen(array),array);

	char *ptr=malloc(100);
	memset(ptr,0,100);

	char *str="12345";
	char *str2="blabababa~~";

	strcpy(ptr,str);
	printf("strlen of ptr is %d, ptr is [%s]\n",strlen(ptr),ptr);

	strncat(ptr,str2,strlen(str2));
	printf("strlen of ptr is %d, ptr is [%s]\n",strlen(ptr),ptr);

	sprintf(array,"%s+I'm loving it!!",ptr);
	printf("strlen of (array) is %d, array is [%s]\n",strlen(array),array);

	free(ptr);

	char *data;
	short *sp1, *sp2;
	int *ip;
	data = (char *)malloc(16);

	printf("############## malloc() done , data=0x%x ###################\n",data);

	sp1 = (short*)(data+5);
	sp2 = (short*)(data+9);
	ip = (int*)(data+13);
	// sp1 = (short*)(data+4);
	// sp2 = (short*)(data+8);
	// ip = (int*)(data+12);
	*sp1=10;
	*sp2=20;
	*ip=30;

	printf("############## assign value done ###################\n");

	printf("sp1 at %x, value %d\nsp2 at %x, value %d\nip at %x , value %d\n", sp1, *sp1, sp2,  *sp2, ip, *ip);

	free(data);
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

#if 0
	char *ptr=malloc(1000);
    *ptr="1\0";

	uart_puts(ptr);
#endif

    //BSP_Ser_Init(); //Ruby marked
#if OS_CRITICAL_METHOD == 3u                               /* Allocate storage for CPU status register */
    OS_CPU_SR  cpu_sr = 0u;
#endif
	// uart_puts("AppTaskCreate...\n");
	uart_puts("create AppTaskCreate ...\n");
    AppTaskCreate();                                           /* Create application tasks                             */

	//ruby: put timer here to avoid timer continue to happen before task create
	BSP_OS_TmrTickInit(1000);
		// dump(__func__,__LINE__);

    // APP_TRACE_INFO(("\r\n"));
    // APP_TRACE_INFO(("Application start on the Cortex-A9!\r\n"));

       // uart_puts("====================================================\n");
       unsigned long long addr=0x3f000000;
       char *node_name="pcie";
       // uart_puts("[%s] %llx.%s\n",node_name, (unsigned long long)addr, node_name);

       addr=0x80000000000;
   
	    // uart_puts("[%s] %llx.%s\n",node_name, (unsigned long long)addr, node_name);

	for(;;) {

        OS_ENTER_CRITICAL();
		//dump(__func__,__LINE__);
		// uart_puts(YELLOW " [%s:%d] 11111111111111 before calling OSTimeDlyHMSM 1sec\n" NONE,__func__,__LINE__);
//		delay(4000);
	//	uart_puts("now calling OSTimeDlyHMSM((0, 0, 0, 10)\n");
#if 1
		test_libc();
#endif
        uart_puts( LIGHT_BLUE "in AppTask1  while, now calling delay!\n" NONE);
		OSTimeDlyHMSM(0, 0, 0, 100);
		// APP_TRACE_INFO(("Periodic output from the Cortex-A9\r\n"));
        OS_EXIT_CRITICAL();
		//	uart_puts(YELLOW " [%s:%d]\n" NONE,__func__,__LINE__);
	}

}
