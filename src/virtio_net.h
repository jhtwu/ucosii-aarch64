/*
 * VirtIO Network Driver Header
 * Based on SMC911x driver structure
 * Compatible with ARM virt machine and GICv3
 */

#ifndef _VIRTIO_NET_H_
#define _VIRTIO_NET_H_

#include <config.h>
#include <asm/types.h>
#include <net.h>
#include <asm/system.h>

#define DRIVERNAME "virtio-net"

/* VirtIO MMIO Device Layout */
#define VIRTIO_MMIO_MAGIC_VALUE         0x000
#define VIRTIO_MMIO_VERSION             0x004
#define VIRTIO_MMIO_DEVICE_ID           0x008
#define VIRTIO_MMIO_VENDOR_ID           0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE     0x028  /* legacy VirtIO v1 only */
#define VIRTIO_MMIO_QUEUE_SEL           0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034
#define VIRTIO_MMIO_QUEUE_NUM           0x038
#define VIRTIO_MMIO_QUEUE_READY         0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064
#define VIRTIO_MMIO_STATUS              0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW     0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH    0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW      0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH     0x0a4
#define VIRTIO_MMIO_CONFIG              0x100

/* VirtIO Status Bits */
#define VIRTIO_STATUS_ACKNOWLEDGE       1
#define VIRTIO_STATUS_DRIVER            2
#define VIRTIO_STATUS_DRIVER_OK         4
#define VIRTIO_STATUS_FEATURES_OK       8
#define VIRTIO_STATUS_FAILED            128

/* VirtIO Device IDs */
#define VIRTIO_ID_NET                   1

/* VirtIO Feature Bits */
#define VIRTIO_NET_F_CSUM               0   /* Host handles pkts w/ partial csum */
#define VIRTIO_NET_F_GUEST_CSUM         1   /* Guest handles pkts w/ partial csum */
#define VIRTIO_NET_F_MAC                5   /* Host has given MAC address. */
#define VIRTIO_NET_F_STATUS             16  /* virtio_net_config.status available */

/* VirtIO Net Config */
struct virtio_net_config {
    u8 mac[6];
    u16 status;
    u16 max_virtqueue_pairs;
} __attribute__((packed));

/* VirtQueue Descriptor */
struct vring_desc {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
} __attribute__((packed));

#define VRING_DESC_F_NEXT       1
#define VRING_DESC_F_WRITE      2

/* VirtQueue Available Ring */
struct vring_avail {
    u16 flags;
    u16 idx;
    u16 ring[];
} __attribute__((packed));

/* VirtQueue Used Ring */
struct vring_used_elem {
    u32 id;
    u32 len;
} __attribute__((packed));

struct vring_used {
    u16 flags;
    u16 idx;
    struct vring_used_elem ring[];
} __attribute__((packed));

/* VirtIO Net Header */
struct virtio_net_hdr {
    u8 flags;
    u8 gso_type;
    u16 hdr_len;
    u16 gso_size;
    u16 csum_start;
    u16 csum_offset;
} __attribute__((packed));

/* Queue sizes */
#define VIRTIO_NET_QUEUE_SIZE   64
#define VIRTIO_NET_RX_QUEUE     0
#define VIRTIO_NET_TX_QUEUE     1

/* VirtIO Net Device Structure */
struct virtio_net_dev {
    struct eth_device eth_dev;

    /* MMIO base address */
    unsigned long iobase;

    /* Queues */
    struct vring_desc *rx_desc;
    struct vring_avail *rx_avail;
    struct vring_used *rx_used;

    struct vring_desc *tx_desc;
    struct vring_avail *tx_avail;
    struct vring_used *tx_used;

    /* Queue indices */
    u16 rx_last_used;
    u16 tx_last_used;

    /* Buffers */
    u8 *rx_buffers[VIRTIO_NET_QUEUE_SIZE];
    u8 *tx_buffers[VIRTIO_NET_QUEUE_SIZE];

    /* IRQ number for GICv3 */
    u32 irq;
};

/* Register access functions */
static inline u32 virtio_mmio_read(struct virtio_net_dev *dev, u32 offset)
{
    u32 val;
    val = *(volatile u32*)(dev->iobase + offset);
    __asm__ volatile("dsb sy" ::: "memory");  /* Data Synchronization Barrier */
    return val;
}

static inline void virtio_mmio_write(struct virtio_net_dev *dev, u32 offset, u32 val)
{
    __asm__ volatile("dsb sy" ::: "memory");  /* Data Synchronization Barrier */
    *(volatile u32*)(dev->iobase + offset) = val;
    __asm__ volatile("dsb sy" ::: "memory");  /* Ensure write completes */
}

/* Function declarations */
int virtio_net_initialize(unsigned long base_addr, u32 irq);
int virtio_net_send(struct eth_device *dev, void *packet, int length);
int virtio_net_rx(struct eth_device *dev);
void virtio_net_halt(struct eth_device *dev);

extern struct virtio_net_dev *virtio_net_device;

#endif /* _VIRTIO_NET_H_ */
