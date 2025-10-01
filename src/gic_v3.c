//This file is porting rom include/linux/irqchip/arm-gic-v3.h ,drivers/irqchip/irq-gic-v3.c in linux kernel 4.16.2

#include  <lib_def.h>
#include  <cpu.h>

#include  <os_cpu.h>

#include  <bsp.h>
#include  <bsp_os.h>
#include  <bsp_int.h>

typedef unsigned long long u64;
typedef unsigned int u32;

/*
 * Re-Distributor registers, offsets from RD_base
 */
#define GICR_CTLR           0x0000
#define GICR_IIDR           0x0004
#define GICR_TYPER          0x0008
#define GICR_STATUSR        0x0010
#define GICR_WAKER          0x0014
#define GICR_SETLPIR            0x0040
#define GICR_CLRLPIR            0x0048
#define GICR_SEIR           0x0068
#define GICR_PROPBASER          0x0070
#define GICR_PENDBASER          0x0078
#define GICR_INVLPIR            0x00A0
#define GICR_INVALLR            0x00B0
#define GICR_SYNCR          0x00C0
#define GICR_MOVLPIR            0x0100
#define GICR_MOVALLR            0x0110
#define GICR_IDREGS         0xFFD0
#define GICR_PIDR2          0xFFE8

#define GICR_CTLR_ENABLE_LPIS       (1UL << 0)

#define GICR_TYPER_CPU_NUMBER(r)    (((r) >> 8) & 0xffff)

#define GICR_WAKER_ProcessorSleep   (1U << 1)
#define GICR_WAKER_ChildrenAsleep   (1U << 2)

#define DIST_BASE        0x08000000
#define RDIST_RD_BASE    0x080a0000
#define RDIST_SGI_BASE   0x080b0000

#define GIC_DIST_CTRL           0x000
#define GIC_DIST_CTR            0x004
#define GIC_DIST_IIDR           0x008
#define GIC_DIST_IGROUP         0x080
#define GIC_DIST_ENABLE_SET     0x100
#define GIC_DIST_ENABLE_CLEAR       0x180
#define GIC_DIST_PENDING_SET        0x200
#define GIC_DIST_PENDING_CLEAR      0x280
#define GIC_DIST_ACTIVE_SET     0x300
#define GIC_DIST_ACTIVE_CLEAR       0x380
#define GIC_DIST_PRI            0x400
#define GIC_DIST_TARGET         0x800
#define GIC_DIST_CONFIG         0xc00
#define GIC_DIST_SOFTINT        0xf00
#define GIC_DIST_SGI_PENDING_CLEAR  0xf10
#define GIC_DIST_SGI_PENDING_SET    0xf20

#define GICD_ENABLE         0x1
#define GICD_DISABLE            0x0
#define GICD_INT_ACTLOW_LVLTRIG     0x0
#define GICD_INT_EN_CLR_X32     0xffffffff
#define GICD_INT_EN_SET_SGI     0x0000ffff
#define GICD_INT_EN_CLR_PPI     0xffff0000
#define GICD_INT_DEF_PRI        0xa0
#define GICD_INT_DEF_PRI_X4     ((GICD_INT_DEF_PRI << 24) |\
                    (GICD_INT_DEF_PRI << 16) |\
                    (GICD_INT_DEF_PRI << 8) |\
                    GICD_INT_DEF_PRI)


/*
 * Distributor registers. We assume we're running non-secure, with ARE
 * being set. Secure-only and non-ARE registers are not described.
 */
#define GICD_CTLR           0x0000
#define GICD_TYPER          0x0004
#define GICD_IIDR           0x0008
#define GICD_STATUSR            0x0010
#define GICD_SETSPI_NSR         0x0040
#define GICD_CLRSPI_NSR         0x0048
#define GICD_SETSPI_SR          0x0050
#define GICD_CLRSPI_SR          0x0058
#define GICD_SEIR           0x0068
#define GICD_IGROUPR            0x0080
#define GICD_ISENABLER          0x0100
#define GICD_ICENABLER          0x0180
#define GICD_ISPENDR            0x0200
#define GICD_ICPENDR            0x0280
#define GICD_ISACTIVER          0x0300
#define GICD_ICACTIVER          0x0380
#define GICD_IPRIORITYR         0x0400
#define GICD_ICFGR          0x0C00
#define GICD_IGRPMODR           0x0D00
#define GICD_NSACR          0x0E00
#define GICD_IROUTER            0x6000
#define GICD_IDREGS         0xFFD0
#define GICD_PIDR2          0xFFE8


