/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2012 The Chromium OS Authors.
 * (C) Copyright 2002-2010
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 */

#ifndef __ASM_GENERIC_GBL_DATA_H
#define __ASM_GENERIC_GBL_DATA_H

/* Architecture-specific global data */
struct arch_global_data {
	/* "static data" needed by most of timer.c on ARM platforms */
	unsigned long timer_rate_hz;
	unsigned int tbu;
	unsigned int tbl;
	unsigned long lastinc;
	unsigned long long timer_reset_value;
	unsigned long tlb_addr;
	unsigned long tlb_size;
	unsigned long tlb_fillptr;
	unsigned long tlb_emerg;
};


// #define DECLARE_GLOBAL_DATA_PTR		register volatile gd_t *gd asm ("x18")


/*
 * The following data structure is placed in some memory which is
 * available very early after boot (like DPRAM on MPC8xx/MPC82xx, or
 * some locked parts of the data cache) to allow for a minimum set of
 * global variables during system initialization (until we have set
 * up the memory controller so that we can use RAM).
 *
 * Keep it *SMALL* and remember to set GENERATED_GBL_DATA_SIZE > sizeof(gd_t)
 *
 * Each architecture has its own private fields. For now all are private
 */

typedef struct global_data {
	struct arch_global_data arch;	/* architecture-specific data */
} gd_t;


#ifdef CONFIG_BOARD_TYPES
#define gd_board_type()		gd->board_type
#else
#define gd_board_type()		0
#endif

/*
 * Global Data Flags - the top 16 bits are reserved for arch-specific flags
 */
#define GD_FLG_RELOC		0x00001	/* Code was relocated to RAM	   */
#define GD_FLG_DEVINIT		0x00002	/* Devices have been initialized   */
#define GD_FLG_SILENT		0x00004	/* Silent mode			   */
#define GD_FLG_POSTFAIL		0x00008	/* Critical POST test failed	   */
#define GD_FLG_POSTSTOP		0x00010	/* POST seqeunce aborted	   */
#define GD_FLG_LOGINIT		0x00020	/* Log Buffer has been initialized */
#define GD_FLG_DISABLE_CONSOLE	0x00040	/* Disable console (in & out)	   */
#define GD_FLG_ENV_READY	0x00080	/* Env. imported into hash table   */
#define GD_FLG_SERIAL_READY	0x00100	/* Pre-reloc serial console ready  */
#define GD_FLG_FULL_MALLOC_INIT	0x00200	/* Full malloc() is ready	   */
#define GD_FLG_SPL_INIT		0x00400	/* spl_init() has been called	   */
#define GD_FLG_SKIP_RELOC	0x00800	/* Don't relocate		   */
#define GD_FLG_RECORD		0x01000	/* Record console		   */
#define GD_FLG_ENV_DEFAULT	0x02000 /* Default variable flag	   */
#define GD_FLG_SPL_EARLY_INIT	0x04000 /* Early SPL init is done	   */
#define GD_FLG_LOG_READY	0x08000 /* Log system is ready for use	   */

#endif /* __ASM_GENERIC_GBL_DATA_H */
