// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2013
 * David Feng <fenghua@phytium.com.cn>
 *
 * (C) Copyright 2016
 * Alexander Graf <agraf@suse.de>
 */

#include <asm/types.h>
#include <asm/global_data.h>
#include <asm/system.h>
#include <asm/mmu.h>

// DECLARE_GLOBAL_DATA_PTR;

gd_t gd;

extern const char mmu_start __attribute__((section(".mmutable")));


void * my_memset(void * s,int c,u64 count)
{
	unsigned long *sl = (unsigned long *) s;
	char *s8;

	unsigned long cl = 0;
	int i;

	/* do it one word at a time (32 bits or 64 bits) while possible */
	if ( ((ulong)s & (sizeof(*sl) - 1)) == 0) {
		for (i = 0; i < sizeof(*sl); i++) {
			cl <<= 8;
			cl |= c & 0xff;
		}
		while (count >= sizeof(*sl)) {
			*sl++ = cl;
			count -= sizeof(*sl);
		}
	}

	s8 = (char *)sl;
	while (count--)
		*s8++ = c;

	return s;
}

/*
 *  With 4k page granule, a virtual address is split into 4 lookup parts
 *  spanning 9 bits each:
 *
 *    _______________________________________________
 *   |       |       |       |       |       |       |
 *   |   0   |  Lv0  |  Lv1  |  Lv2  |  Lv3  |  off  |
 *   |_______|_______|_______|_______|_______|_______|
 *     63-48   47-39   38-30   29-21   20-12   11-00
 *
 *             mask        page size
 *
 *    Lv0: FF8000000000       --
 *    Lv1:   7FC0000000       1G
 *    Lv2:     3FE00000       2M
 *    Lv3:       1FF000       4K
 *    off:          FFF
 */

u64 get_tcr(int el, u64 *pips, u64 *pva_bits)
{
	u64 max_addr = 0;
	u64 ips, va_bits;
	u64 tcr;
	int i;

	/* Find the largest address we need to support */
	for (i = 0; mem_map[i].size || mem_map[i].attrs; i++)
		max_addr = max(max_addr, mem_map[i].virt + mem_map[i].size);

	/* Calculate the maximum physical (and thus virtual) address */
	if (max_addr > (1ULL << 44)) {
		ips = 5;
		va_bits = 48;
	} else  if (max_addr > (1ULL << 42)) {
		ips = 4;
		va_bits = 44;
	} else  if (max_addr > (1ULL << 40)) {
		ips = 3;
		va_bits = 42;
	} else  if (max_addr > (1ULL << 36)) {
		ips = 2;
		va_bits = 40;
	} else  if (max_addr > (1ULL << 32)) {
		ips = 1;
		va_bits = 36;
	} else {
		ips = 0;
		va_bits = 32;
	}

	if (el == 1) {
		tcr = TCR_EL1_RSVD | (ips << 32) | TCR_EPD1_DISABLE;
	} else if (el == 2) {
		tcr = TCR_EL2_RSVD | (ips << 16);
	} else {
		tcr = TCR_EL3_RSVD | (ips << 16);
	}

	/* PTWs cacheable, inner/outer WBWA and inner shareable */
	tcr |= TCR_TG0_4K | TCR_SHARED_INNER | TCR_ORGN_WBWA | TCR_IRGN_WBWA;
	tcr |= TCR_T0SZ(va_bits);

	if (pips)
		*pips = ips;
	if (pva_bits)
		*pva_bits = va_bits;

	return tcr;
}

#define MAX_PTE_ENTRIES 512

static int pte_type(u64 *pte)
{
	return *pte & PTE_TYPE_MASK;
}

/* Returns the LSB number for a PTE on level <level> */
static int level2shift(int level)
{
	/* Page is 12 bits wide, every level translates 9 bits */
	return (12 + 9 * (3 - level));
}

