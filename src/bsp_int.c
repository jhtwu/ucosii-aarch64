/*
*********************************************************************************************************
*
*                                    MICRIUM BOARD SUPPORT PACKAGE
*
*                          (c) Copyright 2003-2016; Micrium, Inc.; Weston, FL
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
*                                    GENERIC INTERRUPT CONTROLLER
*
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                             INCLUDE FILES
*********************************************************************************************************
*/

#include  <cpu.h>
#include  <lib_def.h>

#include  "bsp.h"
#include  "bsp_int.h"

//Ruby mark
//#include  "../include/imx_defs.h"

/*
*********************************************************************************************************
*                                            LOCAL DEFINES
*********************************************************************************************************
*/

#ifdef VEXPRESS
#define  BSP_INT_GIC_DIST_REG         ((ARM_REG_GIC_DIST_PTR)(ARM_PRIV_PERIPH_BASE + 0x1000u))
#define  BSP_INT_GIC_IF_REG           ((ARM_REG_GIC_IF_PTR)(ARM_PRIV_PERIPH_BASE + 0x100u))
#define  BSP_INT_GIC_RDIST_SGI_BASE   ((ARM_REG_GIC_DIST_PTR)(ARM_PRIV_PERIPH_BASE + 0x1f000u))
#define  BSP_GIC_DIST_BASE_ADDR       (ARM_PRIV_PERIPH_BASE + 0x1000u)
#else /* VIRT */
#define  BSP_GIC_DIST_BASE_ADDR       (0x08000000u)
#define  BSP_GIC_CPU_IF_BASE_ADDR     (0x08010000u)
#define  BSP_GIC_RDIST_SGI_BASE_ADDR  (0x080b0000u)
#define  BSP_INT_GIC_DIST_REG         ((ARM_REG_GIC_DIST_PTR)(BSP_GIC_DIST_BASE_ADDR))
#define  BSP_INT_GIC_IF_REG           ((ARM_REG_GIC_IF_PTR)(BSP_GIC_CPU_IF_BASE_ADDR))
#define  BSP_INT_GIC_RDIST_SGI_BASE   ((ARM_REG_GIC_DIST_PTR)(BSP_GIC_RDIST_SGI_BASE_ADDR))
#endif

/*
*********************************************************************************************************
*                                       LOCAL GLOBAL VARIABLES
*********************************************************************************************************
*/

static  BSP_INT_FNCT_PTR BSP_IntVectTbl[ARM_GIC_INT_SRC_CNT];   /* Interrupt vector table.                              */
static  CPU_INT08U        BSP_GIC_Variant = 2u;                 /* Detected GIC variant (2 or 3).                       */

static void BSP_IntDetectVariant(void)
{
    CPU_INT32U typer = *((volatile CPU_REG32 *)(BSP_GIC_DIST_BASE_ADDR + 0x004u));

    if (typer & (1u << 19)) {
        BSP_GIC_Variant = 3u;
        uart_puts("Detected GICv3\n");
    } else {
        BSP_GIC_Variant = 2u;
        uart_puts("Detected GICv2\n");
    }
}

