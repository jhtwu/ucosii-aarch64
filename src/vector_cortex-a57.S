/* -*- mode: asm; coding:utf-8 -*- */
/************************************************************************/
/*  OS kernel sample                                                    */
/*  Copyright 2014 Takeharu KATO                                        */
/*                                                                      */
/*  Exception vector                                                    */
/*                                                                      */
/************************************************************************/
#define ASM_FILE   1

/*
 * IRQ
 */
#define IRQ_FOUND          (0)
#define IRQ_NOT_FOUND      (1)

.section ".text.boot"

.macro build_trapframe exc_type
    // SAVE CURRENT TASK'S CONTEXT:
    stp x29, x30, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x17, x18, [sp, #-16]!
    stp x15, x16, [sp, #-16]!
    stp x13, x14, [sp, #-16]!
    stp x11, x12, [sp, #-16]!
    stp x9,  x10, [sp, #-16]!
    stp x7,  x8,  [sp, #-16]!
    stp x5,  x6,  [sp, #-16]!
    stp x3,  x4,  [sp, #-16]!
    stp x1,  x2,  [sp, #-16]!
    mrs x21,  SPSR_EL1
    stp x21, x0,  [sp, #-16]!

.endm

.macro store_traped_sp
	mrs	x21, sp_el0
	str	x21, [sp, #(16)]
.endm

.macro call_sync_trap_handler
	bl sync_trap_handler
.endm

.macro call_common_trap_handler
	// All IRQ vectors funnel into C dispatcher for timer tick / 將所有 IRQ 集中轉交給 C 層處理系統節拍
	bl common_irq_trap_handler
.endm

.macro store_nested_sp
	mov	x21, sp 	
	add	x21, x21, #(288)
	str	x21, [sp, #(16)]
.endm

.macro restore_traped_sp
	ldr	w21, [sp, #(16)]
	msr	sp_el0, x21
.endm

.macro restore_trapframe
    ldp x21,  x0,  [sp], #16                              // Pop new task's context.
    msr SPSR_EL1, x21
    ldp x1,  x2,  [sp], #16
    ldp x3,  x4,  [sp], #16
    ldp x5,  x6,  [sp], #16
    ldp x7,  x8,  [sp], #16
    ldp x9,  x10, [sp], #16
    ldp x11, x12, [sp], #16
    ldp x13, x14, [sp], #16
    ldp x15, x16, [sp], #16
    ldp x17, x18, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x29, x30, [sp], #16

	eret
	ret
.endm
	
	/*
	 * Exception vectors.
	 */
	.align 11
	.globl vectors
vectors:
	/*
	 * Current EL with SP0
	 */
	.align 7
	b	_curr_el_sp0_sync		/* Synchronous */
	.align 7
	b	_curr_el_sp0_irq		/* IRQ/vIRQ */
	.align 7
	b	_curr_el_sp0_fiq		/* FIQ/vFIQ */
	.align 7
	b	_curr_el_sp0_serror		/* SError/vSError */

	/*
	 * Current EL with SPx
	 */
	.align 7
	b	_curr_el_spx_sync		/* Synchronous */
	.align 7
	b	_curr_el_spx_irq		/* IRQ/vIRQ */
	.align 7
	b	_curr_el_spx_fiq		/* FIQ/vFIQ */
	.align 7
	b	_curr_el_spx_serror		/* SError/vSError */

	/*
	 * Lower EL using AArch64
	 */
	.align 7
	b	_lower_el_aarch64_sync
	.align 7
	b	_lower_el_aarch64_irq
	.align 7
	b	_lower_el_aarch64_fiq
	.align 7
	b	_lower_el_aarch64_serror

	/*
	 * Lower EL using AArch32
	 */
	.align 7
	b	_lower_el_aarch32_sync
	.align 7
	b	_lower_el_aarch32_irq
	.align 7
	b	_lower_el_aarch32_fiq
	.align 7
	b	_lower_el_aarch32_serror


	.align  2
_curr_el_sp0_sync:
	build_trapframe 0x000
	store_traped_sp
	call_sync_trap_handler
	restore_traped_sp
	restore_trapframe

	.align  2
_curr_el_sp0_irq:
	build_trapframe (0x080)
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe

	.align  2
_curr_el_sp0_fiq:
	build_trapframe (0x100)
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe

	.align  2
_curr_el_sp0_serror:
	build_trapframe (0x180)
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe


	.align  2
_curr_el_spx_sync:
	// This may impace original x20,x21,x22, but it's easier for debugging what happened.
	// Could refer to  ARMv8-A_Architecture_Reference_Manual_(Issue_A.a).pdf to check the exception reason
	mrs    x20, esr_el1  
	mrs    x21, far_el1
	mrs    x22, elr_el1
	msr daifset, #0xf  // disable interruptions.
	build_trapframe (0x200)
	store_nested_sp
	call_sync_trap_handler
	restore_traped_sp
	restore_trapframe
	msr    daifclr, #0xf     // arch_local_irq_enable
	b _curr_el_spx_fiq

	.align  2
_curr_el_spx_irq:
      b OS_CPU_ARM_ExceptIrqHndlr  //In OS_CPU_ARM_ExceptIrqHndlr will do push/pop

	.align  2
_curr_el_spx_fiq:
	build_trapframe (0x300)
	store_nested_sp
	call_common_trap_handler	
	restore_traped_sp
	restore_trapframe

	.align  2
_curr_el_spx_serror:
	build_trapframe (0x380)	
	store_nested_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe


	.align  2
_lower_el_aarch64_sync:
	build_trapframe (0x400)
	store_traped_sp
	call_sync_trap_handler
	restore_traped_sp
	restore_trapframe
	
	.align  2
_lower_el_aarch64_irq:
	build_trapframe (0x480)	
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe
	
	.align  2
_lower_el_aarch64_fiq:
	build_trapframe (0x500)
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe
	
	.align  2
_lower_el_aarch64_serror:
	build_trapframe (0x580)	
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe


	.align  2
_lower_el_aarch32_sync:
	build_trapframe (0x600)	
	store_traped_sp
	call_sync_trap_handler
	restore_traped_sp
	restore_trapframe
	
	.align  2
_lower_el_aarch32_irq:
	build_trapframe (0x680)
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe
	
	.align  2
_lower_el_aarch32_fiq:
	build_trapframe (0x700)
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe
	
	.align  2
_lower_el_aarch32_serror:
	build_trapframe (0x780)
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe
