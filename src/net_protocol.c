#include "includes.h"
#include "virtio_net.h"
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

/* Handle ARP request */
static void handle_arp(u8 *pkt, int len)
{
    struct eth_hdr *eth = (struct eth_hdr *)pkt;
    struct arp_hdr *arp = (struct arp_hdr *)(pkt + sizeof(struct eth_hdr));
    u8 reply[64];
    struct eth_hdr *reply_eth;
    struct arp_hdr *reply_arp;
    u8 *sender_mac, *sender_ip, *target_ip;
    struct net_iface *iface;

    if (len < sizeof(struct eth_hdr) + 28) {
        return;
    }

    /* Get pointers to ARP fields */
    sender_mac = &arp->ar_data[0];
    sender_ip = &arp->ar_data[6];
    target_ip = &arp->ar_data[16];

    /* Check if ARP request is for us */
    if (ntohs(arp->ar_op) != ARPOP_REQUEST) {
        return;
    }

    iface = net_find_iface_by_ip(target_ip);
    if (!iface || !iface->dev) {
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
static void handle_icmp(u8 *pkt, int len)
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
    u32 dest_ip;
    struct net_iface *iface;

    if (len < sizeof(struct eth_hdr) + IP_HDR_SIZE + 8) {
        return;
    }

    /* Check if ICMP echo request */
    if (icmp->type != ICMP_ECHO_REQUEST) {
        return;
    }

    /* Check if for our IP */
    dest_ip = ntohl(ip->ip_dst.s_addr);
    {
        u8 ip_bytes[4] = {
            (u8)((dest_ip >> 24) & 0xff),
            (u8)((dest_ip >> 16) & 0xff),
            (u8)((dest_ip >> 8) & 0xff),
            (u8)(dest_ip & 0xff)
        };
        iface = net_find_iface_by_ip(ip_bytes);
    }

    if (!iface || !iface->dev) {
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

    test_net_on_frame((u8 *)pkt, len);

    if (len < sizeof(struct eth_hdr)) {
        return;
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
                        handle_icmp(pkt, len);
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
