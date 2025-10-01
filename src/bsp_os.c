/*
*********************************************************************************************************
*
*                                    MICRIUM BOARD SUPPORT PACKAGE
*
*                          (c) Copyright 2003-2013; Micrium, Inc.; Weston, FL
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
*                                  CORTEX A9 OS BOARD SUPORT PACKAGE
*
* Filename      : bsp_os.c
* Version       : V1.00
* Programmer(s) : JBL
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                             INCLUDE FILES
*********************************************************************************************************
*/

#include  <lib_def.h>
#include  <cpu.h>

#include  <os_cpu.h>

#include  <bsp.h>
#include  <bsp_os.h>
#include  <bsp_int.h>
#include  <includes.h>



/*
*********************************************************************************************************
*                                            LOCAL DEFINES
*********************************************************************************************************
*/

#define  ARM_PTMR_REG_PTLR     (*((CPU_REG32 *)(ARM_PRIV_PERIPH_BASE + 0x0600))) /* Private timer load register.        */
#define  ARM_PTMR_REG_PTCTRR   (*((CPU_REG32 *)(ARM_PRIV_PERIPH_BASE + 0x0604))) /* Private timer counter register.     */
#define  ARM_PTMR_REG_PTCTLR   (*((CPU_REG32 *)(ARM_PRIV_PERIPH_BASE + 0x0608))) /* Private timer control register.     */
#define  ARM_PTMR_REG_PTISR    (*((CPU_REG32 *)(ARM_PRIV_PERIPH_BASE + 0x060C))) /* Private timer interrupt status register.*/


/*
*********************************************************************************************************
*                                      BSP_OS_SemCreate()
*
* Description : Creates a sempahore to lock/unlock
*
* Argument(s) : p_sem        Pointer to a BSP_OS_SEM structure
*
*               sem_val      Initial value of the semaphore.
*
*               p_sem_name   Pointer to the semaphore name.
*
* Return(s)   : DEF_OK        if the semaphore was created.
*               DEF_FAIL      if the sempahore could not be created.
*
* Caller(s)   : Application.
*
* Note(s)     : none.
*
*********************************************************************************************************
*/

CPU_BOOLEAN  BSP_OS_SemCreate (BSP_OS_SEM       *p_sem,
                               BSP_OS_SEM_VAL    sem_val,
                               CPU_CHAR         *p_sem_name)
{
    OS_EVENT    *p_event;

#if (OS_EVENT_NAME_EN > 0)
    CPU_INT08U  err;
#endif

    p_event = OSSemCreate(sem_val);

    if (p_event == (OS_EVENT *)0) {
        return (DEF_FAIL);
    }

    *p_sem = (BSP_OS_SEM)(p_event);

#if (OS_EVENT_NAME_EN > 0)
    OSEventNameSet((OS_EVENT *)p_event,
                   (INT8U    *)p_sem_name,
                   (INT8U    *)&err);
#endif


    return (DEF_OK);
}


/*
*********************************************************************************************************
*                                     BSP_OS_SemWait()
*
* Description : Wait on a semaphore to become available
*
* Argument(s) : p_sem        Pointer to the sempahore handler.
*
*               dly_ms       delay in miliseconds to wait on the semaphore
*
* Return(s)   : error code return     DEF_OK       if the semaphore was acquire
*                                     DEF_FAIL     if the sempahore could not be acquire
*
* Caller(s)   : Application.
*
* Note(s)     : none.
*********************************************************************************************************
*/

CPU_BOOLEAN  BSP_OS_SemWait (BSP_OS_SEM *p_sem,
                             CPU_INT32U  dly_ms)
{
    CPU_INT08U  err;
    CPU_INT32U  dly_ticks;


    dly_ticks  = ((dly_ms * DEF_TIME_NBR_mS_PER_SEC) / OS_TICKS_PER_SEC);

    OSSemPend((OS_EVENT   *)*p_sem,
              (CPU_INT32U  )dly_ticks,
              (CPU_INT08U *)&err);

    if (err != OS_ERR_NONE) {
       return (DEF_FAIL);
    }

    return (DEF_OK);
}

