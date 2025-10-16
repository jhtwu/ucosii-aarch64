#include "includes.h"
#include "virtio_net.h"
#include "nat.h"
#include <net.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Network byte order helpers - inline macros */
#ifndef htons
#define htons(x) ((u16)((((x) & 0xff) << 8) | (((x) & 0xff00) >> 8)))
#endif
#ifndef ntohs
#define ntohs(x) htons(x)
#endif
#ifndef htonl
#define htonl(x) ((u32)((((x) & 0xff) << 24) | (((x) & 0xff00) << 8) | \
                  (((x) & 0xff0000) >> 8) | (((x) & 0xff000000) >> 24)))
#endif
#ifndef ntohl
#define ntohl(x) htonl(x)
#endif

/* Guest network configuration */
#define GUEST_LAN_IP   {192, 168, 1, 1}
#define GUEST_WAN_IP   {10, 3, 5, 99}
#define GUEST_LAN_MAC  {0x52, 0x54, 0x00, 0x12, 0x34, 0x56}
#define GUEST_WAN_MAC  {0x52, 0x54, 0x00, 0x65, 0x43, 0x21}

extern struct virtio_net_dev *virtio_net_device;

struct net_iface {
    u8 ip[4];
    u8 default_mac[6];
    u8 mac[6];
    struct eth_device *dev;
};

static struct net_iface net_ifaces[] = {
    { GUEST_LAN_IP, GUEST_LAN_MAC, {0}, NULL },
    { GUEST_WAN_IP, GUEST_WAN_MAC, {0}, NULL },
};

/* Network interface indices */
#define NET_IFACE_LAN  0
#define NET_IFACE_WAN  1

/* NAT enabled flag */
static bool nat_enabled = false;

__attribute__((weak)) void test_net_on_frame(u8 *pkt, int len)
{
    (void)pkt;
    (void)len;
}

/* Ethernet header */
struct eth_hdr {
    u8 dest_mac[6];
    u8 src_mac[6];
    u16 ethertype;
} __attribute__((packed));

/* TCP header */
struct tcp_hdr {
    u16 th_sport;     /* source port */
    u16 th_dport;     /* destination port */
    u32 th_seq;       /* sequence number */
    u32 th_ack;       /* acknowledgement number */
    u8  th_off;       /* data offset */
    u8  th_flags;     /* flags */
    u16 th_win;       /* window */
    u16 th_sum;       /* checksum */
    u16 th_urp;       /* urgent pointer */
} __attribute__((packed));

/* UDP header */
struct udp_hdr {
    u16 uh_sport;     /* source port */
    u16 uh_dport;     /* destination port */
    u16 uh_len;       /* length */
    u16 uh_sum;       /* checksum */
} __attribute__((packed));

