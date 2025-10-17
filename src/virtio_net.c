/*
 * VirtIO Network Driver
 * Based on SMC911x driver structure
 * Compatible with ARM virt machine and GICv3
 */

#include <config.h>
#include <asm/types.h>
#include <stdint.h>
#include <malloc.h>
#include <net.h>
#include <asm/system.h>
#include <stddef.h>
#include <stdbool.h>
#include "virtio_net.h"
#include "includes.h"

#define VIRTIO_NET_MAX_DEVICES 2

static struct virtio_net_dev *virtio_net_device_list[VIRTIO_NET_MAX_DEVICES];
static size_t virtio_net_device_count;
struct virtio_net_dev *virtio_net_device = NULL;

/* Timer functions from SMC911x */
extern int get_timer(void);
extern void udelay(int nsec);
extern void mdelay(int second);

/* Default MAC address */
static uchar mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

/* Packet buffers */
static uchar net_pkt_buf[(PKTBUFSRX+1) * PKTSIZE_ALIGN + PKTALIGN];
extern uchar *net_rx_packets[PKTBUFSRX];

#define VIRTIO_QUEUE_ALIGN 4096u

static void *virtio_alloc_queue_mem(size_t size)
{
    void *base;
    uintptr_t aligned;

    base = malloc(size + VIRTIO_QUEUE_ALIGN - 1u);
    if (!base) {
        return NULL;
    }

    aligned = (uintptr_t)base;
    aligned = (aligned + (VIRTIO_QUEUE_ALIGN - 1u)) & ~(uintptr_t)(VIRTIO_QUEUE_ALIGN - 1u);

    return (void *)aligned;
}

/* Helper function to align addresses */
static inline u64 virt_to_phys(void *ptr)
{
    return (u64)ptr;
}

/* Initialize virtqueue */
static int virtio_net_init_queue(struct virtio_net_dev *dev, int queue_num,
                                  struct vring_desc **desc,
                                  struct vring_avail **avail,
                                  struct vring_used **used)
{
    u32 queue_size;
    u64 desc_addr, avail_addr, used_addr;
    int i;

    /* Select queue */
    virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_SEL, queue_num);

    /* Ensure queue is disabled before configuration */
    virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_READY, 0);

    /* Get queue size */
    queue_size = virtio_mmio_read(dev, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (queue_size == 0) {
        printf(DRIVERNAME ": Queue %d not available\n", queue_num);
        return -1;
    }

    if (queue_size > VIRTIO_NET_QUEUE_SIZE) {
        queue_size = VIRTIO_NET_QUEUE_SIZE;
    }

    printf(DRIVERNAME ": Queue %d size: %d\n", queue_num, queue_size);

    /* Allocate queue structures */
    *desc = (struct vring_desc *)virtio_alloc_queue_mem(sizeof(struct vring_desc) * queue_size);
    *avail = (struct vring_avail *)virtio_alloc_queue_mem(sizeof(struct vring_avail) + sizeof(u16) * queue_size);
    *used = (struct vring_used *)virtio_alloc_queue_mem(sizeof(struct vring_used) + sizeof(struct vring_used_elem) * queue_size);

    if (!*desc || !*avail || !*used) {
        printf(DRIVERNAME ": Failed to allocate queue structures\n");
        return -1;
    }

    memset(*desc, 0, sizeof(struct vring_desc) * queue_size);
    memset(*avail, 0, sizeof(struct vring_avail) + sizeof(u16) * queue_size);
    memset(*used, 0, sizeof(struct vring_used) + sizeof(struct vring_used_elem) * queue_size);

    /* Set queue addresses */
    desc_addr = virt_to_phys(*desc);
    avail_addr = virt_to_phys(*avail);
    used_addr = virt_to_phys(*used);

    virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_NUM, queue_size);
    virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_DESC_LOW, (u32)desc_addr);
    virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_DESC_HIGH, (u32)(desc_addr >> 32));
    virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_AVAIL_LOW, (u32)avail_addr);
    virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (u32)(avail_addr >> 32));
    virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_USED_LOW, (u32)used_addr);
    virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_USED_HIGH, (u32)(used_addr >> 32));

    /* Mark queue as ready */
    virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_READY, 1);

    /* Allocate buffers for RX queue */
    if (queue_num == VIRTIO_NET_RX_QUEUE) {
        for (i = 0; i < queue_size; i++) {
            dev->rx_buffers[i] = (u8 *)malloc(PKTSIZE_ALIGN + sizeof(struct virtio_net_hdr));
            if (!dev->rx_buffers[i]) {
                printf(DRIVERNAME ": Failed to allocate RX buffer %d\n", i);
                return -1;
            }

            /* Setup descriptor */
            (*desc)[i].addr = virt_to_phys(dev->rx_buffers[i]);
            (*desc)[i].len = PKTSIZE_ALIGN + sizeof(struct virtio_net_hdr);
            (*desc)[i].flags = VRING_DESC_F_WRITE;
            (*desc)[i].next = 0;

            /* Add to available ring */
            (*avail)->ring[i] = i;
        }
        (*avail)->idx = queue_size;

        /* Kick the device so it notices newly available RX buffers */
        virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, queue_num);
    }

    return 0;
}

