#define ASM_FILE   1

.section .isr_vector

.globl _Reset
_Reset:
	mrs x18, CurrentEL
	LSR x18, x18, #2
	
	/* check CPU ID = 0x0, or jump to hang */
	mrs	x0, mpidr_el1
	and	x0, x0, #3 
	cmp	x0, #0
	bne	hang
	
	/*
	 * Reset Vector Base Address,
	 */
reset_vector_base:
	/*
		GDB hang if access:
			vbar_el3, vbar_el2
			scr_el3, hcr_el2
	*/
	ldr	w1, = vectors
	msr	vbar_el1, x1
	
	MRS x0, CPACR_EL1
	LDR x1, =0x300000 // Don't trap to EL1 on SIMD access from EL0,1
	ORR  w0, w0, w1
	MSR CPACR_EL1, x0
	
boot_cpu:
	/* configure stack */
	adrp	x0, stackUSR	// Address of 4KB page at a PC-relative offset

// setup_global_data_ptr:
// 	ldr	x0, =0x40200000
// 	bic	sp, x0, #0xf	/* 16-byte alignment for ABI compliance */
// 	mov	x0, sp
	// bl	board_init_f_alloc_reserve
	// mov	sp, x0
	// /* set up gd here, outside any C code */
	// mov	x18, x0
	// bl	board_init_f_init_reserve

	// mov	x0, #0
	// bl	board_init_f

	
main_label:					// Why we need this label let GDB step into main()?
	mov	sp, x0				// sp = stack_top (align with 4KB page)
	bl	_start
	
hang:
	wfi
	b	hang

