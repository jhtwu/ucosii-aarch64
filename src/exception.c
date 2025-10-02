/* -*- mode: c; coding:utf-8 -*- */
/**********************************************************************/
/*  OS kernel sample                                                  */
/*  Copyright 2014 Takeharu KATO                                      */
/*                                                                    */
/*  Exception handler                                                 */
/*                                                                    */
/**********************************************************************/

void sync_trap_handler()
{
	static int count=0;
	count++;
	if(count%5000==0)
		uart_puts(".s");
}


void common_irq_trap_handler()
{
	// Forward IRQs (including timer tick) to the shared dispatcher / 將包含計時器在內的 IRQ 轉送至共用分派器
	BSP_IntHandler();
}