static CPU_INT32U intc_icdicfrn_table[] =
{
    0xa0a0a0a0,            /* ICDICFR0  :  15 to   0 */
    0xa0a0a0a0,            /* ICDICFR1  :  19 to  16 */
    0xa0a0a0a0,            /* ICDICFR2  :  47 to  32 */
    0xa0a0a0a0,            /* ICDICFR3  :  63 to  48 */
    0xa0a0a0a0,            /* ICDICFR4  :  79 to  64 */
    0xa0a0a0a0,            /* ICDICFR5  :  95 to  80 */
    0xa0a0a0a0,            /* ICDICFR6  : 111 to  96 */
    0xa0a0a0a0,            /* ICDICFR7  : 127 to 112 */
    0xa0a0a0a0,            /* ICDICFR8  : 143 to 128 */
    0xa0a0a0a0,            /* ICDICFR9  : 159 to 144 */
    0xa0a0a0a0,            /* ICDICFR10 : 175 to 160 */
    0xa0a0a0a0,            /* ICDICFR11 : 191 to 176 */
    0xa0a0a0a0,            /* ICDICFR12 : 207 to 192 */
    0xa0a0a0a0,            /* ICDICFR13 : 223 to 208 */
    0xa0a0a0a0,            /* ICDICFR14 : 239 to 224 */
    0xa0a0a0a0,            /* ICDICFR15 : 255 to 240 */
    0xa0a0a0a0,            /* ICDICFR16 : 271 to 256 */
    0xa0a0a0a0,            /* ICDICFR17 : 287 to 272 */
    0xa0a0a0a0,            /* ICDICFR18 : 303 to 288 */
    0xa0a0a0a0,            /* ICDICFR19 : 319 to 304 */
    0xa0a0a0a0,            /* ICDICFR20 : 335 to 320 */
    0xa0a0a0a0,            /* ICDICFR21 : 351 to 336 */
    0xa0a0a0a0,            /* ICDICFR22 : 367 to 352 */
    0xa0a0a0a0,            /* ICDICFR23 : 383 to 368 */
    0xa0a0a0a0,            /* ICDICFR24 : 399 to 384 */
    0xa0a0a0a0,            /* ICDICFR25 : 415 to 400 */
    0xa0a0a0a0,            /* ICDICFR26 : 431 to 416 */
    0xa0a0a0a0,            /* ICDICFR27 : 447 to 432 */
    0xa0a0a0a0,            /* ICDICFR28 : 463 to 448 */
    0xa0a0a0a0,            /* ICDICFR29 : 479 to 464 */
    0xa0a0a0a0,            /* ICDICFR30 : 495 to 480 */
    0xa0a0a0a0,            /* ICDICFR31 : 511 to 496 */
    0xa0a0a0a0,            /* ICDICFR32 : 527 to 512 */
    0xa0a0a0a0,            /* ICDICFR33 : 543 to 528 */
    0xa0a0a0a0,            /* ICDICFR34 : 559 to 544 */
    0xa0a0a0a0,            /* ICDICFR35 : 575 to 560 */
    0xa0a0a0a0             /* ICDICFR36 : 586 to 576 */
};
                                                                /* ------------ GIC DISTRIBUTOR INTERFACE ------------- */
typedef  struct  arm_reg_gic_dist {
    CPU_REG32  ICDDCR;                                          /* Distributor Control Register.                        */
    CPU_REG32  ICDICTR;                                         /* Interrupt Controller Type Register.                  */
    CPU_REG32  ICDIIDR;                                         /* Distributor Implementer Identification Register.     */
    CPU_REG32  RSVD1[29];                                       /* Reserved.                                            */
    CPU_REG32  ICDISRn[32];                                     /* Interrupt Security Registers.                        */
    CPU_REG32  ICDISERn[32];                                    /* Interrupt Set-Enable Registers.                      */
    CPU_REG32  ICDICERn[32];                                    /* Interrupt Clear-Enable Registers.                    */
    CPU_REG32  ICDISPRn[32];                                    /* Interrupt Set-Pending Registers.                     */
    CPU_REG32  ICDICPRn[32];                                    /* Interrupt Clear-Pending Registers.                   */
    CPU_REG32  ICDABRn[32];                                     /* Active Bit Registers.                                */
    CPU_REG32  RSVD2[32];                                       /* Reserved.                                            */
    CPU_REG32  ICDIPRn[255];                                    /* Interrupt Priority Registers.                        */
    CPU_REG32  RSVD3[1];                                        /* Reserved.                                            */
    CPU_REG32  ICDIPTRn[255];                                   /* Interrupt Processor Target Registers.                */
    CPU_REG32  RSVD4[1];                                        /* Reserved.                                            */
    CPU_REG32  ICDICFRn[64];                                    /* Interrupt Configuration Registers.                   */
    CPU_REG32  RSVD5[128];                                      /* Reserved.                                            */
    CPU_REG32  ICDSGIR;                                         /* Software Generate Interrupt Register.                */
    CPU_REG32  RSVD6[51];                                       /* Reserved.                                            */
} ARM_REG_GIC_DIST, *ARM_REG_GIC_DIST_PTR;


                                                                /* ---------------- GIC CPU INTERFACE ----------------- */
