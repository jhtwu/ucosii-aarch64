
#include "virtio_net.h"
#include <net.h>

static struct eth_device *eth_devices;
struct eth_device *eth_current;

int eth_register(struct eth_device *dev)
{
	struct eth_device *d;
	static int index;

//Ruby marked
//	assert(strlen(dev->name) < sizeof(dev->name));

	if (!eth_devices) {
		eth_devices = dev;
		eth_current = dev;
		//ruby modified
		//eth_current_changed();
	} else {
		for (d = eth_devices; d->next != eth_devices; d = d->next)
			;
		d->next = dev;
	}

	dev->state = ETH_STATE_INIT;
	dev->next  = eth_devices;
	dev->index = index++;

	return 0;
}

/* Moved to net_protocol.c */
void net_process_received_packet_old(uchar *in_packet, int len)
{
#if 0
	struct ethernet_hdr *et;
	struct ip_udp_hdr *ip;
	struct in_addr dst_ip;
	struct in_addr src_ip;
	int eth_proto;
#if defined(CONFIG_CMD_CDP)
	int iscdp;
#endif
	ushort cti = 0, vlanid = VLAN_NONE, myvlanid, mynvlanid;

	debug_cond(DEBUG_NET_PKT, "packet received\n");

	net_rx_packet = in_packet;
	net_rx_packet_len = len;
	et = (struct ethernet_hdr *)in_packet;

	/* too small packet? */
	if (len < ETHER_HDR_SIZE)
		return;

#if defined(CONFIG_API) || defined(CONFIG_EFI_LOADER)
	if (push_packet) {
		(*push_packet)(in_packet, len);
		return;
	}
#endif

#if defined(CONFIG_CMD_CDP)
	/* keep track if packet is CDP */
	iscdp = is_cdp_packet(et->et_dest);
#endif

	myvlanid = ntohs(net_our_vlan);
	if (myvlanid == (ushort)-1)
		myvlanid = VLAN_NONE;
	mynvlanid = ntohs(net_native_vlan);
	if (mynvlanid == (ushort)-1)
		mynvlanid = VLAN_NONE;

	eth_proto = ntohs(et->et_protlen);

	if (eth_proto < 1514) {
		struct e802_hdr *et802 = (struct e802_hdr *)et;
		/*
		 *	Got a 802.2 packet.  Check the other protocol field.
		 *	XXX VLAN over 802.2+SNAP not implemented!
		 */
		eth_proto = ntohs(et802->et_prot);

		ip = (struct ip_udp_hdr *)(in_packet + E802_HDR_SIZE);
		len -= E802_HDR_SIZE;

	} else if (eth_proto != PROT_VLAN) {	/* normal packet */
		ip = (struct ip_udp_hdr *)(in_packet + ETHER_HDR_SIZE);
		len -= ETHER_HDR_SIZE;

	} else {			/* VLAN packet */
		struct vlan_ethernet_hdr *vet =
			(struct vlan_ethernet_hdr *)et;

		debug_cond(DEBUG_NET_PKT, "VLAN packet received\n");

		/* too small packet? */
		if (len < VLAN_ETHER_HDR_SIZE)
			return;

		/* if no VLAN active */
		if ((ntohs(net_our_vlan) & VLAN_IDMASK) == VLAN_NONE
#if defined(CONFIG_CMD_CDP)
				&& iscdp == 0
#endif
				)
			return;

		cti = ntohs(vet->vet_tag);
		vlanid = cti & VLAN_IDMASK;
		eth_proto = ntohs(vet->vet_type);

		ip = (struct ip_udp_hdr *)(in_packet + VLAN_ETHER_HDR_SIZE);
		len -= VLAN_ETHER_HDR_SIZE;
	}

	debug_cond(DEBUG_NET_PKT, "Receive from protocol 0x%x\n", eth_proto);

#if defined(CONFIG_CMD_CDP)
	if (iscdp) {
		cdp_receive((uchar *)ip, len);
		return;
	}
#endif

	if ((myvlanid & VLAN_IDMASK) != VLAN_NONE) {
		if (vlanid == VLAN_NONE)
			vlanid = (mynvlanid & VLAN_IDMASK);
		/* not matched? */
		if (vlanid != (myvlanid & VLAN_IDMASK))
			return;
	}

	switch (eth_proto) {
	case PROT_ARP:
		arp_receive(et, ip, len);
		break;

#ifdef CONFIG_CMD_RARP
	case PROT_RARP:
		rarp_receive(ip, len);
		break;
#endif
	case PROT_IP:
		debug_cond(DEBUG_NET_PKT, "Got IP\n");
		/* Before we start poking the header, make sure it is there */
		if (len < IP_UDP_HDR_SIZE) {
			debug("len bad %d < %lu\n", len,
			      (ulong)IP_UDP_HDR_SIZE);
			return;
		}
		/* Check the packet length */
		if (len < ntohs(ip->ip_len)) {
			debug("len bad %d < %d\n", len, ntohs(ip->ip_len));
			return;
		}
		len = ntohs(ip->ip_len);
		debug_cond(DEBUG_NET_PKT, "len=%d, v=%02x\n",
			   len, ip->ip_hl_v & 0xff);

		/* Can't deal with anything except IPv4 */
		if ((ip->ip_hl_v & 0xf0) != 0x40)
			return;
		/* Can't deal with IP options (headers != 20 bytes) */
		if ((ip->ip_hl_v & 0x0f) > 0x05)
			return;
		/* Check the Checksum of the header */
		if (!ip_checksum_ok((uchar *)ip, IP_HDR_SIZE)) {
			debug("checksum bad\n");
			return;
		}
		/* If it is not for us, ignore it */
		dst_ip = net_read_ip(&ip->ip_dst);
		if (net_ip.s_addr && dst_ip.s_addr != net_ip.s_addr &&
		    dst_ip.s_addr != 0xFFFFFFFF) {
#ifdef CONFIG_MCAST_TFTP
			if (net_mcast_addr != dst_ip)
#endif
				return;
		}
		/* Read source IP address for later use */
		src_ip = net_read_ip(&ip->ip_src);
		/*
		 * The function returns the unchanged packet if it's not
		 * a fragment, and either the complete packet or NULL if
		 * it is a fragment (if !CONFIG_IP_DEFRAG, it returns NULL)
		 */
		ip = net_defragment(ip, &len);
		if (!ip)
			return;
		/*
		 * watch for ICMP host redirects
		 *
		 * There is no real handler code (yet). We just watch
		 * for ICMP host redirect messages. In case anybody
		 * sees these messages: please contact me
		 * (wd@denx.de), or - even better - send me the
		 * necessary fixes :-)
		 *
		 * Note: in all cases where I have seen this so far
		 * it was a problem with the router configuration,
		 * for instance when a router was configured in the
		 * BOOTP reply, but the TFTP server was on the same
		 * subnet. So this is probably a warning that your
		 * configuration might be wrong. But I'm not really
		 * sure if there aren't any other situations.
		 *
		 * Simon Glass <sjg@chromium.org>: We get an ICMP when
		 * we send a tftp packet to a dead connection, or when
		 * there is no server at the other end.
		 */
		if (ip->ip_p == IPPROTO_ICMP) {
			receive_icmp(ip, len, src_ip, et);
			return;
		} else if (ip->ip_p != IPPROTO_UDP) {	/* Only UDP packets */
			return;
		}

		debug_cond(DEBUG_DEV_PKT,
			   "received UDP (to=%pI4, from=%pI4, len=%d)\n",
			   &dst_ip, &src_ip, len);

#ifdef CONFIG_UDP_CHECKSUM
		if (ip->udp_xsum != 0) {
			ulong   xsum;
			ushort *sumptr;
			ushort  sumlen;

			xsum  = ip->ip_p;
			xsum += (ntohs(ip->udp_len));
			xsum += (ntohl(ip->ip_src.s_addr) >> 16) & 0x0000ffff;
			xsum += (ntohl(ip->ip_src.s_addr) >>  0) & 0x0000ffff;
			xsum += (ntohl(ip->ip_dst.s_addr) >> 16) & 0x0000ffff;
			xsum += (ntohl(ip->ip_dst.s_addr) >>  0) & 0x0000ffff;

			sumlen = ntohs(ip->udp_len);
			sumptr = (ushort *)&(ip->udp_src);

			while (sumlen > 1) {
				ushort sumdata;

				sumdata = *sumptr++;
				xsum += ntohs(sumdata);
				sumlen -= 2;
			}
			if (sumlen > 0) {
				ushort sumdata;

				sumdata = *(unsigned char *)sumptr;
				sumdata = (sumdata << 8) & 0xff00;
				xsum += sumdata;
			}
			while ((xsum >> 16) != 0) {
				xsum = (xsum & 0x0000ffff) +
				       ((xsum >> 16) & 0x0000ffff);
			}
			if ((xsum != 0x00000000) && (xsum != 0x0000ffff)) {
				printf(" UDP wrong checksum %08lx %08x\n",
				       xsum, ntohs(ip->udp_xsum));
				return;
			}
		}
#endif

#if defined(CONFIG_NETCONSOLE) && !defined(CONFIG_SPL_BUILD)
		nc_input_packet((uchar *)ip + IP_UDP_HDR_SIZE,
				src_ip,
				ntohs(ip->udp_dst),
				ntohs(ip->udp_src),
				ntohs(ip->udp_len) - UDP_HDR_SIZE);
#endif
		/*
		 * IP header OK.  Pass the packet to the current handler.
		 */
		(*udp_packet_handler)((uchar *)ip + IP_UDP_HDR_SIZE,
				      ntohs(ip->udp_dst),
				      src_ip,
				      ntohs(ip->udp_src),
				      ntohs(ip->udp_len) - UDP_HDR_SIZE);
		break;
	}
#endif
}

int eth_init()
{
	int rc;
	printf("[%s:%d]\n",__func__,__LINE__);

#ifdef CONFIG_VIRTIO_NET
	/* Use VirtIO Net for ARM virt machine */
	printf("Initializing VirtIO Net driver...\n");
	rc = virtio_net_initialize(CONFIG_VIRTIO_NET_BASE, CONFIG_VIRTIO_NET_IRQ);
#else
	printf("No Ethernet driver configured!\n");
	rc = -1;
#endif

	return rc;
}