static u64 *find_pte(u64 addr, int level)
{
	int start_level = 0;
	u64 *pte;
	u64 idx;
	u64 va_bits;
	int i;

	// printf("addr=0x%x level=%d\n", addr, level);

	get_tcr(0, NULL, &va_bits);
	if (va_bits < 39)
		start_level = 1;

	if (level < start_level)
		return NULL;

	/* Walk through all page table levels to find our PTE */
	pte = (u64*)gd.arch.tlb_addr;
	for (i = start_level; i < 4; i++) {
		idx = (addr >> level2shift(i)) & 0x1FF;
		pte += idx;
		// printf("*********** idx=%llx PTE 0x%llx at level %d: 0x%llx\n", idx, pte, i, *pte);

		/* Found it */
		if (i == level){
			// printf("idx=%llx PTE %p at level %d: %llx\n", idx, pte, i, *pte);
			return pte;
		}
		/* PTE is no table (either invalid or block), can't traverse */
		if (pte_type(pte) != PTE_TYPE_TABLE)
			return NULL;
		/* Off to the next level */
		pte = (u64*)(*pte & 0x0000fffffffff000ULL);
	}

	/* Should never reach here */
	return NULL;
}

/* Returns and creates a new full table (512 entries) */
static u64 *create_table(void)
{
	u64 *new_table = (u64*)gd.arch.tlb_fillptr;
	u64 pt_len = MAX_PTE_ENTRIES * sizeof(u64);

	/* Allocate MAX_PTE_ENTRIES pte entries */
	gd.arch.tlb_fillptr += pt_len;

	if (gd.arch.tlb_fillptr - gd.arch.tlb_addr > gd.arch.tlb_size){
		printf("Insufficient RAM for page table: 0x%lx > 0x%lx. "
		      "Please increase the size in get_page_table_size()",
			gd.arch.tlb_fillptr - gd.arch.tlb_addr,
			gd.arch.tlb_size);
		exit(0);
	}

	/* Mark all entries as invalid */
	my_memset(new_table, 0, pt_len);

	return new_table;
}

static void set_pte_table(u64 *pte, u64 *table)
{
	/* Point *pte to the new table */
//	printf("[set_pte_table()] Setting 0x%llx to addr=0x%llx\n", pte, table);
	*pte = PTE_TYPE_TABLE | (ulong)table;
}

/* Splits a block PTE into table with subpages spanning the old block */
static void split_block(u64 *pte, int level)
{
	u64 old_pte = *pte;
	u64 *new_table;
	u64 i = 0;
	/* level describes the parent level, we need the child ones */
	int levelshift = level2shift(level + 1);

	if (pte_type(pte) != PTE_TYPE_BLOCK){
		printf("PTE %p (%llx) is not a block. Some driver code wants to "
		      "modify dcache settings for an range not covered in "
		      "mem_map.", pte, old_pte);
		exit(0);
	}

	new_table = create_table();
	//debug("Splitting pte %p (%llx) into %p\n", pte, old_pte, new_table);

	for (i = 0; i < MAX_PTE_ENTRIES; i++) {
		new_table[i] = old_pte | (i << levelshift);

		/* Level 3 block PTEs have the table type */
		if ((level + 1) == 3)
			new_table[i] |= PTE_TYPE_TABLE;

		//debug("Setting new_table[%lld] = %llx\n", i, new_table[i]);
	}

	/* Set the new table into effect */
	set_pte_table(pte, new_table);
}

