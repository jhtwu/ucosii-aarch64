#include "uart.h"

volatile unsigned int * const UART0DR = (unsigned int *) 0x09000000;
volatile unsigned int * const UART0FR = (unsigned int *) 0x09000018;


void my_uart_putc(const char c)
{
	// Wait for UART to become ready to transmit.
	while ((*UART0FR) & (1 << 5)) { }
	*UART0DR = c; /* Transmit char */
}

void uart_puthex(uint64_t n)
{
	const char *hexdigits = "0123456789ABCDEF";

	my_uart_putc('0');
	my_uart_putc('x');
	for (int i = 60; i >= 0; i -= 4){
		my_uart_putc(hexdigits[(n >> i) & 0xf]);
		if (i == 32)
			my_uart_putc(' ');
	}
}

void uart_puts(const char *s) {
	for (int i = 0; s[i] != '\0'; i ++)
		my_uart_putc((unsigned char)s[i]);
}