typedef  struct  arm_reg_gic_if {
    CPU_REG32  ICCICR;                                          /* CPU Interface Control Register.                      */
    CPU_REG32  ICCPMR;                                          /* Interrupt Priority Mask Register.                    */
    CPU_REG32  ICCBPR;                                          /* Binary Point Register.                               */
    CPU_REG32  ICCIAR;                                          /* Interrupt Acknowledge Register.                      */
    CPU_REG32  ICCEOIR;                                         /* End Interrupt Register.                              */
    CPU_REG32  ICCRPR;                                          /* Running Priority Register.                           */
    CPU_REG32  ICCHPIR;                                         /* Highest Pending Interrupt Register.                  */
    CPU_REG32  ICCABPR;                                         /* Aliased Binary Point Register.                       */
    CPU_REG32  RSVD[55];                                        /* Reserved.                                            */
    CPU_REG32  ICCIIDR;                                         /* CPU Interface Identification Register.               */
} ARM_REG_GIC_IF, *ARM_REG_GIC_IF_PTR;


                                                                /* ----------- DISTRIBUTOR CONTROL REGISTER ----------- */
#define  ARM_BIT_GIC_DIST_ICDDCR_EN            DEF_BIT_00       /* Global GIC enable.                                   */



                                                                /* ---------- CPU INTERFACE CONTROL REGISTER ---------- */
#define  ARM_BIT_GIC_IF_ICCICR_ENS             DEF_BIT_00       /* Enable secure interrupts.                            */
#define  ARM_BIT_GIC_IF_ICCICR_ENNS            DEF_BIT_01       /* Enable non-secure interrupts.                        */
#define  ARM_BIT_GIC_IF_ICCICR_ACKCTL          DEF_BIT_02       /* Secure ack of NS interrupts.                         */
#define  ARM_BIT_GIC_IF_ICCICR_FIQEN           DEF_BIT_03       /* Enable FIQ.                                          */


/*
*********************************************************************************************************
*********************************************************************************************************
*                                          GLOBAL FUNCTIONS
*********************************************************************************************************
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                            BSP_Int_Init()
*
* Description : Initialise interrupts.
*
* Argument(s) : none.
*
* Return(s)   : none.
*
* Note(s)     : none.
*
*********************************************************************************************************
*/

void BSP_Int_Init (void)
{
#if (BSP_CFG_FIQ_EN == DEF_ENABLED)
    CPU_INT32U  i;
#endif

    BSP_IntDetectVariant();

    if (BSP_GIC_Variant == 2u) {
#if (BSP_CFG_FIQ_EN == DEF_ENABLED)
        for(i = 0; i < 32; i++) {
            BSP_INT_GIC_DIST_REG->ICDISRn[i] = 0xFFFFFFFFu;
        }
#endif

        BSP_INT_GIC_DIST_REG->ICDDCR |= 3u;
                                                                /* Enable the GIC interface.                            */
        BSP_INT_GIC_IF_REG->ICCICR |= (ARM_BIT_GIC_IF_ICCICR_ENS | ARM_BIT_GIC_IF_ICCICR_ENNS);

#if (BSP_CFG_FIQ_EN == DEF_ENABLED)
        BSP_INT_GIC_IF_REG->ICCICR |= ARM_BIT_GIC_IF_ICCICR_FIQEN | ARM_BIT_GIC_IF_ICCICR_ACKCTL;
#endif

	    int offset;

	    for (offset = 0; offset < 32; offset++)
	    {
		    BSP_INT_GIC_DIST_REG->ICDICFRn[offset] = intc_icdicfrn_table[offset];
		    BSP_INT_GIC_DIST_REG->ICDISPRn[offset]=0xFfffffff;
//		BSP_INT_GIC_DIST_REG->ICDISERn[offset]=0xffffffff;
//		BSP_INT_GIC_DIST_REG->ICDICPRn[offset]=0xFfffffff;
	    }
    } else {
	    int offset;

	    for (offset = 0; offset < 32; offset++)
	    {
		    BSP_INT_GIC_RDIST_SGI_BASE->ICDICFRn[offset] = intc_icdicfrn_table[offset];
		    BSP_INT_GIC_RDIST_SGI_BASE->ICDISPRn[offset]=0xFfffffff;
//		BSP_INT_GIC_DIST_REG->ICDISERn[offset]=0xffffffff;
//		BSP_INT_GIC_DIST_REG->ICDICPRn[offset]=0xFfffffff;
	    }
    }
    CPU_MB();

    CPU_IntEn();

    BSP_IntPrioMaskSet(0xFFu);
}