/*
*********************************************************************************************************
*                                      BSP_OS_SemCreate()
*
* Description : Post a semaphore
*
* Argument(s) : p_sem                 Pointer to the Semaphore handler.
*
* Return(s)   : error code return     DEF_OK     if the semaphore was posted.
*                                     DEF_FAIL   if the sempahore could not be posted.
*
* Caller(s)   : Application.
*
* Note(s)     : none.
*********************************************************************************************************
*/

CPU_BOOLEAN  BSP_OS_SemPost (BSP_OS_SEM * p_sem)
{
    CPU_INT08U  err;


    err = OSSemPost((OS_EVENT *)*p_sem);

    if (err != OS_ERR_NONE) {
        return (DEF_FAIL);
    }

    return (DEF_OK);
}

enum arch_timer_reg {
    ARCH_TIMER_REG_CTRL,
    ARCH_TIMER_REG_TVAL,
};

// #define isb(option) __asm__ __volatile__ ("isb " #option : : : "memory")
#define isb(option) NOTHING()
#define ARCH_TIMER_PHYS_ACCESS      0
#define ARCH_TIMER_VIRT_ACCESS      1
#define ARCH_TIMER_MEM_PHYS_ACCESS  2
#define ARCH_TIMER_MEM_VIRT_ACCESS  3


#define ARCH_TIMER_TYPE_CP15        BIT(0)
#define ARCH_TIMER_TYPE_MEM     BIT(1)

#define ARCH_TIMER_CTRL_ENABLE      (1 << 0)
#define ARCH_TIMER_CTRL_IT_MASK     (1 << 1)
#define ARCH_TIMER_CTRL_IT_STAT     (1 << 2)


static __always_inline
void arch_timer_reg_write_cp15(int access, enum arch_timer_reg reg, unsigned int val)
{
    if (access == ARCH_TIMER_PHYS_ACCESS) {
		uart_puts("arch_timer_reg_write_cp15, ARCH_TIMER_PHYS_ACCESS\n");
        switch (reg) {
        case ARCH_TIMER_REG_CTRL:
			//write_sysreg(val, cntp_ctl_el0);
			asm volatile("msr cntp_ctl_el0,%x0" : : "rZ" (val));
		
            break;
        case ARCH_TIMER_REG_TVAL:
            //write_sysreg(val, cntp_tval_el0);
			asm volatile("msr cntp_tval_el0,%x0" : : "rZ" (val));
            break;
        }
    } else if (access == ARCH_TIMER_VIRT_ACCESS) {
		//uart_puts("arch_timer_reg_write_cp15, ARCH_TIMER_VIRT_ACCESS\n");
        switch (reg) {
        case ARCH_TIMER_REG_CTRL:
            //write_sysreg(val, cntv_ctl_el0);
		//uart_puts("Write ARCH_TIMER_REG_CTRL with value:"); uart_puthex(val); uart_puts("\n");
			asm volatile("msr cntv_ctl_el0,%x0" : : "rZ" (val));
            break;
        case ARCH_TIMER_REG_TVAL:
		//uart_puts("Write ARCH_TIMER_REG_TVAL with value:"); uart_puthex(val); uart_puts("\n");
            //write_sysreg(val, cntv_tval_el0);
			asm volatile("msr cntv_tval_el0,%x0" : : "rZ" (val));
            break;
        }
    }

    isb();
}