/*
 * Those registers are actually from GICv2, but the spec demands that they
 * are implemented as RES0 if ARE is 1 (which we do in KVM's emulated GICv3).
 */
#define GICD_ITARGETSR          0x0800
#define GICD_SGIR           0x0F00
#define GICD_CPENDSGIR          0x0F10
#define GICD_SPENDSGIR          0x0F20

#define GICD_CTLR_RWP           (1U << 31)
#define GICD_CTLR_DS            (1U << 6)
#define GICD_CTLR_ARE_NS        (1U << 4)
#define GICD_CTLR_ENABLE_G1A        (1U << 1)
#define GICD_CTLR_ENABLE_G1     (1U << 0)


/*
 * Re-Distributor registers, offsets from SGI_base
 */
#define GICR_IGROUPR0           GICD_IGROUPR
#define GICR_ISENABLER0         GICD_ISENABLER
#define GICR_ICENABLER0         GICD_ICENABLER
#define GICR_ISPENDR0           GICD_ISPENDR
#define GICR_ICPENDR0           GICD_ICPENDR
#define GICR_ISACTIVER0         GICD_ISACTIVER
#define GICR_ICACTIVER0         GICD_ICACTIVER
#define GICR_IPRIORITYR0        GICD_IPRIORITYR
#define GICR_ICFGR0         GICD_ICFGR
#define GICR_IGRPMODR0          GICD_IGRPMODR
#define GICR_NSACR          GICD_NSACR

void writel_relaxed(int value, int addr)
{
    CPU_REG32 *ptr=addr;
    *ptr=value;
	/*uart_puts("addr: "); uart_puthex(addr);	
	uart_puts(" ,value: "); uart_puthex(value);	
	uart_puts("\n");
	*/
}

int readl_relaxed(int addr)
{
	return  *(int *)(addr);
}

void gic_enable_redist(int enable)
{
    int rbase;
    unsigned int count = 1000000;    /* 1s! */
    unsigned int val;
	uart_puts("gic_enable_redist\n");

    rbase = RDIST_RD_BASE;// gic_data_rdist_rd_base();
    //printk("[%s:%d] Enter! rbase=0x%x\n",__func__,__LINE__,rbase);

    val =  *(int *)(rbase + GICR_WAKER);
    if (enable)
        /* Wake up this CPU redistributor */
        val &= ~GICR_WAKER_ProcessorSleep;
    else
        val |= GICR_WAKER_ProcessorSleep;

    writel_relaxed(val,rbase + GICR_WAKER);

    if (!enable) {      /* Check that GICR_WAKER is writeable */
        val = *(int *)(rbase + GICR_WAKER);
        if (!(val & GICR_WAKER_ProcessorSleep))
            return; /* No PM support in this redistributor */
    }

    while (--count) {
        val = *(int *)(rbase + GICR_WAKER);
        if (enable ^ *(int *)(val & GICR_WAKER_ChildrenAsleep))
            break;
         OSTimeDlyHMSM(0, 0, 0, 1);
    };
    if (!count)
        printf("redistributor failed to %s...\n",
                   enable ? "wakeup" : "sleep");
    //printk("[%s:%d] Exit!\n",__func__,__LINE__);
}

void gic_cpu_config(void *base, void (*sync_access)(void))
{
    int i;

    //printk("[%s:%d] Enter.... base=0x%x\n",__func__,__LINE__,base);
	uart_puts("gic_cpu_config\n");
    /*
     * Deal with the banked PPI and SGI interrupts - disable all
     * PPI interrupts, ensure all SGI interrupts are enabled.
     * Make sure everything is deactivated.
     */
    writel_relaxed(GICD_INT_EN_CLR_X32, base + GIC_DIST_ACTIVE_CLEAR);
    writel_relaxed(GICD_INT_EN_CLR_PPI, base + GIC_DIST_ENABLE_CLEAR);
    writel_relaxed(GICD_INT_EN_SET_SGI, base + GIC_DIST_ENABLE_SET);

    /*
     * Set priority on PPI and SGI interrupts
     */
    for (i = 0; i < 32; i += 4)
        writel_relaxed(GICD_INT_DEF_PRI_X4,
                    base + GIC_DIST_PRI + i * 4 / 4);

    if (sync_access)
        sync_access();
    //printk("[%s:%d] Exit.... base=0x%x\n",__func__,__LINE__,base);
}