/*
*********************************************************************************************************
*                                            BSP_IntSrcEn()
*
* Description : Enable interrupt source int_id.
*
* Argument(s) : int_id      Interrupt to enable.
*
* Return(s)   : none.
*
* Note(s)     : none.
*
*********************************************************************************************************
*/

void  BSP_IntSrcEn (CPU_INT32U int_id)
{
    CPU_INT32U  reg_off;
    CPU_INT32U  reg_bit;


    if(int_id >= ARM_GIC_INT_SRC_CNT) {
        return;
    }

    CPU_MB();

    reg_off = int_id >> 5u;                                     /* Calculate the register offset.                       */

    reg_bit = int_id & 0x1F;                                    /* Mask bit ID.                                         */

    if (BSP_GIC_Variant == 2u) {
        BSP_INT_GIC_DIST_REG->ICDISERn[reg_off] |= (1u << reg_bit);
    } else {
        ARM_REG_GIC_DIST_PTR base = (int_id < 32u) ? BSP_INT_GIC_RDIST_SGI_BASE : BSP_INT_GIC_DIST_REG;

        base->ICDISERn[reg_off] |= (1u << reg_bit);
        uart_puts("&base->ICDISERn[reg_off]=");
        uart_puthex(&base->ICDISERn[reg_off]);
        uart_puts("\n");
        uart_puts("base->ICDISERn[reg_off]=");
        uart_puthex(base->ICDISERn[reg_off]);
        uart_puts("\n");
    }

    CPU_MB();
}


/*
*********************************************************************************************************
*                                            BSP_IntSrcDis()
*
* Description : Disable interrupt source int_id.
*
* Argument(s) : int_id      Interrupt to disable.
*
* Return(s)   : none.
*
* Note(s)     : none.
*
*********************************************************************************************************
*/

void  BSP_IntSrcDis (CPU_INT32U int_id)
{
    CPU_INT32U  reg_off;
    CPU_INT32U  reg_bit;

    if(int_id >= ARM_GIC_INT_SRC_CNT) {
        return;
    }

    CPU_MB();

    reg_off = int_id >> 5u;                                     /* Calculate the register offset.                       */

    reg_bit = int_id & 0x1F;                                    /* Mask bit ID.                                         */

    if (BSP_GIC_Variant == 2u) {
        BSP_INT_GIC_DIST_REG->ICDICERn[reg_off] = 1u << reg_bit;
    } else {
        ARM_REG_GIC_DIST_PTR base = (int_id < 32u) ? BSP_INT_GIC_RDIST_SGI_BASE : BSP_INT_GIC_DIST_REG;

        base->ICDICERn[reg_off] = 1u << reg_bit;
    }
    CPU_MB();
}


/*
*********************************************************************************************************
*                                         BSP_IntPrioMaskSet()
*
* Description : Set CPU's interrupt priority mask.
*
* Argument(s) : prio        Priority mask.
*
* Return(s)   : none.
*
* Note(s)     : none.
*
*********************************************************************************************************
*/

void  BSP_IntPrioMaskSet (CPU_INT32U prio)
{
    if ((BSP_GIC_Variant == 2u) && (prio < 256u)) {
        CPU_MB();
        BSP_INT_GIC_IF_REG->ICCPMR = prio;
        CPU_MB();
    }
}