static __always_inline
unsigned int arch_timer_reg_read_cp15(int access, enum arch_timer_reg reg)
{
	int val;
    if (access == ARCH_TIMER_PHYS_ACCESS) {
        switch (reg) {
        case ARCH_TIMER_REG_CTRL:
            //return read_sysreg(cntp_ctl_el0);
			asm volatile("mrs %0,cntp_ctl_el0" : "=r" (val));
			return val;
        case ARCH_TIMER_REG_TVAL:
            //return arch_timer_reg_read_stable(cntp_tval_el0);
			asm volatile("mrs %0,cntp_tval_el0" : "=r" (val));
			return val;
        }
    } else if (access == ARCH_TIMER_VIRT_ACCESS) {
        switch (reg) {
        case ARCH_TIMER_REG_CTRL:
            //return read_sysreg(cntp_ctl_el0);
	//		uart_puts("arch_timer_reg_read_cp15,reg ARCH_TIMER_REG_CTRL: ");
			asm volatile("mrs %0,cntv_ctl_el0" : "=r" (val));
	//		uart_puthex(val); uart_puts("\n");
			return val;
        case ARCH_TIMER_REG_TVAL:
	//		uart_puts("arch_timer_reg_read_cp15,reg ARCH_TIMER_REG_TVAL: ");
            //return arch_timer_reg_read_stable(cntv_tval_el0);
            //return arch_timer_reg_read_stable(cntp_tval_el0);
	//		uart_puthex(val); uart_puts("\n");
			asm volatile("mrs %0,cntv_tval_el0" : "=r" (val));
			return val;
        }
    }

	isb();
    //BUG();
}


void cp15_virt_timer_init()
{
//	uart_puts("cp15_virt_timer_init\n");
	arch_timer_reg_write_cp15(ARCH_TIMER_VIRT_ACCESS,0,7);
	arch_timer_reg_write_cp15(ARCH_TIMER_VIRT_ACCESS,1,614000);
	//arch_timer_reg_write_cp15(ARCH_TIMER_VIRT_ACCESS,1,1250000);
	//arch_timer_reg_write_cp15(ARCH_TIMER_VIRT_ACCESS,1,1000); //TODO: Ruby which value to set?
	arch_timer_reg_write_cp15(ARCH_TIMER_VIRT_ACCESS,0,5);
}
/*
*********************************************************************************************************
*                                       BSP_OS_TmrTickHandler()
*
* Description : Interrupt handler for the tick timer
*
* Argument(s) : cpu_id     Source core id
*
* Return(s)   : none.
*
* Caller(s)   : Application.
*
* Note(s)     : none.
*********************************************************************************************************
*/

void  BSP_OS_TmrTickHandler(CPU_INT32U cpu_id)
{
    ARM_PTMR_REG_PTISR = 0x01u;                                 /* Clear the interrupt.                                 */
	static int i=0;
#if 1 //def TASK_DBG
	i++;
	if(i%100==0){
		uart_puts("\nBSP_OS_TmrTickHandler, OSIntNesting=");
		uart_puthex(OSIntNesting);
	}
#endif
    OSTimeTick();
	unsigned int ctrl;
	ctrl = arch_timer_reg_read_cp15(ARCH_TIMER_VIRT_ACCESS,ARCH_TIMER_REG_CTRL);
		
//	uart_puthex(ctrl);
    if (ctrl & ARCH_TIMER_CTRL_IT_STAT) {
        ctrl |= ARCH_TIMER_CTRL_IT_MASK;
		arch_timer_reg_write_cp15(ARCH_TIMER_VIRT_ACCESS,ARCH_TIMER_REG_CTRL,ctrl);
	}

	cp15_virt_timer_init();
/*
	int val=0x2;
	asm volatile("msr cntkctl_el1,%x0" : : "rZ" (val));

	val=0xd6;
	asm volatile("msr cntkctl_el1,%x0" : : "rZ" (val));
*/
#if 0 //Linux's method  drivers/clocksource/arm_arch_timer.c. function timer_handler
    unsigned long ctrl;

    ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL, evt);
    if (ctrl & ARCH_TIMER_CTRL_IT_STAT) {
        ctrl |= ARCH_TIMER_CTRL_IT_MASK;
        arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl, evt);
        evt->event_handler(evt);
        return IRQ_HANDLED;
    }