static void gic_do_wait_for_rwp(void *base)
{
    unsigned int count = 1000000;    /* 1s! */

    while (readl_relaxed(base + GICD_CTLR) & GICD_CTLR_RWP) {
        count--;
        if (!count) {
            printf("RWP timeout, gone fishing\n");
            return;
        }
        OSTimeDlyHMSM(0, 0, 0, 1);
    };
}

#define isb(option) __asm__ __volatile__ ("isb " #option : : : "memory")

/*
 * CPU interface registers
 */
#define ICC_CTLR_EL1_EOImode_SHIFT  (1)
#define ICC_CTLR_EL1_EOImode_drop_dir   (0U << ICC_CTLR_EL1_EOImode_SHIFT)
#define ICC_CTLR_EL1_EOImode_drop   (1U << ICC_CTLR_EL1_EOImode_SHIFT)
#define ICC_CTLR_EL1_EOImode_MASK   (1 << ICC_CTLR_EL1_EOImode_SHIFT)
#define ICC_CTLR_EL1_CBPR_SHIFT     0
#define ICC_CTLR_EL1_CBPR_MASK      (1 << ICC_CTLR_EL1_CBPR_SHIFT)
#define ICC_CTLR_EL1_PRI_BITS_SHIFT 8
#define ICC_CTLR_EL1_PRI_BITS_MASK  (0x7 << ICC_CTLR_EL1_PRI_BITS_SHIFT)
#define ICC_CTLR_EL1_ID_BITS_SHIFT  11
#define ICC_CTLR_EL1_ID_BITS_MASK   (0x7 << ICC_CTLR_EL1_ID_BITS_SHIFT)
#define ICC_CTLR_EL1_SEIS_SHIFT     14
#define ICC_CTLR_EL1_SEIS_MASK      (0x1 << ICC_CTLR_EL1_SEIS_SHIFT)
#define ICC_CTLR_EL1_A3V_SHIFT      15
#define ICC_CTLR_EL1_A3V_MASK       (0x1 << ICC_CTLR_EL1_A3V_SHIFT)
#define ICC_CTLR_EL1_RSS        (0x1 << 18)
#define ICC_PMR_EL1_SHIFT       0
#define ICC_PMR_EL1_MASK        (0xff << ICC_PMR_EL1_SHIFT)
#define ICC_BPR0_EL1_SHIFT      0
#define ICC_BPR0_EL1_MASK       (0x7 << ICC_BPR0_EL1_SHIFT)
#define ICC_BPR1_EL1_SHIFT      0
#define ICC_BPR1_EL1_MASK       (0x7 << ICC_BPR1_EL1_SHIFT)
#define ICC_IGRPEN0_EL1_SHIFT       0
#define ICC_IGRPEN0_EL1_MASK        (1 << ICC_IGRPEN0_EL1_SHIFT)
#define ICC_IGRPEN1_EL1_SHIFT       0
#define ICC_IGRPEN1_EL1_MASK        (1 << ICC_IGRPEN1_EL1_SHIFT)
#define ICC_SRE_EL1_DIB         (1U << 2)
#define ICC_SRE_EL1_DFB         (1U << 1)
#define ICC_SRE_EL1_SRE         (1U << 0)

/*
 * Hypervisor interface registers (SRE only)
 */
#define ICH_LR_VIRTUAL_ID_MASK      ((1ULL << 32) - 1)

#define ICH_LR_EOI          (1ULL << 41)
#define ICH_LR_GROUP            (1ULL << 60)
#define ICH_LR_HW           (1ULL << 61)
#define ICH_LR_STATE            (3ULL << 62)
#define ICH_LR_PENDING_BIT      (1ULL << 62)
#define ICH_LR_ACTIVE_BIT       (1ULL << 63)
#define ICH_LR_PHYS_ID_SHIFT        32
#define ICH_LR_PHYS_ID_MASK     (0x3ffULL << ICH_LR_PHYS_ID_SHIFT)
#define ICH_LR_PRIORITY_SHIFT       48
#define ICH_LR_PRIORITY_MASK        (0xffULL << ICH_LR_PRIORITY_SHIFT)

/* Our default, arbitrary priority value. Linux only uses one anyway. */
#define DEFAULT_PMR_VALUE	0xf0

#if 0

/*
 * ARMv8 ARM reserves the following encoding for system registers:
 * (Ref: ARMv8 ARM, Section: "System instruction class encoding overview",
 *  C5.2, version:ARM DDI 0487A.f)
 *  [20-19] : Op0
 *  [18-16] : Op1
 *  [15-12] : CRn
 *  [11-8]  : CRm
 *  [7-5]   : Op2
 */