/* Enqueue RX packet descriptor (called from ISR) */
static inline int virtio_net_rx_enqueue(struct virtio_net_dev *dev, u16 buffer_id, u16 len)
{
    u16 next_head = (dev->rx_pkt_queue_head + 1) % VIRTIO_NET_RX_PKT_QUEUE_SIZE;

    /* Check if queue is full */
    if (next_head == dev->rx_pkt_queue_tail) {
        return -1;  /* Queue full */
    }

    /* Enqueue packet descriptor (no packet copy!) */
    dev->rx_pkt_queue[dev->rx_pkt_queue_head].buffer_id = buffer_id;
    dev->rx_pkt_queue[dev->rx_pkt_queue_head].len = len;

    /* Memory barrier before updating head */
    __asm__ volatile("dmb sy" ::: "memory");

    dev->rx_pkt_queue_head = next_head;

    return 0;
}

/* RX processing task - runs at task level, not in ISR context */
static void virtio_net_rx_task(void *arg)
{
    struct virtio_net_dev *dev = (struct virtio_net_dev *)arg;
    INT8U err;
    u16 tail;
    u16 head;
    u16 buffer_id;
    u16 pktlen;
    u8 *pkt;
    int processed;

    while (1) {
        /* Wait for packets (blocking on semaphore) */
        OSSemPend(dev->rx_sem, 0, &err);
        if (err != OS_ERR_NONE) {
            continue;
        }

        processed = 0;

        /* Process all enqueued packets */
        while (1) {
            tail = dev->rx_pkt_queue_tail;

            /* Memory barrier before reading head */
            __asm__ volatile("dmb sy" ::: "memory");

            head = dev->rx_pkt_queue_head;

            /* Check if queue is empty */
            if (tail == head) {
                break;
            }

            /* Dequeue packet descriptor */
            buffer_id = dev->rx_pkt_queue[tail].buffer_id;
            pktlen = dev->rx_pkt_queue[tail].len;

            /* Get packet pointer (skip virtio_net_hdr) */
            pkt = dev->rx_buffers[buffer_id] + sizeof(struct virtio_net_hdr);

            /* Process packet directly from RX buffer (no copy!) */
            net_process_received_packet(pkt, pktlen);
            dev->rx_packet_count++;

            /* Recycle buffer - make it available to device again */
            dev->rx_avail->ring[dev->rx_avail->idx % VIRTIO_NET_QUEUE_SIZE] = buffer_id;
            dev->rx_avail->idx++;
            processed++;

            /* Update tail pointer */
            dev->rx_pkt_queue_tail = (tail + 1) % VIRTIO_NET_RX_PKT_QUEUE_SIZE;
        }

        /* Notify device of recycled buffers (batch notification) */
        if (processed > 0) {
            virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_RX_QUEUE);
        }
    }
}

