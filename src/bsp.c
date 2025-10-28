/*
*********************************************************************************************************
*
*                                    MICRIUM BOARD SUPPORT PACKAGE
*
*                          (c) Copyright 2003-2015; Micrium, Inc.; Weston, FL
*
*               All rights reserved.  Protected by international copyright laws.
*
*               This BSP is provided in source form to registered licensees ONLY.  It is
*               illegal to distribute this source code to any third party unless you receive
*               written permission by an authorized Micrium representative.  Knowledge of
*               the source code may NOT be used to develop a similar product.
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
*                                    MICRIUM BOARD SUPPORT PACKAGE
*                                              IMX6SX-SDB
*
* Filename      : bsp.c
* Version       : V1.00
* Programmer(s) : JBL
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                             INCLUDE FILES
*********************************************************************************************************
*/

#include  <cpu.h>
#include  <lib_def.h>

#include  <bsp.h>
#include  <bsp_int.h>

#include  "gic.h"

void gic_v3_init(void);


/*
*********************************************************************************************************
*                                             LOCAL DEFINES
*********************************************************************************************************
*/

#define  IMX6_REG_AIPSTZ1_MPR0    (*((CPU_REG32 *)0x0207C000))
#define  IMX6_REG_AIPSTZ1_MPR1    (*((CPU_REG32 *)0x0207C004))
#define  IMX6_REG_AIPSTZ1_OPACR0  (*((CPU_REG32 *)0x0207C040))
#define  IMX6_REG_AIPSTZ1_OPACR1  (*((CPU_REG32 *)0x0207C044))
#define  IMX6_REG_AIPSTZ1_OPACR2  (*((CPU_REG32 *)0x0207C048))
#define  IMX6_REG_AIPSTZ1_OPACR3  (*((CPU_REG32 *)0x0207C04C))
#define  IMX6_REG_AIPSTZ1_OPACR4  (*((CPU_REG32 *)0x0207C050))

#define  IMX6_REG_AIPSTZ2_MPR0    (*((CPU_REG32 *)0x0217C000))
#define  IMX6_REG_AIPSTZ2_MPR1    (*((CPU_REG32 *)0x0217C004))
#define  IMX6_REG_AIPSTZ2_OPACR0  (*((CPU_REG32 *)0x0217C040))
#define  IMX6_REG_AIPSTZ2_OPACR1  (*((CPU_REG32 *)0x0217C044))
#define  IMX6_REG_AIPSTZ2_OPACR2  (*((CPU_REG32 *)0x0217C048))
#define  IMX6_REG_AIPSTZ2_OPACR3  (*((CPU_REG32 *)0x0217C04C))
#define  IMX6_REG_AIPSTZ2_OPACR4  (*((CPU_REG32 *)0x0217C050))

#define  IMX6_REG_AIPSTZ3_MPR0    (*((CPU_REG32 *)0x0227C000))
#define  IMX6_REG_AIPSTZ3_MPR1    (*((CPU_REG32 *)0x0227C004))
#define  IMX6_REG_AIPSTZ3_OPACR0  (*((CPU_REG32 *)0x0227C040))
#define  IMX6_REG_AIPSTZ3_OPACR1  (*((CPU_REG32 *)0x0227C044))
#define  IMX6_REG_AIPSTZ3_OPACR2  (*((CPU_REG32 *)0x0227C048))
#define  IMX6_REG_AIPSTZ3_OPACR3  (*((CPU_REG32 *)0x0227C04C))
#define  IMX6_REG_AIPSTZ3_OPACR4  (*((CPU_REG32 *)0x0227C050))

#define  IMX6_REG_CSU_CSL(ix) (*((CPU_REG32 *)(0x021C0000 + (ix * 0x04))))
#define  IMX6_REG_CSU_HP0     (*((CPU_REG32 *)0x02100200))
#define  IMX6_REG_CSU_HP1     (*((CPU_REG32 *)0x02100204))


/*
*********************************************************************************************************
*********************************************************************************************************
*                                            GLOBAL FUNCTIONS
*********************************************************************************************************
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*                                              BSP_Init()
*
* Description : Initialise the BSP.
*
* Argument(s) : none.
*
* Return(s)   : none.
*
* Caller(s)   : Application.
*
* Note(s)     : none.
*
*********************************************************************************************************
*/

