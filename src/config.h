#ifndef _CONFIG_H_
#define _CONFIG_H_

/* VirtIO Net Configuration for ARM virt machine */
#define CONFIG_VIRTIO_NET 1
#define CONFIG_VIRTIO_NET_BASE 0x0a000000  /* First virtio-mmio device on virt machine */
#define CONFIG_VIRTIO_NET_IRQ  48          /* SPI 16 (32+16=48) for virtio-mmio@0a000000 */

#endif /* _CONFIG_H_ */