/*
*********************************************************************************************************
*                                           BSP_IntPrioSet()
*
* Description : Set interrupt priority.
*
* Argument(s) : int_id  Interrupt id.
*
*               prio    Interrupt priority.
*
* Return(s)   : none.
*
* Note(s)     : none.
*
*********************************************************************************************************
*/

void  BSP_IntPrioSet (CPU_INT32U  int_id,
                      CPU_INT32U  prio)
{
    CPU_INT32U  reg_off;
    CPU_INT32U  reg_byte;
    CPU_INT32U  temp_reg;
    CPU_SR_ALLOC();

    if(int_id >= ARM_GIC_INT_SRC_CNT) {
        return;
    }

    if(prio >= 256) {
        return;
    }

    CPU_CRITICAL_ENTER();

    reg_off = int_id >> 2u;
    reg_byte = int_id & 0x03;

    temp_reg = BSP_INT_GIC_DIST_REG->ICDIPRn[reg_off];
    temp_reg = temp_reg & ~(0xFF << (reg_byte * 8u));
    temp_reg = temp_reg | ((prio & 0x1Fu) << (reg_byte * 8u));

    BSP_INT_GIC_DIST_REG->ICDIPRn[reg_off] = temp_reg;

    CPU_CRITICAL_EXIT();
}


/*
*********************************************************************************************************
*                                          BSP_IntTargetSet()
*
* Description : Set interrupt target.
*
* Argument(s) : int_id              Interrupt id.
*
*               int_target_list     Interrupt CPU target list.
*
* Return(s)   : none.
*
* Note(s)     : none.
*
*********************************************************************************************************
*/

void  BSP_IntTargetSet (CPU_INT32U  int_id,
                        CPU_INT08U  int_target_list)
{
    CPU_INT32U  reg_off;
    CPU_INT32U  reg_byte;
    CPU_INT32U  temp_reg;
    CPU_SR_ALLOC();


    if(int_id >= ARM_GIC_INT_SRC_CNT) {
        return;
    }

    CPU_CRITICAL_ENTER();

    reg_off = int_id >> 2u;
    reg_byte = int_id & 0x03;

    temp_reg = BSP_INT_GIC_DIST_REG->ICDIPTRn[reg_off];
    temp_reg = temp_reg & ~(0xFF << (reg_byte * 8u));
    temp_reg = temp_reg | ((int_target_list & 0x1Fu) << (reg_byte * 8u));

    BSP_INT_GIC_DIST_REG->ICDIPTRn[reg_off] = temp_reg;

    CPU_CRITICAL_EXIT();
}


/*
*********************************************************************************************************
*                                           BSP_IntVectSet()
*
* Description : Configure interrupt vector.
*
* Argument(s) : int_id              Interrupt ID.
*
*               int_prio            Interrupt priority.
*
*               int_target_list     Interrupt CPU target list
*
*               int_fnct            ISR function pointer.
*
* Return(s)   : Interrupt configuration result.
*                                DEF_YES                   Interrupt successfully set.
*                                OS_ERR_OBJ_PTR_NULL       Error setting interrupt.
*
* Note(s)     : none.
*
*********************************************************************************************************
*/

CPU_BOOLEAN  BSP_IntVectSet (CPU_INT32U       int_id,
                             CPU_INT32U       int_prio,
                             CPU_INT08U       int_target_list,
                             BSP_INT_FNCT_PTR int_fnct)
{

    CPU_SR_ALLOC();


    if(int_prio > 255u) {
        return DEF_NO;
    }

    if(int_id >= ARM_GIC_INT_SRC_CNT) {
        return DEF_NO;
    }

    if(int_target_list > 255u) {
        return DEF_NO;
    }


    CPU_CRITICAL_ENTER();                                       /* Prevent partially configured interrupts.             */

    if (BSP_GIC_Variant == 2u) {
        if (int_target_list == 0u) {
            int_target_list = 1u;
        }
        BSP_IntPrioSet(int_id,
                       int_prio);

        BSP_IntTargetSet(int_id,
                          int_target_list);
    }
    BSP_IntVectTbl[int_id] = int_fnct;
    /* Cache ISR for GIC dispatch / 儲存 GIC 中斷對應的 ISR */

    CPU_CRITICAL_EXIT();


    return (DEF_OK);
}

