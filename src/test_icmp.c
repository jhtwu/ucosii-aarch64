#include "smc911x.h"
#include <net.h>
#include "includes.h"
#include "virtio_net.h"

/* ARP request packet for 192.168.1.0/24 network
 * Destination MAC: ff:ff:ff:ff:ff:ff (broadcast)
 * Source MAC: 52:54:00:12:34:56 (VirtIO MAC)
 * EtherType: 0x0806 (ARP)
 * Operation: 0x0001 (ARP Request)
 * Sender MAC: 52:54:00:12:34:56
 * Sender IP: 192.168.1.1 (0xc0a80101) - Guest IP
 * Target MAC: 00:00:00:00:00:00
 * Target IP: 192.168.1.103 (0xc0a80167) - Host IP
 */
char buf[600]={0xff,0xff,0xff,0xff,0xff,0xff,0x52,0x54,0x00,0x12,0x34,0x56,0x08,0x06,0x00,0x01,
			   0x08,0x00,0x06,0x04,0x00,0x01,0x52,0x54,0x00,0x12,0x34,0x56,0xc0,0xa8,0x01,0x01,
			   0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xa8,0x01,0x67,0x00,0x00,0x00,0x00,0x00,0x00,
			   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

void send_icmp()
{
	int i=0;
	struct eth_device *dev;

#ifdef CONFIG_VIRTIO_NET
	extern struct virtio_net_dev *virtio_net_device;
	if (virtio_net_device) {
		dev = &virtio_net_device->eth_dev;
	} else
#endif
	{
		dev = ethdev;
	}

	printf(PURPLE "--------------> Sending ARP request to 192.168.1.103! dev at %p\n" NONE, dev);
	printf("Source MAC: 52:54:00:12:34:56, Source IP: 192.168.1.1\n");
	printf("Target IP: 192.168.1.103 (host br-lan)\n");

	if (dev && dev->send) {
		dev->send(dev, buf, 60);  // ARP packet is 60 bytes (42 + 18 padding)
	} else {
		printf("ERROR: No valid network device for sending!\n");
	}
}