/* Add one mm_region map entry to the page tables */
static void add_map(struct mm_region *map)
{
	u64 *pte;
	u64 virt = map->virt;
	u64 phys = map->phys;
	u64 size = map->size;
	u64 attrs = map->attrs | PTE_TYPE_BLOCK | PTE_BLOCK_AF;
	u64 blocksize;
	int level;
	u64 *new_table;

	// printf("sizeof(u64)=%ld\n",sizeof(u64));

//	printf("----------> phys=0x%llx,\tvirt=0x%llx,\tsize=0x%llx,attrs=0x%llx\n",phys,virt,size,attrs);

	while (size) {
		pte = find_pte(virt, 0);
		if (pte && (pte_type(pte) == PTE_TYPE_FAULT)) {
			//debug("Creating table for virt 0x%llx\n", virt);
			new_table = create_table();
			set_pte_table(pte, new_table);
		}

		for (level = 1; level < 4; level++) {
			pte = find_pte(virt, level);
			if (!pte){
//				printf("xxxxxxxxxxxxxxxxxxxxxx pte not found\n");
				exit(0);
			}

			blocksize = 1ULL << level2shift(level);
			// printf("Checking if pte fits for virt=0x%llx size=0x%llx blocksize=0x%llx\n",virt, size, blocksize);
			if (size >= blocksize && !(virt & (blocksize - 1))) {
				/* Page fits, create block PTE */
				// printf("\t----> Setting PTE 0x%llx to block virt=0x%llx\n",pte, virt);
				if (level == 3)
					*pte = phys | attrs | PTE_TYPE_PAGE;
				else
					*pte = phys | attrs;
				virt += blocksize;
				phys += blocksize;
				size -= blocksize;
				break;
			} else if (pte_type(pte) == PTE_TYPE_FAULT) {
				/* Page doesn't fit, create subpages */
				// printf("\t----> Creating subtable for virt 0x%llx blksize=%llx\n",virt, blocksize);
				new_table = create_table();
				set_pte_table(pte, new_table);
			} else if (pte_type(pte) == PTE_TYPE_BLOCK) {
				// printf("\t----> Split block into subtable for virt 0x%llx blksize=0x%llx\n",virt, blocksize);
				split_block(pte, level);
			}else{
				// printf("\t----> else:  virt 0x%llx blksize=0x%llx\n",virt, blocksize);
			}
		}
	}
}

enum pte_type {
	PTE_INVAL,
	PTE_BLOCK,
	PTE_LEVEL,
};

/*
 * This is a recursively called function to count the number of
 * page tables we need to cover a particular PTE range. If you
 * call this with level = -1 you basically get the full 48 bit
 * coverage.
 */
static int count_required_pts(u64 addr, int level, u64 maxaddr)
{
	int levelshift = level2shift(level);
	u64 levelsize = 1ULL << levelshift;
	u64 levelmask = levelsize - 1;
	u64 levelend = addr + levelsize;
	int r = 0;
	int i;
	enum pte_type pte_type = PTE_INVAL;

	for (i = 0; mem_map[i].size || mem_map[i].attrs; i++) {
		struct mm_region *map = &mem_map[i];
		u64 start = map->virt;
		u64 end = start + map->size;

		/* Check if the PTE would overlap with the map */
		if (max(addr, start) <= min(levelend, end)) {
			start = max(addr, start);
			end = min(levelend, end);

			/* We need a sub-pt for this level */
			if ((start & levelmask) || (end & levelmask)) {
				pte_type = PTE_LEVEL;
				break;
			}

			/* Lv0 can not do block PTEs, so do levels here too */
			if (level <= 0) {
				pte_type = PTE_LEVEL;
				break;
			}

			/* PTE is active, but fits into a block */
			pte_type = PTE_BLOCK;
		}
	}

	/*
	 * Block PTEs at this level are already covered by the parent page
	 * table, so we only need to count sub page tables.
	 */
	if (pte_type == PTE_LEVEL) {
		int sublevel = level + 1;
		u64 sublevelsize = 1ULL << level2shift(sublevel);

		/* Account for the new sub page table ... */
		r = 1;

		/* ... and for all child page tables that one might have */
		for (i = 0; i < MAX_PTE_ENTRIES; i++) {
			r += count_required_pts(addr, sublevel, maxaddr);
			addr += sublevelsize;

			if (addr >= maxaddr) {
				/*
				 * We reached the end of address space, no need
				 * to look any further.
				 */
				break;
			}
		}
	}

	return r;
}

/* Returns the estimated required size of all page tables */
u64 get_page_table_size(void)
{
	u64 one_pt = MAX_PTE_ENTRIES * sizeof(u64);
	u64 size = 0;
	u64 va_bits;
	int start_level = 0;

	get_tcr(0, NULL, &va_bits);
	if (va_bits < 39)
		start_level = 1;

	/* Account for all page tables we would need to cover our memory map */
	size = one_pt * count_required_pts(0, start_level - 1, 1ULL << va_bits);

	/*
	 * We need to duplicate our page table once to have an emergency pt to
	 * resort to when splitting page tables later on
	 */
	size *= 2;

	/*
	 * We may need to split page tables later on if dcache settings change,
	 * so reserve up to 4 (random pick) page tables for that.
	 */
	size += one_pt * 4;

	return size;
}