CPU_INT08U BSP_Int_GICVariantGet(void)
{
    return BSP_GIC_Variant;
}


/*
*********************************************************************************************************
*                                           BSP_IntHandler()
*
* Description : Generic interrupt handler.
*
* Argument(s) : Interrupt type.
*
* Return(s)   : none.
*
* Note(s)     : none.
*
*********************************************************************************************************
*/

extern unsigned long long gic_read_iar(void);
extern gic_write_eoir(unsigned int irq);

void  BSP_IntHandler (void)
{
    CPU_INT32U        int_ack;
    CPU_INT32U        int_id;
    CPU_INT32U        int_cpu;
    BSP_INT_FNCT_PTR  p_isr;

    CPU_SR_ALLOC();

    CPU_CRITICAL_ENTER();                                       /* Prevent partially configured interrupts.             */

    if (BSP_GIC_Variant == 2u) {
        int_ack = BSP_INT_GIC_IF_REG->ICCIAR;                       /* Acknowledge the interrupt.                           */
        int_id = int_ack & DEF_BIT_FIELD(10u, 0u);                  /* Mask away the CPUID.                                 */
        int_cpu = (int_ack & DEF_BIT_FIELD(12u, 2u)) >> 10u;        /* Extract the interrupt source.                        */
    } else {
        int_ack = 0u;
        int_id = (CPU_INT32U)gic_read_iar();
        int_cpu = 0u;
    }

    if(int_id == 1023u) {                                       /* Spurious interrupt.                                  */
        return;
    }

    p_isr = BSP_IntVectTbl[int_id];                             /* Fetch ISR handler.                                   */
    /* Dispatch registered ISR (timer tick, peripherals, etc.) / 呼叫已註冊的 ISR（包含系統節拍與周邊） */

//	if(int_id!=27)  //For debug pci interrupt
//	printf("[%s:%d]----------------------------------------------> int_id=%d\n",__func__,__LINE__,int_id);

    if(p_isr != DEF_NULL) {
        (*p_isr)(int_id);                                      /* Call ISR handler.                                    */
    }

    CPU_MB();                                                   /* Memory barrier before ending the interrupt.          */

    if (BSP_GIC_Variant == 2u) {
        BSP_INT_GIC_IF_REG->ICCEOIR = int_id;
    } else {
        gic_write_eoir(int_id);
    }
    CPU_CRITICAL_EXIT();
}


/*
*********************************************************************************************************
*                                            BSP_SGITrig()
*
* Description : Trigger software generated interrupt.
*
* Argument(s) : Interrupt type.
*
* Return(s)   : none.
*
* Note(s)     : none.
*
*********************************************************************************************************
*/

void  BSP_SGITrig  (CPU_INT32U  int_sgi)
{
    CPU_MB();
    BSP_INT_GIC_DIST_REG->ICDSGIR = DEF_BIT_16 | int_sgi;
    CPU_MB();
}


/*
*********************************************************************************************************
*                                          BSP_IntSrcFIQSet()
*
* Description : Change the group of an interrupt to group 0.
*
* Argument(s) : Interrupt ID.
*
* Return(s)   : none.
*
* Note(s)     : none.
*
*********************************************************************************************************
*/
void  BSP_IntSrcGroup0Set(CPU_INT32U int_id)
{
    CPU_INT32U  reg_off;
    CPU_INT32U  reg_bit;
    CPU_SR_ALLOC();

    if(int_id >= ARM_GIC_INT_SRC_CNT) {
        return;
    }

    reg_off = int_id >> 5u;                                     /* Calculate the register offset.                       */

    reg_bit = int_id & 0x1F;                                    /* Mask bit ID.                                         */

    CPU_CRITICAL_ENTER();
    BSP_INT_GIC_DIST_REG->ICDISRn[reg_off] &= ~(1u << reg_bit);
    CPU_CRITICAL_EXIT();

}
