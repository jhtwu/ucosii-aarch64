/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2013
 * David Feng <fenghua@phytium.com.cn>
 */

.macro	switch_el, xreg, el3_label, el2_label, el1_label
	mrs	\xreg, CurrentEL
	cmp	\xreg, 0xc
	b.eq	\el3_label
	cmp	\xreg, 0x8
	b.eq	\el2_label
	cmp	\xreg, 0x4
	b.eq	\el1_label
.endm

/*
 * void __asm_invalidate_tlb_all(void)
 *
 * invalidate all tlb entries.
*/
.globl  __asm_invalidate_tlb_all

__asm_invalidate_tlb_all:
		switch_el x9, 3f, 2f, 1f
	3:	tlbi	alle3
		dsb	sy
		isb
		b	0f
	2:	tlbi	alle2
		dsb	sy
		isb
		b	0f
	1:	tlbi	vmalle1
		dsb	sy
		isb
	0:
		ret