/* Calculate IP checksum - RFC 1071 */
static u16 ip_checksum(void *data, int len)
{
    u32 sum = 0;
    u16 *p = (u16 *)data;

    while (len > 1) {
        sum += *p++;  /* Already in network byte order */
        len -= 2;
    }

    if (len == 1) {
        sum += (*(u8 *)p) << 8;  /* Pad last byte to 16 bits */
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return ~sum;  /* One's complement */
}

/* Calculate TCP/UDP checksum with pseudo-header - RFC 793/768 */
static u16 tcp_udp_checksum(struct ip_hdr *ip, void *transport_hdr, int transport_len)
{
    u32 sum = 0;
    u16 *p;
    int i;

    /* Pseudo-header: src IP (4 bytes) - already in network byte order */
    u16 *src_ip_words = (u16 *)&ip->ip_src.s_addr;
    sum += src_ip_words[0];
    sum += src_ip_words[1];

    /* Pseudo-header: dst IP (4 bytes) - already in network byte order */
    u16 *dst_ip_words = (u16 *)&ip->ip_dst.s_addr;
    sum += dst_ip_words[0];
    sum += dst_ip_words[1];

    /* Pseudo-header: zero byte + protocol (1 byte) */
    sum += htons(ip->ip_p);

    /* Pseudo-header: TCP/UDP length (2 bytes) */
    sum += htons(transport_len);

    /* TCP/UDP header and data */
    p = (u16 *)transport_hdr;
    for (i = 0; i < transport_len; i += 2) {
        if (i + 1 < transport_len) {
            sum += *p++;
        } else {
            /* Odd byte: pad with zero */
            sum += htons((*(u8 *)p) << 8);
        }
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return ~sum;
}

static struct net_iface *net_find_iface_by_ip(const u8 ip_bytes[4])
{
    for (size_t i = 0; i < sizeof(net_ifaces) / sizeof(net_ifaces[0]); ++i) {
        if (net_ifaces[i].dev &&
            net_ifaces[i].ip[0] == ip_bytes[0] &&
            net_ifaces[i].ip[1] == ip_bytes[1] &&
            net_ifaces[i].ip[2] == ip_bytes[2] &&
            net_ifaces[i].ip[3] == ip_bytes[3]) {
            return &net_ifaces[i];
        }
    }
    return NULL;
}

static int net_get_iface_index(const struct net_iface *iface)
{
    if (!iface) {
        return -1;
    }
    ptrdiff_t index = iface - net_ifaces;
    if (index < 0 || index >= (int)(sizeof(net_ifaces) / sizeof(net_ifaces[0]))) {
        return -1;
    }
    return (int)index;
}

/* Forward declarations */
static void net_send_arp_request(const u8 target_ip[4], struct net_iface *out_iface);

void net_register_iface(struct eth_device *dev)
{
    if (!dev) {
        return;
    }

    for (size_t i = 0; i < sizeof(net_ifaces) / sizeof(net_ifaces[0]); ++i) {
        if (net_ifaces[i].dev) {
            continue;
        }

        if (memcmp(dev->enetaddr, net_ifaces[i].default_mac, 6) == 0 ||
            memcmp(net_ifaces[i].mac, dev->enetaddr, 6) == 0) {
            memcpy(net_ifaces[i].mac, dev->enetaddr, 6);
            net_ifaces[i].dev = dev;
            return;
        }
    }

    /* Fallback: bind to first free slot */
    for (size_t i = 0; i < sizeof(net_ifaces) / sizeof(net_ifaces[0]); ++i) {
        if (!net_ifaces[i].dev) {
            memcpy(net_ifaces[i].mac, dev->enetaddr, 6);
            net_ifaces[i].dev = dev;
            return;
        }
    }
}

/**
 * net_enable_nat() - Enable NAT functionality
 */
void net_enable_nat(void)
{
    if (!nat_enabled) {
        nat_init();
        nat_configure(net_ifaces[NET_IFACE_LAN].ip,
                     net_ifaces[NET_IFACE_WAN].ip);
        nat_enabled = true;
        printf("[NET] NAT functionality enabled\n");
    }
}

/**
 * net_forward_icmp_with_nat() - Forward ICMP packet with NAT translation
 * @pkt: Packet buffer
 * @len: Packet length
 * @from_iface_idx: Source interface index
 *
 * Performs NAT translation and forwards ICMP packet to the appropriate interface.
 * Returns: 0 on success, -1 on error
 */
static int net_forward_icmp_with_nat(u8 *pkt, int len, int from_iface_idx)
{
    struct eth_hdr *eth = (struct eth_hdr *)pkt;
    struct ip_hdr *ip = (struct ip_hdr *)(pkt + sizeof(struct eth_hdr));
    struct icmp_hdr *icmp = (struct icmp_hdr *)(pkt + sizeof(struct eth_hdr) + IP_HDR_SIZE);
    int to_iface_idx;
    struct net_iface *out_iface;
    u8 src_ip_bytes[4], dst_ip_bytes[4];
    u16 original_id, translated_id;
    u32 src_ip_u32, dst_ip_u32;

    if (len < sizeof(struct eth_hdr) + IP_HDR_SIZE + 8) {
        return -1;
    }

    /* Extract IP addresses */
    src_ip_u32 = ntohl(ip->ip_src.s_addr);
    dst_ip_u32 = ntohl(ip->ip_dst.s_addr);
    src_ip_bytes[0] = (src_ip_u32 >> 24) & 0xff;
    src_ip_bytes[1] = (src_ip_u32 >> 16) & 0xff;
    src_ip_bytes[2] = (src_ip_u32 >> 8) & 0xff;
    src_ip_bytes[3] = src_ip_u32 & 0xff;
    dst_ip_bytes[0] = (dst_ip_u32 >> 24) & 0xff;
    dst_ip_bytes[1] = (dst_ip_u32 >> 16) & 0xff;
    dst_ip_bytes[2] = (dst_ip_u32 >> 8) & 0xff;
    dst_ip_bytes[3] = dst_ip_u32 & 0xff;

    original_id = ntohs(icmp->un.echo.id);

    /* Determine direction and perform NAT */
    if (from_iface_idx == NET_IFACE_LAN) {
        /* LAN -> WAN: Outbound NAT (SNAT) */
        to_iface_idx = NET_IFACE_WAN;

        /* Perform NAT translation */
        if (nat_translate_outbound(NAT_PROTO_ICMP, src_ip_bytes, original_id,
                                   dst_ip_bytes, 0, &translated_id) != 0) {
            printf("[NAT] Outbound translation failed\n");
            return -1;
        }

        /* Modify packet: change source IP to WAN IP */
        ip->ip_src.s_addr = htonl((net_ifaces[NET_IFACE_WAN].ip[0] << 24) |
                                  (net_ifaces[NET_IFACE_WAN].ip[1] << 16) |
                                  (net_ifaces[NET_IFACE_WAN].ip[2] << 8) |
                                   net_ifaces[NET_IFACE_WAN].ip[3]);

        /* Update ICMP ID */
        icmp->un.echo.id = htons(translated_id);

        printf("[NAT] ICMP LAN->WAN: %d.%d.%d.%d:%u->%d.%d.%d.%d (SNAT to %d.%d.%d.%d:%u)\n",
               src_ip_bytes[0], src_ip_bytes[1], src_ip_bytes[2], src_ip_bytes[3], original_id,
               dst_ip_bytes[0], dst_ip_bytes[1], dst_ip_bytes[2], dst_ip_bytes[3],
               net_ifaces[NET_IFACE_WAN].ip[0], net_ifaces[NET_IFACE_WAN].ip[1],
               net_ifaces[NET_IFACE_WAN].ip[2], net_ifaces[NET_IFACE_WAN].ip[3], translated_id);

    } else if (from_iface_idx == NET_IFACE_WAN) {
        /* WAN -> LAN: Inbound NAT (reverse SNAT) */
        u8 original_lan_ip[4];
        u16 original_lan_id;

        /* Perform reverse NAT lookup */
        if (nat_translate_inbound(NAT_PROTO_ICMP, original_id,
                                 src_ip_bytes, 0,
                                 original_lan_ip, &original_lan_id) != 0) {
            /* No matching NAT entry - packet not for us */
            return -1;
        }

        to_iface_idx = NET_IFACE_LAN;

        /* Modify packet: change destination IP to original LAN IP */
        ip->ip_dst.s_addr = htonl((original_lan_ip[0] << 24) |
                                  (original_lan_ip[1] << 16) |
                                  (original_lan_ip[2] << 8) |
                                   original_lan_ip[3]);

        /* Restore original ICMP ID */
        icmp->un.echo.id = htons(original_lan_id);

        printf("[NAT] ICMP WAN->LAN: %d.%d.%d.%d:%u->? (reverse NAT to %d.%d.%d.%d:%u)\n",
               src_ip_bytes[0], src_ip_bytes[1], src_ip_bytes[2], src_ip_bytes[3], original_id,
               original_lan_ip[0], original_lan_ip[1], original_lan_ip[2], original_lan_ip[3],
               original_lan_id);
    } else {
        return -1;
    }

    /* Recalculate IP checksum */
    ip->ip_sum = 0;
    ip->ip_sum = ip_checksum(ip, IP_HDR_SIZE);

    /* Recalculate ICMP checksum */
    int ip_len = ntohs(ip->ip_len);
    int icmp_len = ip_len - IP_HDR_SIZE;
    icmp->checksum = 0;
    icmp->checksum = ip_checksum(icmp, icmp_len);

    /* Update Ethernet header and forward */
    out_iface = &net_ifaces[to_iface_idx];
    if (!out_iface->dev) {
        return -1;
    }

    memcpy(eth->src_mac, out_iface->mac, 6);

    /* Perform ARP lookup for destination MAC */
    u8 dest_ip_for_arp[4];
    if (to_iface_idx == NET_IFACE_WAN) {
        /* Going to WAN - use actual destination IP */
        dest_ip_for_arp[0] = dst_ip_bytes[0];
        dest_ip_for_arp[1] = dst_ip_bytes[1];
        dest_ip_for_arp[2] = dst_ip_bytes[2];
        dest_ip_for_arp[3] = dst_ip_bytes[3];
    } else {
        /* Going to LAN - use translated destination */
        u32 lan_dst = ntohl(ip->ip_dst.s_addr);
        dest_ip_for_arp[0] = (lan_dst >> 24) & 0xff;
        dest_ip_for_arp[1] = (lan_dst >> 16) & 0xff;
        dest_ip_for_arp[2] = (lan_dst >> 8) & 0xff;
        dest_ip_for_arp[3] = lan_dst & 0xff;
    }

    if (arp_cache_lookup(dest_ip_for_arp, eth->dest_mac)) {
        /* MAC found in cache - send packet */
        virtio_net_send(out_iface->dev, pkt, len);
    } else {
        /* MAC not in cache - send ARP request and use broadcast for now */
        printf("[ARP] MAC not found for %d.%d.%d.%d, sending ARP request\n",
               dest_ip_for_arp[0], dest_ip_for_arp[1], dest_ip_for_arp[2], dest_ip_for_arp[3]);
        net_send_arp_request(dest_ip_for_arp, out_iface);

        /* Still send the packet with broadcast MAC - this works for some protocols */
        memset(eth->dest_mac, 0xff, 6);
        virtio_net_send(out_iface->dev, pkt, len);
    }

    return 0;
}

/**
 * net_forward_tcp_with_nat() - Forward TCP packet with NAT translation
 * @pkt: Packet buffer
 * @len: Packet length
 * @from_iface_idx: Source interface index
 *
 * Performs NAT translation and forwards TCP packet to the appropriate interface.
 * Returns: 0 on success, -1 on error
 */
static int net_forward_tcp_with_nat(u8 *pkt, int len, int from_iface_idx)
{
    struct eth_hdr *eth = (struct eth_hdr *)pkt;
    struct ip_hdr *ip = (struct ip_hdr *)(pkt + sizeof(struct eth_hdr));
    struct tcp_hdr *tcp = (struct tcp_hdr *)(pkt + sizeof(struct eth_hdr) + IP_HDR_SIZE);
    int to_iface_idx;
    struct net_iface *out_iface;
    u8 src_ip_bytes[4], dst_ip_bytes[4];
    u16 original_port, translated_port;
    u32 src_ip_u32, dst_ip_u32;

    if (len < sizeof(struct eth_hdr) + IP_HDR_SIZE + 20) {
        return -1;
    }

    /* Extract IP addresses */
    src_ip_u32 = ntohl(ip->ip_src.s_addr);
    dst_ip_u32 = ntohl(ip->ip_dst.s_addr);
    src_ip_bytes[0] = (src_ip_u32 >> 24) & 0xff;
    src_ip_bytes[1] = (src_ip_u32 >> 16) & 0xff;
    src_ip_bytes[2] = (src_ip_u32 >> 8) & 0xff;
    src_ip_bytes[3] = src_ip_u32 & 0xff;
    dst_ip_bytes[0] = (dst_ip_u32 >> 24) & 0xff;
    dst_ip_bytes[1] = (dst_ip_u32 >> 16) & 0xff;
    dst_ip_bytes[2] = (dst_ip_u32 >> 8) & 0xff;
    dst_ip_bytes[3] = dst_ip_u32 & 0xff;

    /* Determine direction and perform NAT */
    if (from_iface_idx == NET_IFACE_LAN) {
        /* LAN -> WAN: Outbound NAT (SNAT) */
        to_iface_idx = NET_IFACE_WAN;
        original_port = ntohs(tcp->th_sport);
        u16 dst_port = ntohs(tcp->th_dport);

        /* Perform NAT translation */
        if (nat_translate_outbound(NAT_PROTO_TCP, src_ip_bytes, original_port,
                                   dst_ip_bytes, dst_port, &translated_port) != 0) {
            printf("[NAT] TCP outbound translation failed\n");
            return -1;
        }

        /* Modify packet: change source IP to WAN IP */
        ip->ip_src.s_addr = htonl((net_ifaces[NET_IFACE_WAN].ip[0] << 24) |
                                  (net_ifaces[NET_IFACE_WAN].ip[1] << 16) |
                                  (net_ifaces[NET_IFACE_WAN].ip[2] << 8) |
                                   net_ifaces[NET_IFACE_WAN].ip[3]);

        /* Update TCP source port */
        tcp->th_sport = htons(translated_port);

        /* Rate-limit logging to reduce overhead */
        static u32 tcp_out_count = 0;
        if ((++tcp_out_count % 1000) == 1 || tcp_out_count < 10) {
            printf("[NAT] TCP LAN->WAN: %d.%d.%d.%d:%u->%d.%d.%d.%d:%u (SNAT to %d.%d.%d.%d:%u) [%u pkts]\n",
                   src_ip_bytes[0], src_ip_bytes[1], src_ip_bytes[2], src_ip_bytes[3], original_port,
                   dst_ip_bytes[0], dst_ip_bytes[1], dst_ip_bytes[2], dst_ip_bytes[3], dst_port,
                   net_ifaces[NET_IFACE_WAN].ip[0], net_ifaces[NET_IFACE_WAN].ip[1],
                   net_ifaces[NET_IFACE_WAN].ip[2], net_ifaces[NET_IFACE_WAN].ip[3], translated_port,
                   tcp_out_count);
        }

    } else if (from_iface_idx == NET_IFACE_WAN) {
        /* WAN -> LAN: Inbound NAT (reverse SNAT) */
        u8 original_lan_ip[4];
        u16 original_lan_port;
        u16 dst_port = ntohs(tcp->th_dport);
        u16 src_port = ntohs(tcp->th_sport);

        /* Rate-limited logging */
        static u32 tcp_in_count = 0;
        if ((++tcp_in_count % 1000) == 1 || tcp_in_count < 10) {
            printf("[NAT] TCP WAN->LAN: Received from %d.%d.%d.%d:%u to port %u [%u pkts]\n",
                   src_ip_bytes[0], src_ip_bytes[1], src_ip_bytes[2], src_ip_bytes[3], src_port, dst_port,
                   tcp_in_count);
        }

        /* Perform reverse NAT lookup */
        if (nat_translate_inbound(NAT_PROTO_TCP, dst_port,
                                 src_ip_bytes, src_port,
                                 original_lan_ip, &original_lan_port) != 0) {
            /* No matching NAT entry - packet not for us */
            printf("[NAT] TCP WAN->LAN: No NAT mapping found for port %u\n", dst_port);
            return -1;
        }

        to_iface_idx = NET_IFACE_LAN;

        /* Modify packet: change destination IP to original LAN IP */
        ip->ip_dst.s_addr = htonl((original_lan_ip[0] << 24) |
                                  (original_lan_ip[1] << 16) |
                                  (original_lan_ip[2] << 8) |
                                   original_lan_ip[3]);

        /* Restore original TCP destination port */
        tcp->th_dport = htons(original_lan_port);

        /* Reduced logging */
    } else {
        return -1;
    }

    /* Recalculate IP checksum */
    ip->ip_sum = 0;
    ip->ip_sum = ip_checksum(ip, IP_HDR_SIZE);

    /* Recalculate TCP checksum (includes pseudo-header) - RFC 793 */
    int ip_total_len = ntohs(ip->ip_len);
    int tcp_len = ip_total_len - IP_HDR_SIZE;
    tcp->th_sum = 0;
    tcp->th_sum = tcp_udp_checksum(ip, tcp, tcp_len);

    /* Update Ethernet header and forward */
    out_iface = &net_ifaces[to_iface_idx];
    if (!out_iface->dev) {
        return -1;
    }

    memcpy(eth->src_mac, out_iface->mac, 6);

    /* Perform ARP lookup for destination MAC */
    u8 dest_ip_for_arp[4];
    if (to_iface_idx == NET_IFACE_WAN) {
        /* Going to WAN - use actual destination IP */
        dest_ip_for_arp[0] = dst_ip_bytes[0];
        dest_ip_for_arp[1] = dst_ip_bytes[1];
        dest_ip_for_arp[2] = dst_ip_bytes[2];
        dest_ip_for_arp[3] = dst_ip_bytes[3];
    } else {
        /* Going to LAN - use translated destination */
        u32 lan_dst = ntohl(ip->ip_dst.s_addr);
        dest_ip_for_arp[0] = (lan_dst >> 24) & 0xff;
        dest_ip_for_arp[1] = (lan_dst >> 16) & 0xff;
        dest_ip_for_arp[2] = (lan_dst >> 8) & 0xff;
        dest_ip_for_arp[3] = lan_dst & 0xff;
    }

    if (arp_cache_lookup(dest_ip_for_arp, eth->dest_mac)) {
        /* MAC found in cache - send packet */
        virtio_net_send(out_iface->dev, pkt, len);
    } else {
        /* MAC not in cache - send ARP request and DO NOT send the packet */
        /* TCP requires proper MAC addressing, broadcast won't work */
        printf("[ARP] MAC not found for %d.%d.%d.%d, sending ARP request (packet dropped)\n",
               dest_ip_for_arp[0], dest_ip_for_arp[1], dest_ip_for_arp[2], dest_ip_for_arp[3]);
        net_send_arp_request(dest_ip_for_arp, out_iface);
        return -1;
    }

    return 0;
}

/**
 * net_forward_udp_with_nat() - Forward UDP packet with NAT translation
 * @pkt: Packet buffer
 * @len: Packet length
 * @from_iface_idx: Source interface index
 *
 * Performs NAT translation and forwards UDP packet to the appropriate interface.
 * Returns: 0 on success, -1 on error
 */
static int net_forward_udp_with_nat(u8 *pkt, int len, int from_iface_idx)
{
    struct eth_hdr *eth = (struct eth_hdr *)pkt;
    struct ip_hdr *ip = (struct ip_hdr *)(pkt + sizeof(struct eth_hdr));
    struct udp_hdr *udp = (struct udp_hdr *)(pkt + sizeof(struct eth_hdr) + IP_HDR_SIZE);
    int to_iface_idx;
    struct net_iface *out_iface;
    u8 src_ip_bytes[4], dst_ip_bytes[4];
    u16 original_port, translated_port;
    u32 src_ip_u32, dst_ip_u32;

    if (len < sizeof(struct eth_hdr) + IP_HDR_SIZE + 8) {
        return -1;
    }

    /* Extract IP addresses */
    src_ip_u32 = ntohl(ip->ip_src.s_addr);
    dst_ip_u32 = ntohl(ip->ip_dst.s_addr);
    src_ip_bytes[0] = (src_ip_u32 >> 24) & 0xff;
    src_ip_bytes[1] = (src_ip_u32 >> 16) & 0xff;
    src_ip_bytes[2] = (src_ip_u32 >> 8) & 0xff;
    src_ip_bytes[3] = src_ip_u32 & 0xff;
    dst_ip_bytes[0] = (dst_ip_u32 >> 24) & 0xff;
    dst_ip_bytes[1] = (dst_ip_u32 >> 16) & 0xff;
    dst_ip_bytes[2] = (dst_ip_u32 >> 8) & 0xff;
    dst_ip_bytes[3] = dst_ip_u32 & 0xff;

    /* Determine direction and perform NAT */
    if (from_iface_idx == NET_IFACE_LAN) {
        /* LAN -> WAN: Outbound NAT (SNAT) */
        to_iface_idx = NET_IFACE_WAN;
        original_port = ntohs(udp->uh_sport);
        u16 dst_port = ntohs(udp->uh_dport);

        /* Perform NAT translation */
        if (nat_translate_outbound(NAT_PROTO_UDP, src_ip_bytes, original_port,
                                   dst_ip_bytes, dst_port, &translated_port) != 0) {
            printf("[NAT] UDP outbound translation failed\n");
            return -1;
        }

        /* Modify packet: change source IP to WAN IP */
        ip->ip_src.s_addr = htonl((net_ifaces[NET_IFACE_WAN].ip[0] << 24) |
                                  (net_ifaces[NET_IFACE_WAN].ip[1] << 16) |
                                  (net_ifaces[NET_IFACE_WAN].ip[2] << 8) |
                                   net_ifaces[NET_IFACE_WAN].ip[3]);

        /* Update UDP source port */
        udp->uh_sport = htons(translated_port);

        printf("[NAT] UDP LAN->WAN: %d.%d.%d.%d:%u->%d.%d.%d.%d:%u (SNAT to %d.%d.%d.%d:%u)\n",
               src_ip_bytes[0], src_ip_bytes[1], src_ip_bytes[2], src_ip_bytes[3], original_port,
               dst_ip_bytes[0], dst_ip_bytes[1], dst_ip_bytes[2], dst_ip_bytes[3], dst_port,
               net_ifaces[NET_IFACE_WAN].ip[0], net_ifaces[NET_IFACE_WAN].ip[1],
               net_ifaces[NET_IFACE_WAN].ip[2], net_ifaces[NET_IFACE_WAN].ip[3], translated_port);

    } else if (from_iface_idx == NET_IFACE_WAN) {
        /* WAN -> LAN: Inbound NAT (reverse SNAT) */
        u8 original_lan_ip[4];
        u16 original_lan_port;
        u16 dst_port = ntohs(udp->uh_dport);
        u16 src_port = ntohs(udp->uh_sport);

        /* Perform reverse NAT lookup */
        if (nat_translate_inbound(NAT_PROTO_UDP, dst_port,
                                 src_ip_bytes, src_port,
                                 original_lan_ip, &original_lan_port) != 0) {
            /* No matching NAT entry - packet not for us */
            return -1;
        }

        to_iface_idx = NET_IFACE_LAN;

        /* Modify packet: change destination IP to original LAN IP */
        ip->ip_dst.s_addr = htonl((original_lan_ip[0] << 24) |
                                  (original_lan_ip[1] << 16) |
                                  (original_lan_ip[2] << 8) |
                                   original_lan_ip[3]);

        /* Restore original UDP destination port */
        udp->uh_dport = htons(original_lan_port);

        printf("[NAT] UDP forward WAN->LAN: Port:%u -> %d.%d.%d.%d:%u\n",
               dst_port,
               original_lan_ip[0], original_lan_ip[1], original_lan_ip[2], original_lan_ip[3],
               original_lan_port);
    } else {
        return -1;
    }

    /* Recalculate IP checksum */
    ip->ip_sum = 0;
    ip->ip_sum = ip_checksum(ip, IP_HDR_SIZE);

    /* Recalculate UDP checksum (includes pseudo-header) - RFC 768 */
    int udp_len = ntohs(udp->uh_len);
    udp->uh_sum = 0;
    udp->uh_sum = tcp_udp_checksum(ip, udp, udp_len);

    /* Update Ethernet header and forward */
    out_iface = &net_ifaces[to_iface_idx];
    if (!out_iface->dev) {
        return -1;
    }

    memcpy(eth->src_mac, out_iface->mac, 6);

    /* Perform ARP lookup for destination MAC */
    u8 dest_ip_for_arp[4];
    if (to_iface_idx == NET_IFACE_WAN) {
        /* Going to WAN - use actual destination IP */
        dest_ip_for_arp[0] = dst_ip_bytes[0];
        dest_ip_for_arp[1] = dst_ip_bytes[1];
        dest_ip_for_arp[2] = dst_ip_bytes[2];
        dest_ip_for_arp[3] = dst_ip_bytes[3];
    } else {
        /* Going to LAN - use translated destination */
        u32 lan_dst = ntohl(ip->ip_dst.s_addr);
        dest_ip_for_arp[0] = (lan_dst >> 24) & 0xff;
        dest_ip_for_arp[1] = (lan_dst >> 16) & 0xff;
        dest_ip_for_arp[2] = (lan_dst >> 8) & 0xff;
        dest_ip_for_arp[3] = lan_dst & 0xff;
    }

    if (arp_cache_lookup(dest_ip_for_arp, eth->dest_mac)) {
        /* MAC found in cache - send packet */
        virtio_net_send(out_iface->dev, pkt, len);
    } else {
        /* MAC not in cache - send ARP request and use broadcast for now */
        printf("[ARP] MAC not found for %d.%d.%d.%d, sending ARP request\n",
               dest_ip_for_arp[0], dest_ip_for_arp[1], dest_ip_for_arp[2], dest_ip_for_arp[3]);
        net_send_arp_request(dest_ip_for_arp, out_iface);

        /* Still send the packet with broadcast MAC - UDP can work with broadcast */
        memset(eth->dest_mac, 0xff, 6);
        virtio_net_send(out_iface->dev, pkt, len);
    }

    return 0;
}

/**
 * net_send_arp_request() - Send an ARP request for a given IP address
 * @target_ip: IP address to resolve (4 bytes)
 * @out_iface: Network interface to send from
 *
 * Sends an ARP request to discover the MAC address of the target IP.
 */
static void net_send_arp_request(const u8 target_ip[4], struct net_iface *out_iface)
{
    u8 arp_pkt[64];
    struct eth_hdr *eth;
    struct arp_hdr *arp;

    if (!out_iface || !out_iface->dev) {
        return;
    }

    memset(arp_pkt, 0, sizeof(arp_pkt));
    eth = (struct eth_hdr *)arp_pkt;
    arp = (struct arp_hdr *)(arp_pkt + sizeof(struct eth_hdr));

    /* Ethernet header - broadcast */
    memset(eth->dest_mac, 0xff, 6);
    memcpy(eth->src_mac, out_iface->mac, 6);
    eth->ethertype = htons(0x0806);  /* ARP */

    /* ARP request */
    arp->ar_hrd = htons(1);          /* Ethernet */
    arp->ar_pro = htons(0x0800);     /* IPv4 */
    arp->ar_hln = 6;                 /* Hardware address length */
    arp->ar_pln = 4;                 /* Protocol address length */
    arp->ar_op = htons(ARPOP_REQUEST);

    /* Sender: our interface */
    memcpy(&arp->ar_data[0], out_iface->mac, 6);        /* Sender MAC */
    memcpy(&arp->ar_data[6], out_iface->ip, 4);         /* Sender IP */

    /* Target: who we're looking for */
    memset(&arp->ar_data[10], 0x00, 6);                  /* Target MAC (unknown) */
    memcpy(&arp->ar_data[16], target_ip, 4);             /* Target IP */

    printf("[ARP] Sending request: Who has %d.%d.%d.%d? Tell %d.%d.%d.%d\n",
           target_ip[0], target_ip[1], target_ip[2], target_ip[3],
           out_iface->ip[0], out_iface->ip[1], out_iface->ip[2], out_iface->ip[3]);

    /* Send ARP request */
    virtio_net_send(out_iface->dev, arp_pkt, 42);
}

/* Handle ARP request */
static void handle_arp(u8 *pkt, int len)
{
    struct eth_hdr *eth = (struct eth_hdr *)pkt;
    struct arp_hdr *arp = (struct arp_hdr *)(pkt + sizeof(struct eth_hdr));
    u8 reply[64];
    struct eth_hdr *reply_eth;
    struct arp_hdr *reply_arp;
    u8 *sender_mac, *sender_ip, *target_ip, *target_mac;
    struct net_iface *iface;
    u16 arp_op;

    if (len < sizeof(struct eth_hdr) + 28) {
        return;
    }

    /* Get pointers to ARP fields */
    sender_mac = &arp->ar_data[0];
    sender_ip = &arp->ar_data[6];
    target_mac = &arp->ar_data[10];
    target_ip = &arp->ar_data[16];

    arp_op = ntohs(arp->ar_op);

    /* Learn sender's MAC address from both ARP requests and replies */
    if (arp_op == ARPOP_REQUEST || arp_op == ARPOP_REPLY) {
        arp_cache_add(sender_ip, sender_mac);
    }

    /* Handle ARP request */
    if (arp_op == ARPOP_REQUEST) {
        iface = net_find_iface_by_ip(target_ip);
        if (!iface || !iface->dev) {
            return;
        }
    } else if (arp_op == ARPOP_REPLY) {
        /* ARP reply - already learned above, nothing more to do */
        printf("[ARP] Received reply: %d.%d.%d.%d is at %02x:%02x:%02x:%02x:%02x:%02x\n",
               sender_ip[0], sender_ip[1], sender_ip[2], sender_ip[3],
               sender_mac[0], sender_mac[1], sender_mac[2], sender_mac[3], sender_mac[4], sender_mac[5]);
        return;
    } else {
        /* Unknown ARP operation */
        return;
    }

    /* Build ARP reply */
    memset(reply, 0, sizeof(reply));
    reply_eth = (struct eth_hdr *)reply;
    reply_arp = (struct arp_hdr *)(reply + sizeof(struct eth_hdr));

    /* Ethernet header */
    memcpy(reply_eth->dest_mac, sender_mac, 6);
    memcpy(reply_eth->src_mac, iface->mac, 6);
    reply_eth->ethertype = htons(0x0806);  /* ARP */

    /* ARP reply */
    reply_arp->ar_hrd = htons(1);          /* Ethernet */
    reply_arp->ar_pro = htons(0x0800);     /* IPv4 */
    reply_arp->ar_hln = 6;
    reply_arp->ar_pln = 4;
    reply_arp->ar_op = htons(ARPOP_REPLY);
    memcpy(&reply_arp->ar_data[0], iface->mac, 6);
    memcpy(&reply_arp->ar_data[6], target_ip, 4);
    memcpy(&reply_arp->ar_data[10], sender_mac, 6);
    memcpy(&reply_arp->ar_data[16], sender_ip, 4);

    /* Send ARP reply */
    if (iface->dev) {
        virtio_net_send(iface->dev, reply, 42);
    }
}

/* Handle ICMP echo request (ping) */
static void handle_icmp(u8 *pkt, int len, int rx_iface_idx)
{
    struct eth_hdr *eth = (struct eth_hdr *)pkt;
    struct ip_hdr *ip = (struct ip_hdr *)(pkt + sizeof(struct eth_hdr));
    struct icmp_hdr *icmp = (struct icmp_hdr *)(pkt + sizeof(struct eth_hdr) + IP_HDR_SIZE);
    u8 reply[1500];
    struct eth_hdr *reply_eth;
    struct ip_hdr *reply_ip;
    struct icmp_hdr *reply_icmp;
    int ip_len = ntohs(ip->ip_len);
    int icmp_len = ip_len - IP_HDR_SIZE;
    u8 *payload;
    int payload_len;
    u32 dest_ip, src_ip;
    struct net_iface *iface;
    u8 src_ip_bytes[4];

    if (len < sizeof(struct eth_hdr) + IP_HDR_SIZE + 8) {
        return;
    }

    /* Check if ICMP echo request or reply */
    if (icmp->type != ICMP_ECHO_REQUEST && icmp->type != ICMP_ECHO_REPLY) {
        return;
    }

    /* Extract source and destination IPs */
    src_ip = ntohl(ip->ip_src.s_addr);
    src_ip_bytes[0] = (src_ip >> 24) & 0xff;
    src_ip_bytes[1] = (src_ip >> 16) & 0xff;
    src_ip_bytes[2] = (src_ip >> 8) & 0xff;
    src_ip_bytes[3] = src_ip & 0xff;

    dest_ip = ntohl(ip->ip_dst.s_addr);
    u8 dest_ip_bytes[4] = {
        (u8)((dest_ip >> 24) & 0xff),
        (u8)((dest_ip >> 16) & 0xff),
        (u8)((dest_ip >> 8) & 0xff),
        (u8)(dest_ip & 0xff)
    };

    iface = net_find_iface_by_ip(dest_ip_bytes);

    /* NAT forwarding logic */
    if (nat_enabled) {
        /* Check if packet is from LAN and destined to external network */
        if (rx_iface_idx == NET_IFACE_LAN && !iface && icmp->type == ICMP_ECHO_REQUEST) {
            /* Forward with NAT: LAN -> WAN */
            net_forward_icmp_with_nat(pkt, len, NET_IFACE_LAN);
            return;
        }

        /* Check if packet is reply from WAN */
        if (rx_iface_idx == NET_IFACE_WAN && icmp->type == ICMP_ECHO_REPLY) {
            /* Try NAT reverse translation: WAN -> LAN */
            if (net_forward_icmp_with_nat(pkt, len, NET_IFACE_WAN) == 0) {
                return;
            }
            /* If NAT translation failed, fall through to normal processing */
        }
    }

    /* Normal ICMP processing: packet is for our IP */
    if (!iface || !iface->dev) {
        return;
    }

    /* Only handle ICMP echo requests for direct replies */
    if (icmp->type != ICMP_ECHO_REQUEST) {
        return;
    }

    payload = (u8 *)icmp + 8;
    payload_len = icmp_len - 8;

    /* Build ICMP reply */
    memset(reply, 0, sizeof(reply));
    reply_eth = (struct eth_hdr *)reply;
    reply_ip = (struct ip_hdr *)(reply + sizeof(struct eth_hdr));
    reply_icmp = (struct icmp_hdr *)(reply + sizeof(struct eth_hdr) + IP_HDR_SIZE);

    /* Ethernet header */
    memcpy(reply_eth->dest_mac, eth->src_mac, 6);
    memcpy(reply_eth->src_mac, iface->mac, 6);
    reply_eth->ethertype = htons(0x0800);  /* IPv4 */

    /* IP header */
    reply_ip->ip_hl_v = 0x45;              /* IPv4, IHL=5 */
    reply_ip->ip_tos = 0;
    reply_ip->ip_len = htons(IP_HDR_SIZE + 8 + payload_len);
    reply_ip->ip_id = ip->ip_id;
    reply_ip->ip_off = 0;
    reply_ip->ip_ttl = 64;
    reply_ip->ip_p = 1;                    /* ICMP */
    reply_ip->ip_src.s_addr = ip->ip_dst.s_addr;
    reply_ip->ip_dst.s_addr = ip->ip_src.s_addr;
    reply_ip->ip_sum = 0;
    reply_ip->ip_sum = ip_checksum(reply_ip, IP_HDR_SIZE);

    /* ICMP header */
    reply_icmp->type = ICMP_ECHO_REPLY;
    reply_icmp->code = 0;
    reply_icmp->un.echo.id = icmp->un.echo.id;
    reply_icmp->un.echo.sequence = icmp->un.echo.sequence;

    /* Copy payload */
    memcpy((u8 *)reply_icmp + 8, payload, payload_len);

    /* Calculate ICMP checksum */
    reply_icmp->checksum = 0;
    reply_icmp->checksum = ip_checksum(reply_icmp, 8 + payload_len);

    /* Send ICMP reply */
    if (iface->dev) {
        int total_len = sizeof(struct eth_hdr) + IP_HDR_SIZE + 8 + payload_len;
        virtio_net_send(iface->dev, reply, total_len);
    }
}

/* Process received packet */
void net_process_received_packet(uchar *pkt, int len)
{
    struct eth_hdr *eth = (struct eth_hdr *)pkt;
    u16 ethertype;
    int rx_iface_idx = -1;

    test_net_on_frame((u8 *)pkt, len);

    if (len < sizeof(struct eth_hdr)) {
        return;
    }

    /* Determine which interface received this packet */
    for (int i = 0; i < (int)(sizeof(net_ifaces) / sizeof(net_ifaces[0])); i++) {
        if (net_ifaces[i].dev &&
            memcmp(eth->dest_mac, net_ifaces[i].mac, 6) == 0) {
            rx_iface_idx = i;
            break;
        }
        /* Also check for broadcast */
        if (net_ifaces[i].dev &&
            (eth->dest_mac[0] & 0x01)) {  /* Multicast/broadcast bit */
            rx_iface_idx = i;
            /* Don't break - continue to find exact match */
        }
    }

    ethertype = ntohs(eth->ethertype);

    switch (ethertype) {
        case 0x0806:  /* ARP */
            handle_arp(pkt, len);
            break;

        case 0x0800:  /* IPv4 */
            {
                struct ip_hdr *ip = (struct ip_hdr *)(pkt + sizeof(struct eth_hdr));
                if (len >= sizeof(struct eth_hdr) + IP_HDR_SIZE) {
                    if (ip->ip_p == 1) {  /* ICMP */
                        handle_icmp(pkt, len, rx_iface_idx);
                    } else if (ip->ip_p == 6 && nat_enabled) {  /* TCP */
                        /* Check if packet needs NAT forwarding */
                        u32 dest_ip = ntohl(ip->ip_dst.s_addr);
                        u8 dest_ip_bytes[4] = {
                            (u8)((dest_ip >> 24) & 0xff),
                            (u8)((dest_ip >> 16) & 0xff),
                            (u8)((dest_ip >> 8) & 0xff),
                            (u8)(dest_ip & 0xff)
                        };
                        struct net_iface *dest_iface = net_find_iface_by_ip(dest_ip_bytes);

                        if (rx_iface_idx == NET_IFACE_LAN && !dest_iface) {
                            /* LAN -> WAN: Forward with NAT */
                            net_forward_tcp_with_nat(pkt, len, NET_IFACE_LAN);
                        } else if (rx_iface_idx == NET_IFACE_WAN) {
                            /* WAN -> LAN: Try NAT reverse translation */
                            net_forward_tcp_with_nat(pkt, len, NET_IFACE_WAN);
                        }
                    } else if (ip->ip_p == 17 && nat_enabled) {  /* UDP */
                        /* Check if packet needs NAT forwarding */
                        u32 dest_ip = ntohl(ip->ip_dst.s_addr);
                        u8 dest_ip_bytes[4] = {
                            (u8)((dest_ip >> 24) & 0xff),
                            (u8)((dest_ip >> 16) & 0xff),
                            (u8)((dest_ip >> 8) & 0xff),
                            (u8)(dest_ip & 0xff)
                        };
                        struct net_iface *dest_iface = net_find_iface_by_ip(dest_ip_bytes);

                        if (rx_iface_idx == NET_IFACE_LAN && !dest_iface) {
                            /* LAN -> WAN: Forward with NAT */
                            net_forward_udp_with_nat(pkt, len, NET_IFACE_LAN);
                        } else if (rx_iface_idx == NET_IFACE_WAN) {
                            /* WAN -> LAN: Try NAT reverse translation */
                            net_forward_udp_with_nat(pkt, len, NET_IFACE_WAN);
                        }
                    }
                }
            }
            break;

        case 0x86dd:  /* IPv6 */
            /* Ignore IPv6 */
            break;

        default:
            break;
    }
}
