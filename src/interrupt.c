#include "interrupt.h"
#include "pl050.h"
#include <stdio.h>
#include "includes.h"

func_t isr_table[MAXIRQNUM];
void install_isr(IRQn_Type irq_num, func_t handler){
	isr_table[irq_num] = handler;
}
void __attribute__ ((interrupt("SWI"))) c_svc(void){
	printf("yaay SVC ticked\n");
    dump2(__func__,__LINE__);
}
void __attribute__ ((interrupt("FIQ"))) c_fiq(void){
	printf("yaay FIQ ticked\n");
    dump2(__func__,__LINE__);
}

void __attribute__ ((interrupt("UND"))) und_handler(void){
	// asm volatile("cpsid i" : : : "memory", "cc");
    dump2(__func__,__LINE__);
	// asm volatile("cpsie i" : : : "memory", "cc");
	
}

void __attribute__ ((interrupt("P_ABT"))) p_abt_handler(void){
	dump2(__func__,__LINE__);
//	OS_CPU_SysTickHandler();
}


void __attribute__ ((interrupt("D_ABT"))) d_abt_handler(void){
	dump2(__func__,__LINE__);
}

void __attribute__ ((interrupt("NOT_USED"))) not_used_handler(void){
	dump2(__func__,__LINE__);
}

void __attribute__ ((interrupt("IRQ"))) c_irq(void){
	// asm volatile("cpsid i" : : : "memory", "cc");
    dump2(__func__,__LINE__);
	int irq_num = GIC_AcknowledgePending();
//	printf("yaay IRQ ticked,  irq_num=%d\n",irq_num);
	GIC_ClearPendingIRQ(irq_num);
	if(isr_table[irq_num] != NULL){
		isr_table[irq_num]();
	}else{
		printf("no handler found for %d\n",irq_num);
	}
	GIC_EndInterrupt(irq_num);
	CPU_MB();
	// asm volatile("cpsie i" : : : "memory", "cc");
}

void enable_irq(IRQn_Type irq_num){
	GIC_EnableIRQ(irq_num);
}
void interrupt_init(void){
	GIC_Enable();
	// asm volatile("cpsie i" : : : "memory", "cc");
}