/* VirtIO interrupt handler for GICv3 */
int BSP_OS_VirtioNetHandler(unsigned int cpu_id)
{
    (void)cpu_id;

    for (size_t i = 0; i < virtio_net_device_count; ++i) {
        struct virtio_net_dev *dev = virtio_net_device_list[i];
        u32 int_status;
        u16 last_used;
        struct vring_used_elem *elem;
        u32 pktlen;
        int enqueued = 0;

        if (!dev) {
            continue;
        }

        int_status = virtio_mmio_read(dev, VIRTIO_MMIO_INTERRUPT_STATUS);
        if (int_status == 0) {
            continue;
        }

        virtio_mmio_write(dev, VIRTIO_MMIO_INTERRUPT_ACK, int_status);

        if (int_status & 0x1) {  /* Used buffer notification */
            /* Lazy cleanup of TX completions (amortized cost) */
            dev->tx_last_used = dev->tx_used->idx;

            dev->irq_count++;

            /* Fast path: Enqueue RX packets without processing */
            last_used = dev->rx_last_used;
            while (last_used != dev->rx_used->idx) {
                elem = &dev->rx_used->ring[last_used % VIRTIO_NET_QUEUE_SIZE];

                if (elem->len < sizeof(struct virtio_net_hdr)) {
                    last_used++;
                    continue;
                }

                pktlen = elem->len - sizeof(struct virtio_net_hdr);

                /* Enqueue packet descriptor (no copy!) */
                if (pktlen > 0 && pktlen <= PKTSIZE_ALIGN) {
                    if (virtio_net_rx_enqueue(dev, elem->id, pktlen) == 0) {
                        enqueued++;
                    } else {
                        /* Queue full - will process on next interrupt */
                        break;
                    }
                }

                last_used++;
            }

            dev->rx_last_used = last_used;

            /* Wake up RX task if packets were enqueued */
            if (enqueued > 0 && dev->rx_sem) {
                OSSemPost(dev->rx_sem);
            }
        }
    }

    return 0;
}