#endif
}



/*
 *********************************************************************************************************
 *                                            BSP_OS_TmrTickInit()
 *
 * Description : Initialize uC/OS-III's tick source
 *
 * Argument(s) : ticks_per_sec              Number of ticks per second.
 *
 * Return(s)   : none.
 *
 * Caller(s)   : Application.
 *
 * Note(s)     : none.
 *********************************************************************************************************
 */

void BSP_OS_TmrTickInit(CPU_INT32U tick_rate)
{
	uart_puts("BSP_OS_TmrTickInit\n");


	//write_sysreg(cntkctl, cntkctl_el1);
	int val=0x2;
	asm volatile("msr cntkctl_el1,%x0" : : "rZ" (val));

	val=0xd6;
	asm volatile("msr cntkctl_el1,%x0" : : "rZ" (val));
	cp15_virt_timer_init();

#if 1
    BSP_IntVectSet (27u,
                    0u,
                    0u,
                    BSP_OS_TmrTickHandler);

    BSP_IntSrcEn(27u);
#endif

}


/*
 *********************************************************************************************************
 *                                          OS_CPU_ExceptHndlr()
 *
 * Description : Handle any exceptions.
 *
 * Argument(s) : except_id     ARM exception type:
 *
 *                                  OS_CPU_ARM_EXCEPT_RESET             0x00
 *                                  OS_CPU_ARM_EXCEPT_UNDEF_INSTR       0x01
 *                                  OS_CPU_ARM_EXCEPT_SWI               0x02
 *                                  OS_CPU_ARM_EXCEPT_PREFETCH_ABORT    0x03
 *                                  OS_CPU_ARM_EXCEPT_DATA_ABORT        0x04
 *                                  OS_CPU_ARM_EXCEPT_ADDR_ABORT        0x05
 *                                  OS_CPU_ARM_EXCEPT_IRQ               0x06
 *                                  OS_CPU_ARM_EXCEPT_FIQ               0x07
 *
 * Return(s)   : none.
 *
 * Caller(s)   : OS_CPU_ARM_EXCEPT_HANDLER(), which is declared in os_cpu_a.s.
 *
 * Note(s)     : (1) Only OS_CPU_ARM_EXCEPT_FIQ and OS_CPU_ARM_EXCEPT_IRQ exceptions handler are implemented.
 *                   For the rest of the exception a infinite loop is implemented for debuging pruposes. This behavior
 *                   should be replaced with another behavior (reboot, etc).
 *********************************************************************************************************
 */

void OS_CPU_ExceptHndlr(CPU_INT32U except_id) {

#if 0
    switch (except_id) {
    case OS_CPU_ARM_EXCEPT_FIQ:
        BSP_IntHandler();
        break;

    case OS_CPU_ARM_EXCEPT_IRQ:
        BSP_IntHandler();
        break;

    case OS_CPU_ARM_EXCEPT_RESET:
        /* $$$$ Insert code to handle a Reset exception               */

    case OS_CPU_ARM_EXCEPT_UNDEF_INSTR:
        /* $$$$ Insert code to handle a Undefine Instruction exception */

    case OS_CPU_ARM_EXCEPT_SWI:
        /* $$$$ Insert code to handle a Software exception             */

    case OS_CPU_ARM_EXCEPT_PREFETCH_ABORT:
        /* $$$$ Insert code to handle a Prefetch Abort exception       */

    case OS_CPU_ARM_EXCEPT_DATA_ABORT:
        /* $$$$ Insert code to handle a Data Abort exception           */

    case OS_CPU_ARM_EXCEPT_ADDR_ABORT:
        /* $$$$ Insert code to handle a Address Abort exception        */
    default:

        while (DEF_TRUE) { /* Infinite loop on other exceptions. (see note #1)          */
            CPU_WaitForEvent();
        }
    }
#endif
}