void  BSP_Init (void)
{
	CPU_INT32U i;
	uart_puts("BSP_Init\n");

#if 0 //ruby marked
	/* Disabled hardware permission checking for all slave. */
	for (i = 0; i <= 39; i++) {
		IMX6_REG_CSU_CSL(i) = 0xFFFFFFFFu;
	}

	/* Configure all masters as priviledged. (CSU-side) */
	//IMX6_REG_CSU_HP0 = 0xFFFFFFFFu;
	//IMX6_REG_CSU_HP1 = 0xFFFFFFFFu;

	/* Configure all masters as priviledged & non-bufferable. (AIPS-side) */
	IMX6_REG_AIPSTZ1_MPR0 = 0x77777777;
	IMX6_REG_AIPSTZ1_MPR1 = 0x77777777;
	IMX6_REG_AIPSTZ2_MPR0 = 0x77777777;
	IMX6_REG_AIPSTZ2_MPR1 = 0x77777777;
	IMX6_REG_AIPSTZ3_MPR0 = 0x77777777;
	IMX6_REG_AIPSTZ3_MPR1 = 0x77777777;

	/* Allow access to all slaves. (AIPS-side) */
	IMX6_REG_AIPSTZ1_OPACR0 = 0x0;
	IMX6_REG_AIPSTZ1_OPACR1 = 0x0;
	IMX6_REG_AIPSTZ1_OPACR2 = 0x0;
	IMX6_REG_AIPSTZ1_OPACR3 = 0x0;
	IMX6_REG_AIPSTZ1_OPACR4 = 0x0;

	IMX6_REG_AIPSTZ2_OPACR0 = 0x0;
	IMX6_REG_AIPSTZ2_OPACR1 = 0x0;
	IMX6_REG_AIPSTZ2_OPACR2 = 0x0;
	IMX6_REG_AIPSTZ2_OPACR3 = 0x0;
	IMX6_REG_AIPSTZ2_OPACR4 = 0x0;

	IMX6_REG_AIPSTZ3_OPACR0 = 0x0;
	IMX6_REG_AIPSTZ3_OPACR1 = 0x0;
	IMX6_REG_AIPSTZ3_OPACR2 = 0x0;
	IMX6_REG_AIPSTZ3_OPACR3 = 0x0;
	IMX6_REG_AIPSTZ3_OPACR4 = 0x0;
#endif
	BSP_Int_Init();
	if (BSP_Int_GICVariantGet() == 2u) {
		GIC_Enable();
	} else {
		gic_v3_init();
	}
	BSP_OS_UARTInit();
	return;
}


/*
*********************************************************************************************************
*                                           BSP_CPU_ClkFreq()
*
* Description : Return the CPU clock frequency.
*
* Argument(s) : none.
*
* Return(s)   : CPU clock frequency in Hz.
*
* Caller(s)   : Various.
*
* Note(s)     : Currently hard coded in this example.
*
*********************************************************************************************************
*/

CPU_INT32U  BSP_CPU_ClkFreq (void)
{
    return (APU_FREQ);
}


/*
*********************************************************************************************************
*                                         BSP_Periph_ClkFreq()
*
* Description : Return the private peripheral clock frequency.
*
* Argument(s) : none.
*
* Return(s)   : Clock frequency in Hz.
*
* Caller(s)   : Various.
*
* Note(s)     : Currently hard coded in this example.
*
*********************************************************************************************************
*/

CPU_INT32U  BSP_Periph_ClkFreq (void)
{
    return (PERIPH_FREQ);
}



/*
*********************************************************************************************************
*                                            BSP_LED_Init()
*
* Description : Initialise user LEDs.
*
* Argument(s) : none.
*
* Return(s)   : none.
*
* Caller(s)   : BSP_Init().
*
* Note(s)     : none.
*
*********************************************************************************************************
*/

void  BSP_LED_Init (void)
{


}


/*
*********************************************************************************************************
*                                             BSP_LED_On()
*
* Description : Turn ON a led.
*
* Argument(s) : led     led number.
*
* Return(s)   : none..
*
* Caller(s)   : Application.
*
* Note(s)     : none.
*
*********************************************************************************************************
*/

void  BSP_LED_On   (CPU_INT32U led)
{
}


/*
*********************************************************************************************************
*                                             BSP_LED_Off()
*
* Description : Turn OFF a led.
*
* Argument(s) : led     led number.
*
* Return(s)   : none..
*
* Caller(s)   : Application.
*
* Note(s)     : none.
*
*********************************************************************************************************
*/

void  BSP_LED_Off  (CPU_INT32U led)
{
}

