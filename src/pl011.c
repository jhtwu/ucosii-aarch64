#include "pl011.h"
#include "includes.h"

volatile pl011_t* const UART0 = (pl011_t*)UART0_BASE;

inline void uart_putc(char c){
	while(UART0->FR & TXFF);
	UART0->DR = c;
}

char  UART_Handler(){

	int count=0;
	uart_puts("UART_Handler: ");
	while(UART0->FR & RXFE){
		count++;
		if(count>10000)
			break;
	};
	if(count>10000)
		return 0;

	uart_putc(UART0->DR);
	uart_puts("\n");
	return UART0->DR;
}

//ruby added
void BSP_OS_UARTInit()
{
	//From ARM Motherboard Express ATX V2M-P1 spec, 
	//UART0INTR is in interrupt 5
	//5+32=37
    BSP_IntVectSet (33u,
                    0u,
                    0u,
                    UART_Handler);

    BSP_IntSrcEn(33u);

    UART0->IMSC	= 0x10;
}