#define Op0_shift   19
#define Op0_mask    0x3
#define Op1_shift   16
#define Op1_mask    0x7
#define CRn_shift   12
#define CRn_mask    0xf
#define CRm_shift   8
#define CRm_mask    0xf
#define Op2_shift   5
#define Op2_mask    0x7

#define sys_reg(op0, op1, crn, crm, op2) \
    (((op0) << Op0_shift) | ((op1) << Op1_shift) | \
     ((crn) << CRn_shift) | ((crm) << CRm_shift) | \
     ((op2) << Op2_shift))

#define __stringify_1(x...) #x
#define __stringify(x...)   __stringify_1(x)


/*
 * Unlike read_cpuid, calls to read_sysreg are never expected to be
 * optimized away or replaced with synthetic values.
 */
#define read_sysreg(r) ({                   \
    u64 __val;                      \
    asm volatile("mrs %0, " __stringify(r) : "=r" (__val)); \
    __val;                          \
})

/*
 * The "Z" constraint normally means a zero immediate, but when combined with
 * the "%x0" template means XZR.
 */
#define write_sysreg(v, r) do {                 \
    u64 __val = (u64)(v);                   \
    asm volatile("msr " __stringify(r) ", %x0"      \
             : : "rZ" (__val));             \
} while (0)

/*
 * For registers without architectural names, or simply unsupported by
 * GAS.
 */

#define read_sysreg_s(r) ({                     \
    u64 __val;                          \
    asm volatile("mrs %0, " __stringify(r) : "=r" (__val));   \
    __val;                              \
})

#define write_sysreg_s(v, r) do {                   \
    u64 __val = (u64)(v);                       \
    asm volatile("msr " __stringify(r) ", %x0" : : "rZ" (__val)); \
} while (0)

#endif

#define SYS_ICC_PMR_EL1         sys_reg(3, 0, 4, 6, 0)
#define SYS_ICC_CTLR_EL1        sys_reg(3, 0, 12, 12, 4)
#define SYS_ICC_IGRPEN1_EL1     sys_reg(3, 0, 12, 12, 7)
#define SYS_ICC_SRE_EL1         sys_reg(3, 0, 12, 12, 5)
#define SYS_ICC_BPR1_EL1        sys_reg(3, 0, 12, 12, 3)

/* Wait for completion of a distributor change */
static void gic_dist_wait_for_rwp(void)
{
	gic_do_wait_for_rwp(DIST_BASE);
}

/* Wait for completion of a redistributor change */
static void gic_redist_wait_for_rwp(void)
{
    gic_do_wait_for_rwp(RDIST_RD_BASE);
}

/*
From https://www.element14.com/community/servlet/JiveServlet/previewBody/41836-102-1-229511/ARM.Reference_Manual.pdf

MRS Xt, <system_register>
Move <system_register> to Xt, where <system_register> is a system register name, or for
implementation-defined registers a name of the form ?œS<op0>_<op1>_<Cn>_<Cm>_<op2>?? e.g.
?œS3_4_c13_c9_7??

MSR <system_register>, Xt
Move Xt to <system_register>, where <system_register> is a system register name, or for
implementation-defined registers a name of the form ?œS<op0>_<op1>_<Cn>_<Cm>_<op2>?? e.g.
?œS3_4_c13_c9_7??*/

/*
 * Low-level accessors
 *
 * These system registers are 32 bits, but we make sure that the compiler
 * sets the GP register's most significant bits to 0 with an explicit cast.
 */


u64 gic_read_iar(void)
{
    u64 irqstat;

//#define SYS_ICC_IAR1_EL1        sys_reg(3, 0, 12, 12, 0)
//    irqstat = read_sysreg_s(SYS_ICC_IAR1_EL1);
  //  dsb(sy);
	asm volatile("mrs %0, S3_0_c12_c12_0" : "=r" (irqstat)); 
    return irqstat;
}

static inline u32 gic_read_sre(void)
{
//#define SYS_ICC_SRE_EL1         sys_reg(3, 0, 12, 12, 5)
//    return read_sysreg_s(SYS_ICC_SRE_EL1);

	int val;
	asm volatile("mrs %0, S3_0_c12_c12_5" : "=r" (val)); 
	return val;
}


 void gic_write_eoir(u32 irq)
{
//	write_sysreg_s(irq, SYS_ICC_EOIR1_EL1);
//#define SYS_ICC_EOIR1_EL1       sys_reg(3, 0, 12, 12, 1)

	asm volatile("msr S3_0_c12_c12_1,%x0" : : "rZ" (irq));
	isb();
}