/* Initialize VirtIO Net device */
static int virtio_net_init_device(struct virtio_net_dev *dev)
{
    u32 magic, version, device_id, vendor_id;
    u32 status;
    struct virtio_net_config *config;
    int i;

    printf(DRIVERNAME ": Initializing at 0x%lx, IRQ %d\n", dev->iobase, dev->irq);

    /* Test address translation */
    u64 par;
    asm("at s1e1r, %0" :: "r"(0x0a000000UL));
    asm volatile("mrs %0, par_el1" : "=r" (par));
    printf("PAR_EL1=0x%llx %s\n", par, (par & 1) ? "FAILED" : "OK");
    if (!(par & 1)) {
        u64 phys_addr = (par >> 12) << 12;
        printf("  -> Physical address: 0x%llx\n", phys_addr);
    }

    /* Verify magic value */
    magic = virtio_mmio_read(dev, VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != 0x74726976) {  /* 'virt' */
        printf(DRIVERNAME ": Invalid magic value: 0x%x\n", magic);
        return -1;
    }

    /* Check version */
    version = virtio_mmio_read(dev, VIRTIO_MMIO_VERSION);
    printf(DRIVERNAME ": Version: %d\n", version);

    /* Check device ID */
    device_id = virtio_mmio_read(dev, VIRTIO_MMIO_DEVICE_ID);
    if (device_id != VIRTIO_ID_NET && device_id != 0) {
        printf(DRIVERNAME ": Not a network device: %d\n", device_id);
        return -1;
    }

    /* Device ID 0 is used by virtio-net-device in QEMU */
    if (device_id == 0) {
        printf(DRIVERNAME ": Detected virtio-net-device (legacy device ID)\n");
    }

    vendor_id = virtio_mmio_read(dev, VIRTIO_MMIO_VENDOR_ID);
    printf(DRIVERNAME ": Vendor: 0x%x, Device: %d\n", vendor_id, device_id);

    /* Test write capability - try QUEUE_SEL which is definitely writable */
    printf(DRIVERNAME ": Testing QUEUE_SEL write (offset 0x%x)...\n", VIRTIO_MMIO_QUEUE_SEL);
    u32 old_queue_sel = virtio_mmio_read(dev, VIRTIO_MMIO_QUEUE_SEL);
    printf(DRIVERNAME ": Original QUEUE_SEL: %d\n", old_queue_sel);

    virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_SEL, 0);
    u32 test_read = virtio_mmio_read(dev, VIRTIO_MMIO_QUEUE_SEL);
    printf(DRIVERNAME ": QUEUE_SEL write test: wrote 0, read back %d %s\n",
           test_read, (test_read == 0) ? "✓" : "✗ FAILED");

    virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_SEL, 1);
    test_read = virtio_mmio_read(dev, VIRTIO_MMIO_QUEUE_SEL);
    printf(DRIVERNAME ": QUEUE_SEL write test: wrote 1, read back %d %s\n",
           test_read, (test_read == 1) ? "✓" : "✗ FAILED");

    virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_SEL, old_queue_sel);  /* Restore */

    /* Reset device */
    printf(DRIVERNAME ": Step 1 - Resetting device...\n");
    virtio_mmio_write(dev, VIRTIO_MMIO_STATUS, 0);
    status = virtio_mmio_read(dev, VIRTIO_MMIO_STATUS);
    printf(DRIVERNAME ": Step 1 - Reset complete, status=0x%x\n", status);

    /* Set ACKNOWLEDGE status bit */
    printf(DRIVERNAME ": Step 2 - Setting ACKNOWLEDGE...\n");
    printf(DRIVERNAME ": Writing 0x%x to STATUS register at offset 0x%x\n",
           VIRTIO_STATUS_ACKNOWLEDGE, VIRTIO_MMIO_STATUS);
    virtio_mmio_write(dev, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    __asm__ volatile("dsb sy" ::: "memory");  /* Data synchronization barrier */
    __asm__ volatile("isb" ::: "memory");      /* Instruction synchronization barrier */
    status = virtio_mmio_read(dev, VIRTIO_MMIO_STATUS);
    printf(DRIVERNAME ": Step 2 - Read back status=0x%x (expected 0x%x)\n",
           status, VIRTIO_STATUS_ACKNOWLEDGE);

    if (status != VIRTIO_STATUS_ACKNOWLEDGE) {
        u64 esr, far;
        asm volatile("mrs %0, esr_el1" : "=r" (esr));
        asm volatile("mrs %0, far_el1" : "=r" (far));
        printf("ESR_EL1=0x%llx, FAR_EL1=0x%llx\n", esr, far);
    }

    /* Set DRIVER status bit - must write ALL status bits cumulatively */
    printf(DRIVERNAME ": Step 3 - Setting DRIVER...\n");
    virtio_mmio_write(dev, VIRTIO_MMIO_STATUS,
                      VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    status = virtio_mmio_read(dev, VIRTIO_MMIO_STATUS);
    printf(DRIVERNAME ": Step 3 - DRIVER set, status=0x%x\n", status);

    /* For VirtIO version 1 (legacy), set guest page size */
    if (version == 1) {
        printf(DRIVERNAME ": Setting GUEST_PAGE_SIZE for legacy VirtIO v1\n");
        virtio_mmio_write(dev, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);
    }

    /* Read device features (both lower and upper 32 bits) */
    printf(DRIVERNAME ": Step 4 - Reading features...\n");
    virtio_mmio_write(dev, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    u32 features_lo = virtio_mmio_read(dev, VIRTIO_MMIO_DEVICE_FEATURES);
    virtio_mmio_write(dev, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
    u32 features_hi = virtio_mmio_read(dev, VIRTIO_MMIO_DEVICE_FEATURES);
    printf(DRIVERNAME ": Features[31:0]=0x%x, [63:32]=0x%x\n", features_lo, features_hi);

    u32 driver_features_lo = 0;
    u32 driver_features_hi = 0;
    if (features_lo & (1u << VIRTIO_NET_F_MAC)) {
        driver_features_lo |= (1u << VIRTIO_NET_F_MAC);
        printf(DRIVERNAME ": Negotiating VIRTIO_NET_F_MAC\n");
    } else {
        printf(DRIVERNAME ": WARNING: Device does not advertise VIRTIO_NET_F_MAC\n");
    }
    if (features_hi & (1u << (VIRTIO_F_VERSION_1 - 32))) {
        driver_features_hi |= (1u << (VIRTIO_F_VERSION_1 - 32));
        printf(DRIVERNAME ": Negotiating VIRTIO_F_VERSION_1\n");
    } else {
        printf(DRIVERNAME ": ERROR: Device does not advertise VIRTIO_F_VERSION_1, cannot continue\n");
        return -1;
    }

    /* Negotiate features - write 0 to accept no features (minimal driver) */
    printf(DRIVERNAME ": Step 5 - Negotiating features (writing 0 for minimal driver)...\n");
    virtio_mmio_write(dev, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    virtio_mmio_write(dev, VIRTIO_MMIO_DRIVER_FEATURES, driver_features_lo);
    virtio_mmio_write(dev, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
    virtio_mmio_write(dev, VIRTIO_MMIO_DRIVER_FEATURES, driver_features_hi);
    printf(DRIVERNAME ": Step 5 - Features written\n");

    /* Set FEATURES_OK - write ALL status bits cumulatively */
    printf(DRIVERNAME ": Step 6 - Setting FEATURES_OK...\n");
    virtio_mmio_write(dev, VIRTIO_MMIO_STATUS,
                      VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    printf(DRIVERNAME ": Step 6 - FEATURES_OK set\n");

    /* Re-read status to verify FEATURES_OK */
    printf(DRIVERNAME ": Step 7 - Verifying FEATURES_OK...\n");
    status = virtio_mmio_read(dev, VIRTIO_MMIO_STATUS);
    printf(DRIVERNAME ": Status register: 0x%x\n", status);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        printf(DRIVERNAME ": ERROR: Features negotiation failed (status=0x%x)\n", status);
        printf(DRIVERNAME ": Device rejected feature negotiation\n");
        virtio_mmio_write(dev, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_FAILED);
        return -1;
    }
    printf(DRIVERNAME ": FEATURES_OK verified successfully\n");

    /* Read MAC address from config space */
    printf(DRIVERNAME ": Step 8 - Reading MAC address...\n");
    config = (struct virtio_net_config *)(dev->iobase + VIRTIO_MMIO_CONFIG);
    memcpy(dev->eth_dev.enetaddr, config->mac, 6);
    printf(DRIVERNAME ": MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           config->mac[0], config->mac[1], config->mac[2],
           config->mac[3], config->mac[4], config->mac[5]);

    /* Initialize queues */
    printf(DRIVERNAME ": Step 9 - Initializing RX queue...\n");
    if (virtio_net_init_queue(dev, VIRTIO_NET_RX_QUEUE,
                               &dev->rx_desc, &dev->rx_avail, &dev->rx_used) < 0) {
        printf(DRIVERNAME ": RX queue init failed\n");
        return -1;
    }
    printf(DRIVERNAME ": Step 9 - RX queue initialized\n");

    printf(DRIVERNAME ": Step 10 - Initializing TX queue...\n");
    if (virtio_net_init_queue(dev, VIRTIO_NET_TX_QUEUE,
                               &dev->tx_desc, &dev->tx_avail, &dev->tx_used) < 0) {
        printf(DRIVERNAME ": TX queue init failed\n");
        return -1;
    }
    printf(DRIVERNAME ": Step 10 - TX queue initialized\n");

    /* Allocate TX buffers */
    for (i = 0; i < VIRTIO_NET_QUEUE_SIZE; i++) {
        dev->tx_buffers[i] = (u8 *)malloc(PKTSIZE_ALIGN + sizeof(struct virtio_net_hdr));
        if (!dev->tx_buffers[i]) {
            printf(DRIVERNAME ": Failed to allocate TX buffer %d\n", i);
            return -1;
        }
    }

    dev->rx_last_used = 0;
    dev->tx_last_used = 0;

    /* Initialize RX packet queue */
    dev->rx_pkt_queue_head = 0;
    dev->rx_pkt_queue_tail = 0;

    /* Create semaphore for RX task wakeup */
    dev->rx_sem = OSSemCreate(0);
    if (!dev->rx_sem) {
        printf(DRIVERNAME ": Failed to create RX semaphore\n");
        return -1;
    }
    printf(DRIVERNAME ": RX semaphore created\n");

    /* Allocate stack for RX task (8KB) */
    dev->rx_task_stack = (OS_STK *)malloc(8192);
    if (!dev->rx_task_stack) {
        printf(DRIVERNAME ": Failed to allocate RX task stack\n");
        return -1;
    }

    /* Create RX processing task with unique priority for each device
     * Priority 10 + device index (higher priority = lower number) */
    dev->rx_task_prio = 10 + (u8)virtio_net_device_count;
    INT8U err = OSTaskCreate(virtio_net_rx_task,
                             (void *)dev,
                             &dev->rx_task_stack[8192/sizeof(OS_STK) - 1],
                             dev->rx_task_prio);
    if (err != OS_ERR_NONE) {
        printf(DRIVERNAME ": Failed to create RX task (err=%d)\n", err);
        return -1;
    }
    printf(DRIVERNAME ": RX processing task created (priority %d)\n", dev->rx_task_prio);

    /* Set DRIVER_OK status - write ALL status bits cumulatively */
    printf(DRIVERNAME ": Step 11 - Setting DRIVER_OK...\n");
    virtio_mmio_write(dev, VIRTIO_MMIO_STATUS,
                      VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                      VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    /* Verify DRIVER_OK was set */
    status = virtio_mmio_read(dev, VIRTIO_MMIO_STATUS);
    printf(DRIVERNAME ": Status after DRIVER_OK: 0x%x\n", status);

    printf(DRIVERNAME ": Device initialization complete (IRQ setup deferred)\n");

    return 0;
}

/* Send packet */
int virtio_net_send(struct eth_device *eth_dev, void *packet, int length)
{
    struct virtio_net_dev *dev = (struct virtio_net_dev *)eth_dev;
    struct virtio_net_hdr *hdr;
    u8 *buf;
    u16 desc_idx;
    u16 available_slots;
    u16 in_flight;

    /* Check and update completed TX descriptors */
    u16 old_last_used = dev->tx_last_used;
    dev->tx_last_used = dev->tx_used->idx;

    /* Calculate in-flight packets (handle wrap-around with modulo) */
    in_flight = (dev->tx_avail->idx - dev->tx_last_used) & 0xFFFF;
    available_slots = VIRTIO_NET_QUEUE_SIZE - in_flight;

    /* If queue is critically full, poll for completions before giving up */
    if (available_slots < 4) {
        int retries = 0;
        while (available_slots < 4 && retries < 100) {
            /* Force check the used ring again */
            dev->tx_last_used = dev->tx_used->idx;
            in_flight = (dev->tx_avail->idx - dev->tx_last_used) & 0xFFFF;
            available_slots = VIRTIO_NET_QUEUE_SIZE - in_flight;

            if (available_slots >= 4) {
                break;
            }

            /* Small delay to let device process */
            retries++;
            if (retries % 10 == 0) {
                /* Manually trigger interrupt check to update used ring */
                virtio_net_rx(&dev->eth_dev);  /* This also updates tx_last_used */
            }
        }

        if (available_slots < 2) {
            static u32 drop_count = 0;
            if ((++drop_count % 100) == 1) {
                printf("[VIRTIO_NET] TX queue full after polling! Dropped %u packets (avail=%u, used=%u, in_flight=%u)\n",
                       drop_count, dev->tx_avail->idx, dev->tx_last_used, in_flight);
            }
            return -1;
        }
    }

    /* Get next available TX buffer */
    desc_idx = dev->tx_avail->idx % VIRTIO_NET_QUEUE_SIZE;
    buf = dev->tx_buffers[desc_idx];

    /* Prepare virtio_net header */
    hdr = (struct virtio_net_hdr *)buf;
    memset(hdr, 0, sizeof(struct virtio_net_hdr));
    hdr->num_buffers = 0;

    /* Copy packet data */
    memcpy(buf + sizeof(struct virtio_net_hdr), packet, length);

    /* Setup descriptor */
    dev->tx_desc[desc_idx].addr = virt_to_phys(buf);
    dev->tx_desc[desc_idx].len = length + sizeof(struct virtio_net_hdr);
    dev->tx_desc[desc_idx].flags = 0;  /* Read-only for device */
    dev->tx_desc[desc_idx].next = 0;

    /* Add to available ring */
    dev->tx_avail->ring[dev->tx_avail->idx % VIRTIO_NET_QUEUE_SIZE] = desc_idx;
    dev->tx_avail->idx++;

    /* Notify device */
    virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_TX_QUEUE);

    /* Fire-and-forget: no waiting for completion! */
    return 0;
}

/* Receive packet */
int virtio_net_rx(struct eth_device *eth_dev)
{
    struct virtio_net_dev *dev = (struct virtio_net_dev *)eth_dev;
    u16 last_used = dev->rx_last_used;
    struct vring_used_elem *elem;
    struct virtio_net_hdr *hdr;
    u8 *pkt;
    u32 pktlen;

    /* Check if we have received packets */
    while (last_used != dev->rx_used->idx) {
        elem = &dev->rx_used->ring[last_used % VIRTIO_NET_QUEUE_SIZE];

        if (elem->len < sizeof(struct virtio_net_hdr)) {
            last_used++;
            continue;
        }

        hdr = (struct virtio_net_hdr *)dev->rx_buffers[elem->id];
        pkt = (u8 *)hdr + sizeof(struct virtio_net_hdr);
        pktlen = elem->len - sizeof(struct virtio_net_hdr);

        /* Process packet */
        if (pktlen > 0 && pktlen <= PKTSIZE_ALIGN) {
            net_process_received_packet(pkt, pktlen);
            dev->rx_packet_count++;
        }

        /* Recycle buffer - make it available again */
        dev->rx_avail->ring[dev->rx_avail->idx % VIRTIO_NET_QUEUE_SIZE] = elem->id;
        dev->rx_avail->idx++;

        last_used++;
    }

    /* Notify device of new available buffers if we recycled any */
    u16 rx_recycled = last_used - dev->rx_last_used;
    if (rx_recycled > 0) {
        virtio_mmio_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_RX_QUEUE);
    }

    dev->rx_last_used = last_used;

    return 0;
}

/* Halt device */
void virtio_net_halt(struct eth_device *eth_dev)
{
    struct virtio_net_dev *dev = (struct virtio_net_dev *)eth_dev;

    /* Reset device */
    virtio_mmio_write(dev, VIRTIO_MMIO_STATUS, 0);

    printf(DRIVERNAME ": Device halted\n");
}

/* Scan for VirtIO devices */
static int virtio_net_scan_devices(unsigned long *found_addrs, u32 *found_irqs, int max_devices)
{
    unsigned long addrs[] = {
        0x0a000000, 0x0a000200, 0x0a000400, 0x0a000600,
        0x0a000800, 0x0a000a00, 0x0a000c00, 0x0a000e00,
        0x0a001000, 0x0a001200, 0x0a001400, 0x0a001600,
        0x0a001800, 0x0a001a00, 0x0a001c00, 0x0a001e00
    };
    u32 irqs[] = {48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63};
    int found = 0;

    printf(DRIVERNAME ": Scanning for VirtIO devices...\n");

    for (int i = 0; i < (int)(sizeof(addrs) / sizeof(addrs[0])); ++i) {
        u32 magic = *(volatile u32*)(addrs[i] + VIRTIO_MMIO_MAGIC_VALUE);

        if (magic != 0x74726976) {  /* 'virt' */
            continue;
        }

        u32 version = *(volatile u32*)(addrs[i] + VIRTIO_MMIO_VERSION);
        u32 device_id = *(volatile u32*)(addrs[i] + VIRTIO_MMIO_DEVICE_ID);
        u32 vendor_id = *(volatile u32*)(addrs[i] + VIRTIO_MMIO_VENDOR_ID);

        printf(DRIVERNAME ": Found VirtIO v%d at 0x%lx, DevID=%d, VendorID=0x%x\n",
               version, addrs[i], device_id, vendor_id);

        /* Accept both VIRTIO_ID_NET (1) and legacy virtio-net-device (0) */
        if (device_id != VIRTIO_ID_NET && device_id != 0) {
            continue;
        }

        if (found < max_devices) {
            found_addrs[found] = addrs[i];
            found_irqs[found] = irqs[i];
            ++found;
        }

        if (found >= max_devices) {
            break;
        }
    }

    return found;
}

static int virtio_net_add_device(unsigned long base_addr, u32 irq)
{
    struct virtio_net_dev *dev;

    if (virtio_net_device_count >= VIRTIO_NET_MAX_DEVICES) {
        printf(DRIVERNAME ": Maximum device count reached (%lu)\n",
               (unsigned long)virtio_net_device_count);
        return 0;
    }

    dev = (struct virtio_net_dev *)malloc(sizeof(struct virtio_net_dev));
    if (!dev) {
        printf(DRIVERNAME ": Failed to allocate device structure\n");
        return -1;
    }

    memset(dev, 0, sizeof(struct virtio_net_dev));

    dev->iobase = base_addr;
    dev->irq = irq;

    if (virtio_net_init_device(dev) < 0) {
        free(dev);
        return -1;
    }

    dev->eth_dev.init = virtio_net_init_device;
    dev->eth_dev.halt = virtio_net_halt;
    dev->eth_dev.send = virtio_net_send;
    dev->eth_dev.recv = virtio_net_rx;
    dev->eth_dev.iobase = base_addr;
    sprintf(dev->eth_dev.name, "%s", DRIVERNAME);

    printf(DRIVERNAME ": Registering eth_device structure\n");
    eth_register(&dev->eth_dev);
    printf(DRIVERNAME ": Registering network interface\n");
    net_register_iface(&dev->eth_dev);

    if (virtio_net_device_count == 0) {
        virtio_net_device = dev;
    }

    virtio_net_device_list[virtio_net_device_count++] = dev;

    return 1;
}

/* Initialize VirtIO Net */
int virtio_net_initialize(unsigned long base_addr, u32 irq)
{
    unsigned long found_addrs[VIRTIO_NET_MAX_DEVICES];
    u32 found_irqs[VIRTIO_NET_MAX_DEVICES];
    int found = 0;
    int initialized = 0;

    if (virtio_net_device_count > 0) {
        return (int)virtio_net_device_count;
    }

    printf("[%s] Initializing VirtIO Net driver\n", __func__);

    found = virtio_net_scan_devices(found_addrs, found_irqs, VIRTIO_NET_MAX_DEVICES);

    if (found == 0 && base_addr != 0) {
        found_addrs[found] = base_addr;
        found_irqs[found] = irq;
        found = 1;
    }

    for (int i = 0; i < found; ++i) {
        int rc = virtio_net_add_device(found_addrs[i], found_irqs[i]);
        if (rc > 0) {
            printf(DRIVERNAME ": Registered device at 0x%lx IRQ %u\n",
                   found_addrs[i], found_irqs[i]);
            initialized += rc;
        }
    }

    if (initialized == 0) {
        printf(DRIVERNAME ": No VirtIO network devices initialized\n");
        return -1;
    }

    /* Now that all devices are initialized, set up interrupts */
    printf(DRIVERNAME ": Setting up interrupts for %zu device(s)...\n", virtio_net_device_count);
    for (size_t i = 0; i < virtio_net_device_count; ++i) {
        struct virtio_net_dev *dev = virtio_net_device_list[i];
        if (dev) {
            printf(DRIVERNAME ": Configuring IRQ %u for device %zu\n", dev->irq, i);
            BSP_IntVectSet(dev->irq, 0u, 0u, BSP_OS_VirtioNetHandler);
            BSP_IntSrcEn(dev->irq);
            printf(DRIVERNAME ": IRQ %u enabled\n", dev->irq);
        }
    }
    printf(DRIVERNAME ": All interrupts configured\n");

    return (int)virtio_net_device_count;
}

struct virtio_net_dev *virtio_net_get_device(size_t index)
{
    if (index < virtio_net_device_count) {
        return virtio_net_device_list[index];
    }
    return NULL;
}

size_t virtio_net_get_device_count(void)
{
    return virtio_net_device_count;
}