void setup_pgtables(void)
{
	uart_puts("[setup_pgtables()]\n");

	int i;

	if (!gd.arch.tlb_fillptr || !gd.arch.tlb_addr){
		// printf("Page table pointer not setup.");
		exit(0);
	}

	/*
	 * Allocate the first level we're on with invalidate entries.
	 * If the starting level is 0 (va_bits >= 39), then this is our
	 * Lv0 page table, otherwise it's the entry Lv1 page table.
	 */
	create_table();

	/* Now add all MMU table entries one after another to the table */
	for (i = 0; mem_map[i].size || mem_map[i].attrs; i++)
		add_map(&mem_map[i]);
}

static void setup_all_pgtables(void)
{
	u64 tlb_addr = gd.arch.tlb_addr;
	u64 tlb_size = gd.arch.tlb_size;

	/* Reset the fill ptr */
	gd.arch.tlb_fillptr = tlb_addr;

	/* Create normal system page tables */
	setup_pgtables();
	// printf("\r\n %s:%d ----------> tlb_addr at %x,tlb_size=%d ", __FILE__,__LINE__,tlb_addr,tlb_size);

	/* Create emergency page tables */
	gd.arch.tlb_size -= (u64)gd.arch.tlb_fillptr - (u64)gd.arch.tlb_addr;
	gd.arch.tlb_addr = gd.arch.tlb_fillptr;
	setup_pgtables();
	gd.arch.tlb_emerg = gd.arch.tlb_addr;
	gd.arch.tlb_addr = tlb_addr;
	gd.arch.tlb_size = tlb_size;

	// printf("\r\n %s:%d ----------> tlb_addr at %x,tlb_size=%d ", __FILE__,__LINE__,tlb_addr,tlb_size);
}

/* to activate the MMU we need to set up virtual memory */
void mmu_setup(void)
{
	int el;
	/* Set up page tables only once */
	if (!gd.arch.tlb_fillptr)
		setup_all_pgtables();

	el = current_el();
	// printf("\r\n %s:%d ----------> el=%d, mmu_setup at %x ", __FILE__,__LINE__,el,&mmu_setup);
	set_ttbr_tcr_mair(el, gd.arch.tlb_addr, get_tcr(el, NULL, NULL),
			  MEMORY_ATTRIBUTES);

	/* enable the mmu */
	set_sctlr(get_sctlr() | CR_M);
	set_sctlr(get_sctlr() & ~CR_SA);
}

/*
 * Performs a invalidation of the entire data cache at all levels
 */
void invalidate_dcache_all(void)
{
	__asm_invalidate_dcache_all();
	__asm_invalidate_l3_dcache();
}

/*
 * Performs a clean & invalidation of the entire data cache at all levels.
 * This function needs to be inline to avoid using stack.
 * __asm_flush_l3_dcache return status of timeout
 */
void flush_dcache_all(void)
{
	int ret;

	__asm_flush_dcache_all();
	ret = __asm_flush_l3_dcache();
	// if (ret)
		//debug("flushing dcache returns 0x%x\n", ret);
	// else
		//debug("flushing dcache successfully.\n");
}

/*
 * Invalidates range in all levels of D-cache/unified cache
 */
void invalidate_dcache_range(unsigned long start, unsigned long stop)
{
	__asm_invalidate_dcache_range(start, stop);
}

/*
 * Flush range(clean & invalidate) from all levels of D-cache/unified cache
 */
void flush_dcache_range(unsigned long start, unsigned long stop)
{
	__asm_flush_dcache_range(start, stop);
}

void dcache_enable(void)
{
	/* The data cache is not active unless the mmu is enabled */
	if (!(get_sctlr() & CR_M)) {
		invalidate_dcache_all();
		__asm_invalidate_tlb_all();
		mmu_setup();
	}

	set_sctlr(get_sctlr() | CR_C);
}