static inline void gic_write_dir(u32 irq)
{
//#define SYS_ICC_DIR_EL1         sys_reg(3, 0, 12, 11, 1)
//    write_sysreg_s(irq, SYS_ICC_DIR_EL1);

	asm volatile("msr S3_0_c12_c11_1 ,%x0" : : "rZ" (irq));
    isb();
}

static inline void gic_write_pmr(u32 val)
{
//#define SYS_ICC_PMR_EL1         sys_reg(3, 0, 4, 6, 0)
//    write_sysreg_s(val, SYS_ICC_PMR_EL1);

	asm volatile("msr S3_0_c4_c6_0, %x0" :: "rZ" (val)); 
}

static inline void gic_write_ctlr(u32 val)
{
//#define SYS_ICC_CTLR_EL1        sys_reg(3, 0, 12, 12, 4)
//    write_sysreg_s(val, SYS_ICC_CTLR_EL1);
	asm volatile("msr  S3_0_c12_c12_4, %x0" :: "rZ" (val)); 
    isb();
}

static inline void gic_write_grpen1(u32 val)
{
//#define SYS_ICC_IGRPEN1_EL1     sys_reg(3, 0, 12, 12, 7)
//    write_sysreg_s(val, SYS_ICC_IGRPEN1_EL1);
	asm volatile("msr S3_0_c12_c12_7, %x0" :: "rZ" (val)); 
    isb();
}

static inline void gic_write_sre(u32 val)
{
//#define SYS_ICC_SRE_EL1         sys_reg(3, 0, 12, 12, 5)
//    write_sysreg_s(val, SYS_ICC_SRE_EL1);
	asm volatile("msr S3_0_c12_c12_5, %x0" :: "rZ" (val)); 
    isb();
}

static inline void gic_write_bpr1(u32 val)
{
//#define SYS_ICC_BPR1_EL1        sys_reg(3, 0, 12, 12, 3)
//    write_sysreg_s(val, SYS_ICC_BPR1_EL1);
	asm volatile("msr S3_0_c12_c12_3, %x0" :: "rZ" (val)); 

}


/*
711 #define read_sysreg_s(r) ({                     \
712     u64 __val;                          \
713     asm volatile("mrs %0, " __stringify(r) : "=r" (__val));   \
714     __val;                              \
715 })
716
717 #define write_sysreg_s(v, r) do {                   \
718     u64 __val = (u64)(v);                       \
719     asm volatile("msr " __stringify(r) ", %x0" : : "rZ" (__val)); \
720 } while (0)
721

*/

static inline int gic_enable_sre(void)
{
    unsigned int  val;

	uart_puts("gic_enable_sre\n");

    val = gic_read_sre();
    if (val & ICC_SRE_EL1_SRE)
        return 1;

	uart_puts("gic_enable_sre 2\n");
    val |= ICC_SRE_EL1_SRE;
    gic_write_sre(val);
    val = gic_read_sre();

	uart_puts("gic_enable_sre 3\n");
    return !!(val & ICC_SRE_EL1_SRE);
}


static void gic_cpu_sys_reg_init(void)
{
	uart_puts("gic_cpu_sys_reg_init\n");
    //printk("[%s:%d]\n",__func__,__LINE__);
    /*
     * Need to check that the SRE bit has actually been set. If
     * not, it means that SRE is disabled at EL2. We're going to
     * die painfully, and there is nothing we can do about it.
     *
     * Kindly inform the luser.
     */
    if (!gic_enable_sre())
    //    printf("GIC: unable to set SRE (disabled at EL2), panic ahead\n");
    	uart_puts("GIC: unable to set SRE (disabled at EL2), panic ahead\n");

	/* Set priority mask register */
	gic_write_pmr(DEFAULT_PMR_VALUE);

	/*
	 * Some firmwares hand over to the kernel with the BPR changed from
	 * its reset value (and with a value large enough to prevent
	 * any pre-emptive interrupts from working at all). Writing a zero
	 * to BPR restores is reset value.
	 */
	gic_write_bpr1(0);

#if 0
	if (static_key_true(&supports_deactivate)) {
		/* EOI drops priority only (mode 1) */
		gic_write_ctlr(ICC_CTLR_EL1_EOImode_drop);
	} else
#endif
	 {
		/* EOI deactivates interrupt too (mode 0) */
		gic_write_ctlr(ICC_CTLR_EL1_EOImode_drop_dir);
	}

	/* ... and let's hit the road... */
	gic_write_grpen1(1);

}