void dcache_disable(void)
{
	__u32 sctlr;

	sctlr = get_sctlr();

	/* if cache isn't enabled no need to disable */
	if (!(sctlr & CR_C))
		return;

	set_sctlr(sctlr & ~(CR_C|CR_M));

	flush_dcache_all();
	__asm_invalidate_tlb_all();
}

int dcache_status(void)
{
	return (get_sctlr() & CR_C) != 0;
}

u64 *arch_get_page_table(void) {
	puts("No page table offset defined\n");

	return NULL;
}

static bool is_aligned(u64 addr, u64 size, u64 align)
{
	return !(addr & (align - 1)) && !(size & (align - 1));
}

/* Use flag to indicate if attrs has more than d-cache attributes */
static u64 set_one_region(u64 start, u64 size, u64 attrs, bool flag, int level)
{
	int levelshift = level2shift(level);
	u64 levelsize = 1ULL << levelshift;
	u64 *pte = find_pte(start, level);

	/* Can we can just modify the current level block PTE? */
	if (is_aligned(start, size, levelsize)) {
		if (flag) {
			*pte &= ~PMD_ATTRMASK;
			*pte |= attrs & PMD_ATTRMASK;
		} else {
			*pte &= ~PMD_ATTRINDX_MASK;
			*pte |= attrs & PMD_ATTRINDX_MASK;
		}
		//debug("Set attrs=%llx pte=%p level=%d\n", attrs, pte, level);

		return levelsize;
	}

	/* Unaligned or doesn't fit, maybe split block into table */
	//debug("addr=%llx level=%d pte=%p (%llx)\n", start, level, pte, *pte);

	/* Maybe we need to split the block into a table */
	if (pte_type(pte) == PTE_TYPE_BLOCK)
		split_block(pte, level);

	/* And then double-check it became a table or already is one */
	if (pte_type(pte) != PTE_TYPE_TABLE){
		// printf("PTE %p (%llx) for addr=%llx should be a table",
		//       pte, *pte, start);
		exit(0);
	}

	/* Roll on to the next page table level */
	return 0;
}

void mmu_set_region_dcache_behaviour(phys_addr_t start, u64 size,
				     enum dcache_option option)
{
	u64 attrs = PMD_ATTRINDX(option);
	u64 real_start = start;
	u64 real_size = size;

	//debug("start=%lx size=%lx\n", (ulong)start, (ulong)size);

	if (!gd.arch.tlb_emerg){
		// printf("Emergency page table not setup.");
		exit(0);
	}

	/*
	 * We can not modify page tables that we're currently running on,
	 * so we first need to switch to the "emergency" page tables where
	 * we can safely modify our primary page tables and then switch back
	 */
	__asm_switch_ttbr(gd.arch.tlb_emerg);

	/*
	 * Loop through the address range until we find a page granule that fits
	 * our alignment constraints, then set it to the new cache attributes
	 */
	while (size > 0) {
		int level;
		u64 r;

		for (level = 1; level < 4; level++) {
			/* Set d-cache attributes only */
			r = set_one_region(start, size, attrs, false, level);
			if (r) {
				/* PTE successfully replaced */
				size -= r;
				start += r;
				break;
			}
		}

	}

	/* We're done modifying page tables, switch back to our primary ones */
	__asm_switch_ttbr(gd.arch.tlb_addr);

	/*
	 * Make sure there's nothing stale in dcache for a region that might
	 * have caches off now
	 */
	flush_dcache_range(real_start, real_start + real_size);
}

/*
 * Modify MMU table for a region with updated PXN/UXN/Memory type/valid bits.
 * The procecess is break-before-make. The target region will be marked as
 * invalid during the process of changing.
 */