void gic_dist_config(void *base, int gic_irqs,
		     void (*sync_access)(void))
{
	unsigned int i;

	//printk("[%s:%d] base=0x%x\n",__func__,__LINE__,base);
	/*
	 * Set all global interrupts to be level triggered, active low.
	 */
	for (i = 32; i < gic_irqs; i += 16)
		writel_relaxed(GICD_INT_ACTLOW_LVLTRIG,
					base + GIC_DIST_CONFIG + i / 4);

	/*
	 * Set priority on all global interrupts.
	 */
	for (i = 32; i < gic_irqs; i += 4)
		writel_relaxed(GICD_INT_DEF_PRI_X4, base + GIC_DIST_PRI + i);

	/*
	 * Deactivate and disable all SPIs. Leave the PPI and SGIs
	 * alone as they are in the redistributor registers on GICv3.
	 */
	for (i = 32; i < gic_irqs; i += 32) {
		writel_relaxed(GICD_INT_EN_CLR_X32,
			       base + GIC_DIST_ACTIVE_CLEAR + i / 8);
		writel_relaxed(GICD_INT_EN_CLR_X32,
			       base + GIC_DIST_ENABLE_CLEAR + i / 8);
	}

	if (sync_access)
		sync_access();
}

/*
 * Even in 32bit systems that use LPAE, there is no guarantee that the I/O
 * interface provides true 64bit atomic accesses, so using strd/ldrd doesn't
 * make much sense.
 * Moreover, 64bit I/O emulation is extremely difficult to implement on
 * AArch32, since the syndrome register doesn't provide any information for
 * them.
 * Consequently, the following IO helpers use 32bit accesses.
 */
static inline void __gic_writeq_nonatomic(unsigned long long val, volatile void *addr)
{
	writel_relaxed((unsigned int)val, addr);
	writel_relaxed((unsigned int)(val >> 32), addr + 4);
}

/*
 *  GICD_IROUTERn, contain the affinity values associated to each interrupt.
 *  The upper-word (aff3) will always be 0, so there is no need for a lock.
 */
#define gic_write_irouter(v, c)		__gic_writeq_nonatomic(v, c)

static void gic_dist_init(void)
{
	void *base=DIST_BASE;
	unsigned int i;
	unsigned long long affinity;
	uart_puts("gic_dist_nit\n");
	writel_relaxed(0, base + GICD_CTLR);
	gic_dist_wait_for_rwp();

	/*
	 * Configure SPIs as non-secure Group-1. This will only matter
	 * if the GIC only has a single security state. This will not
	 * do the right thing if the kernel is running in secure mode,
	 * but that's not the intended use case anyway.
	 */
	for (i = 32; i < 256; i += 32)
		writel_relaxed(~0, base + GICD_IGROUPR + i / 8);

	gic_dist_config(base, 256, gic_dist_wait_for_rwp);

	/* Enable distributor with ARE, Group1 */
	writel_relaxed(GICD_CTLR_ARE_NS | GICD_CTLR_ENABLE_G1A | GICD_CTLR_ENABLE_G1,
		       base + GICD_CTLR);

	/*
	 * Set all global interrupts to the boot CPU only. ARE must be
	 * enabled.
	 */
	//affinity = gic_mpidr_to_affinity(cpu_logical_map(smp_processor_id()));
	affinity = 0; //set value as buildroot directly
	for (i = 32; i < 256; i++)
		gic_write_irouter(affinity, base + GICD_IROUTER + i * 8);
}                                       

void gic_cpu_init()
{
	void *rbase=RDIST_SGI_BASE;
	uart_puts("gic_cpu_nit\n");

	gic_enable_redist(1);

	/* Configure SGIs/PPIs as non-secure Group-1 */
	writel_relaxed(~0,rbase + GICR_IGROUPR0);

	gic_cpu_config(rbase, gic_redist_wait_for_rwp);

    /* initialise system registers */
	gic_cpu_sys_reg_init(); 

}

void gic_v3_init() //refer to gic_init_bases in drivers/irqchip/irq-gic-v3.c
{
	//printf("%s!\n",__func__);
	uart_puts("gic_v3_init\n");
	gic_dist_init();	
	gic_cpu_init();
	uart_puts("end of gic_v3_init\n");
}