void mmu_change_region_attr(phys_addr_t addr, u64 siz, u64 attrs)
{
	int level;
	u64 r, size, start;

	start = addr;
	size = siz;
	/*
	 * Loop through the address range until we find a page granule that fits
	 * our alignment constraints, then set it to "invalid".
	 */
	while (size > 0) {
		for (level = 1; level < 4; level++) {
			/* Set PTE to fault */
			r = set_one_region(start, size, PTE_TYPE_FAULT, true,
					   level);
			if (r) {
				/* PTE successfully invalidated */
				size -= r;
				start += r;
				break;
			}
		}
	}

	flush_dcache_range(gd.arch.tlb_addr,
			   gd.arch.tlb_addr + gd.arch.tlb_size);
	__asm_invalidate_tlb_all();

	/*
	 * Loop through the address range until we find a page granule that fits
	 * our alignment constraints, then set it to the new cache attributes
	 */
	start = addr;
	size = siz;
	while (size > 0) {
		for (level = 1; level < 4; level++) {
			/* Set PTE to new attributes */
			r = set_one_region(start, size, attrs, true, level);
			if (r) {
				/* PTE successfully updated */
				size -= r;
				start += r;
				break;
			}
		}
	}
	flush_dcache_range(gd.arch.tlb_addr,
			   gd.arch.tlb_addr + gd.arch.tlb_size);
	__asm_invalidate_tlb_all();
}

void icache_enable(void)
{
	invalidate_icache_all();
	set_sctlr(get_sctlr() | CR_I);
}

void icache_disable(void)
{
	set_sctlr(get_sctlr() & ~CR_I);
}

int icache_status(void)
{
	return (get_sctlr() & CR_I) != 0;
}

void invalidate_icache_all(void)
{
	__asm_invalidate_icache_all();
	__asm_invalidate_l3_icache();
}

int mmu_status(void){
	return (get_sctlr() & CR_M) != 0;
}
void printBits(u64 const size, void const * const ptr)
{
    unsigned char *b = (unsigned char*) ptr;
    unsigned char byte;
    int i, j;

    for (i=size-1;i>=0;i--)
    {
        for (j=7;j>=0;j--)
        {
            byte = (b[i] >> j) & 1;
            printf("%u", byte);
        }
    }
    puts("");
}


/*
 * Enable dCache & iCache, whether cache is actually enabled
 * depend on CONFIG_SYS_DCACHE_OFF and CONFIG_SYS_ICACHE_OFF
 */
void enable_caches(void)
{
	// This setting is copied from u-boot
	gd.arch.tlb_addr=&mmu_start;	/* 0x50000000 , see linker.ld */
	gd.arch.tlb_size=0xe000;

	uart_puts("mmu_start="); uart_puthex(&mmu_start); uart_puts("\n");


	// Starting MMU
	icache_enable();
	dcache_enable();

	/* After starting MMU, gcc stdlib(printf, malloc, memset) works fine. */

	u64 sctlr_el1 = get_sctlr_el1();
	printBits(sizeof(sctlr_el1), &sctlr_el1);
	printf("get_sctlr_el1=0x%x\n",get_sctlr_el1());
	printf("get_ttbr0_el1=0x%x\n",get_ttbr0_el1());
	printf("get_tcr_el1=0x%x\n",get_tcr_el1());
	printf("get_mair_el1=0x%x\n",get_mair_el1());
	printf("gd=0x%x\n",&gd);


	printf("icache_status=%d\n",icache_status());
	printf("dcache_status=%d\n",dcache_status());
	printf("mmu_status=%d\n",mmu_status());
	printf("get_page_table_size=%d (0x%x)\n",get_page_table_size(),get_page_table_size());


	printf("\r\n");
	printf("\r\n");
	printf("\r\n");
	printf("\r\n");
	printf("############## 1. enable_caches() done ###################\n");
	char *data;
    short *sp1, *sp2;
    int *ip;
    data = (char *)malloc(16);

    printf("############## malloc() done , data=0x%x ###################\n",data);

    sp1 = (short*)(data+5);
    sp2 = (short*)(data+9);
    ip = (int*)(data+13);
    *sp1=10;
    *sp2=20;
    *ip=30;

    printf("############## assign value done ###################\n");

    printf("%d %d %d\n", *sp1, *sp2, *ip);

    uart_puts("############## 2. enable_caches() done ###################\n");
}
