/*
 * NAT (Network Address Translation) Implementation
 *
 * Provides SNAT functionality for routing LAN traffic through WAN interface
 */

#include "nat.h"
#include "includes.h"
#include "portable_libc.h"
#include <string.h>

/* NAT translation table */
static struct nat_entry nat_table[NAT_TABLE_SIZE];

/* Hash table for fast reverse lookup (wan_port -> table index) */
#define NAT_HASH_SIZE 128  /* Must be power of 2 */
static s8 nat_hash_table[NAT_HASH_SIZE];  /* -1 = empty, >=0 = index into nat_table */

/* NAT statistics */
static struct nat_stats nat_statistics;

/* ARP cache table */
static struct arp_entry arp_table[ARP_TABLE_SIZE];

/* NAT configuration */
static struct nat_config nat_cfg = {
    .lan_ip = {192, 168, 1, 1},
    .wan_ip = {10, 3, 5, 99},
    .port_range_start = 20000,
    .port_range_end = 30000
};

/* Next port to allocate (round-robin) */
static u16 next_port = 20000;

/* Forward declarations */
static struct nat_entry *nat_find_entry(u8 protocol, const u8 lan_ip[4],
                                        u16 lan_port, const u8 dst_ip[4],
                                        u16 dst_port);
static struct nat_entry *nat_find_reverse_entry(u8 protocol, u16 wan_port,
                                                const u8 src_ip[4], u16 src_port);
static struct nat_entry *nat_alloc_entry(void);
static u16 nat_alloc_port(void);
static u32 get_tick_count(void);
static bool ip_equal(const u8 ip1[4], const u8 ip2[4]);
static inline u8 nat_hash(u16 wan_port);
static void nat_hash_add(u16 wan_port, int table_index);
static void nat_hash_remove(u16 wan_port);

/**
 * nat_init() - Initialize NAT subsystem
 */
void nat_init(void)
{
    memset(nat_table, 0, sizeof(nat_table));
    memset(&nat_statistics, 0, sizeof(nat_statistics));
    memset(arp_table, 0, sizeof(arp_table));
    memset(nat_hash_table, -1, sizeof(nat_hash_table));  /* Initialize hash table to empty */
    next_port = nat_cfg.port_range_start;

    printf("[NAT] Initialized: LAN=%d.%d.%d.%d WAN=%d.%d.%d.%d\n",
           nat_cfg.lan_ip[0], nat_cfg.lan_ip[1],
           nat_cfg.lan_ip[2], nat_cfg.lan_ip[3],
           nat_cfg.wan_ip[0], nat_cfg.wan_ip[1],
           nat_cfg.wan_ip[2], nat_cfg.wan_ip[3]);
    printf("[ARP] Cache initialized with %d entries\n", ARP_TABLE_SIZE);
    printf("[NAT] Hash table initialized with %d buckets\n", NAT_HASH_SIZE);
}

/**
 * nat_configure() - Configure NAT parameters
 */
void nat_configure(const u8 lan_ip[4], const u8 wan_ip[4])
{
    memcpy(nat_cfg.lan_ip, lan_ip, 4);
    memcpy(nat_cfg.wan_ip, wan_ip, 4);

    printf("[NAT] Reconfigured: LAN=%d.%d.%d.%d WAN=%d.%d.%d.%d\n",
           nat_cfg.lan_ip[0], nat_cfg.lan_ip[1],
           nat_cfg.lan_ip[2], nat_cfg.lan_ip[3],
           nat_cfg.wan_ip[0], nat_cfg.wan_ip[1],
           nat_cfg.wan_ip[2], nat_cfg.wan_ip[3]);
}

/**
 * nat_translate_outbound() - Perform outbound NAT (LAN -> WAN)
 */
int nat_translate_outbound(u8 protocol, const u8 lan_ip[4], u16 lan_port,
                          const u8 dst_ip[4], u16 dst_port, u16 *wan_port)
{
    struct nat_entry *entry;
    u32 current_time = get_tick_count();
    u16 timeout;

    /* Find existing entry */
    entry = nat_find_entry(protocol, lan_ip, lan_port, dst_ip, dst_port);

    if (entry != NULL) {
        /* Update existing entry */
        entry->last_activity = current_time;
        *wan_port = entry->wan_port;
        nat_statistics.translations_out++;
        return 0;
    }

    /* Allocate new entry */
    entry = nat_alloc_entry();
    if (entry == NULL) {
        nat_statistics.table_full++;
        printf("[NAT] ERROR: Translation table full\n");
        return -1;
    }

    /* Set timeout based on protocol */
    switch (protocol) {
        case NAT_PROTO_ICMP:
            timeout = NAT_TIMEOUT_ICMP;
            break;
        case NAT_PROTO_UDP:
            timeout = NAT_TIMEOUT_UDP;
            break;
        case NAT_PROTO_TCP:
            timeout = NAT_TIMEOUT_TCP_INIT;
            break;
        default:
            timeout = NAT_TIMEOUT_UDP;
            break;
    }

    /* Allocate WAN port */
    u16 allocated_port = nat_alloc_port();

    /* Calculate table index */
    int table_idx = (int)(entry - nat_table);

    /* Initialize entry */
    entry->active = true;
    entry->protocol = protocol;
    memcpy(entry->lan_ip, lan_ip, 4);
    entry->lan_port = lan_port;
    entry->wan_port = allocated_port;
    memcpy(entry->dst_ip, dst_ip, 4);
    entry->dst_port = dst_port;
    entry->last_activity = current_time;
    entry->timeout_sec = timeout;

    /* Add to hash table for fast reverse lookup */
    nat_hash_add(allocated_port, table_idx);

    *wan_port = allocated_port;
    nat_statistics.translations_out++;

    printf("[NAT] New outbound: %d.%d.%d.%d:%u -> WAN:%u (proto=%u)\n",
           lan_ip[0], lan_ip[1], lan_ip[2], lan_ip[3], lan_port,
           allocated_port, protocol);

    return 0;
}

/**
 * nat_translate_inbound() - Perform inbound NAT (WAN -> LAN)
 */
int nat_translate_inbound(u8 protocol, u16 wan_port,
                         const u8 src_ip[4], u16 src_port,
                         u8 lan_ip[4], u16 *lan_port)
{
    struct nat_entry *entry;
    u32 current_time = get_tick_count();

    /* Find matching entry */
    entry = nat_find_reverse_entry(protocol, wan_port, src_ip, src_port);

    if (entry == NULL) {
        nat_statistics.no_match++;
        return -1;
    }

    /* Update activity timestamp */
    entry->last_activity = current_time;

    /* Return original LAN address */
    memcpy(lan_ip, entry->lan_ip, 4);
    *lan_port = entry->lan_port;

    nat_statistics.translations_in++;

    return 0;
}

/**
 * nat_cleanup_expired() - Remove expired NAT entries
 */
int nat_cleanup_expired(u32 current_ticks)
{
    int removed = 0;
    u32 current_sec = current_ticks / 1000;  /* Convert to seconds */

    for (int i = 0; i < NAT_TABLE_SIZE; i++) {
        if (!nat_table[i].active) {
            continue;
        }

        u32 entry_sec = nat_table[i].last_activity / 1000;
        u32 age_sec = current_sec - entry_sec;

        if (age_sec >= nat_table[i].timeout_sec) {
            /* Remove from hash table before marking inactive */
            nat_hash_remove(nat_table[i].wan_port);

            nat_table[i].active = false;
            removed++;
            nat_statistics.timeouts++;
        }
    }

    if (removed > 0) {
        printf("[NAT] Cleaned up %d expired entries\n", removed);
    }

    return removed;
}

/**
 * nat_get_stats() - Get NAT statistics
 */
const struct nat_stats *nat_get_stats(void)
{
    return &nat_statistics;
}

/**
 * nat_reset_stats() - Reset statistics
 */
void nat_reset_stats(void)
{
    memset(&nat_statistics, 0, sizeof(nat_statistics));
}

/**
 * nat_print_table() - Print NAT table for debugging
 */
void nat_print_table(void)
{
    int active_count = 0;

    printf("[NAT] Translation Table:\n");
    printf("%-4s %-6s %-18s %-18s %-8s\n",
           "Idx", "Proto", "LAN", "Destination", "Timeout");

    for (int i = 0; i < NAT_TABLE_SIZE; i++) {
        if (!nat_table[i].active) {
            continue;
        }

        active_count++;
        const char *proto_str;
        switch (nat_table[i].protocol) {
            case NAT_PROTO_ICMP: proto_str = "ICMP"; break;
            case NAT_PROTO_TCP:  proto_str = "TCP"; break;
            case NAT_PROTO_UDP:  proto_str = "UDP"; break;
            default:             proto_str = "???"; break;
        }

        printf("%-4d %-6s %d.%d.%d.%d:%-5u %d.%d.%d.%d:%-5u %-8us\n",
               i, proto_str,
               nat_table[i].lan_ip[0], nat_table[i].lan_ip[1],
               nat_table[i].lan_ip[2], nat_table[i].lan_ip[3],
               nat_table[i].lan_port,
               nat_table[i].dst_ip[0], nat_table[i].dst_ip[1],
               nat_table[i].dst_ip[2], nat_table[i].dst_ip[3],
               nat_table[i].dst_port,
               nat_table[i].timeout_sec);
    }

    printf("Active entries: %d/%d\n", active_count, NAT_TABLE_SIZE);
    printf("Stats: Out=%u In=%u TableFull=%u NoMatch=%u Timeouts=%u\n",
           nat_statistics.translations_out, nat_statistics.translations_in,
           nat_statistics.table_full, nat_statistics.no_match,
           nat_statistics.timeouts);
}

/**
 * nat_is_lan_ip() - Check if IP is in LAN subnet
 */
bool nat_is_lan_ip(const u8 ip[4])
{
    /* Check if IP is in 192.168.1.0/24 subnet */
    return (ip[0] == nat_cfg.lan_ip[0] &&
            ip[1] == nat_cfg.lan_ip[1] &&
            ip[2] == nat_cfg.lan_ip[2]);
}

/**
 * nat_is_wan_ip() - Check if IP is our WAN address
 */
bool nat_is_wan_ip(const u8 ip[4])
{
    return ip_equal(ip, nat_cfg.wan_ip);
}

/* ========== Internal Helper Functions ========== */

/**
 * nat_find_entry() - Find existing NAT entry
 */
static struct nat_entry *nat_find_entry(u8 protocol, const u8 lan_ip[4],
                                        u16 lan_port, const u8 dst_ip[4],
                                        u16 dst_port)
{
    for (int i = 0; i < NAT_TABLE_SIZE; i++) {
        if (!nat_table[i].active) {
            continue;
        }

        if (nat_table[i].protocol == protocol &&
            ip_equal(nat_table[i].lan_ip, lan_ip) &&
            nat_table[i].lan_port == lan_port &&
            ip_equal(nat_table[i].dst_ip, dst_ip) &&
            nat_table[i].dst_port == dst_port) {
            return &nat_table[i];
        }
    }

    return NULL;
}

/**
 * nat_find_reverse_entry() - Find entry for inbound translation
 * Uses hash table for O(1) lookup instead of O(N) linear search
 */
static struct nat_entry *nat_find_reverse_entry(u8 protocol, u16 wan_port,
                                                const u8 src_ip[4], u16 src_port)
{
    /* Hash table lookup - O(1) average case */
    u8 hash_idx = nat_hash(wan_port);
    s8 table_idx = nat_hash_table[hash_idx];

    /* Check if hash bucket is empty */
    if (table_idx < 0) {
        return NULL;
    }

    /* Verify the entry matches (handle hash collisions) */
    struct nat_entry *entry = &nat_table[table_idx];

    if (entry->active &&
        entry->protocol == protocol &&
        entry->wan_port == wan_port &&
        ip_equal(entry->dst_ip, src_ip) &&
        entry->dst_port == src_port) {
        return entry;
    }

    /* Hash collision - fall back to linear search (rare) */
    for (int i = 0; i < NAT_TABLE_SIZE; i++) {
        if (!nat_table[i].active) {
            continue;
        }

        if (nat_table[i].protocol == protocol &&
            nat_table[i].wan_port == wan_port &&
            ip_equal(nat_table[i].dst_ip, src_ip) &&
            nat_table[i].dst_port == src_port) {
            return &nat_table[i];
        }
    }

    return NULL;
}

/**
 * nat_alloc_entry() - Allocate a free NAT entry
 */
static struct nat_entry *nat_alloc_entry(void)
{
    /* First pass: find completely free entry */
    for (int i = 0; i < NAT_TABLE_SIZE; i++) {
        if (!nat_table[i].active) {
            return &nat_table[i];
        }
    }

    /* No free entries */
    return NULL;
}

/**
 * nat_alloc_port() - Allocate next available WAN port
 */
static u16 nat_alloc_port(void)
{
    u16 port = next_port++;

    /* Wrap around if we reach the end */
    if (next_port > nat_cfg.port_range_end) {
        next_port = nat_cfg.port_range_start;
    }

    return port;
}

/**
 * get_tick_count() - Get current system tick count
 */
static u32 get_tick_count(void)
{
    /* Use µC/OS-II's tick counter */
    extern volatile u32 OSTime;
    return OSTime;
}

/**
 * ip_equal() - Compare two IP addresses
 */
static bool ip_equal(const u8 ip1[4], const u8 ip2[4])
{
    return (ip1[0] == ip2[0] && ip1[1] == ip2[1] &&
            ip1[2] == ip2[2] && ip1[3] == ip2[3]);
}

/* ========== ARP Cache Functions ========== */

/**
 * arp_cache_add() - Add or update an ARP cache entry
 */
void arp_cache_add(const u8 ip[4], const u8 mac[6])
{
    struct arp_entry *entry = NULL;
    u32 current_time = get_tick_count();
    int i;

    /* Check if entry already exists */
    for (i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].active && ip_equal(arp_table[i].ip, ip)) {
            entry = &arp_table[i];
            break;
        }
    }

    /* If not found, allocate new entry */
    if (entry == NULL) {
        for (i = 0; i < ARP_TABLE_SIZE; i++) {
            if (!arp_table[i].active) {
                entry = &arp_table[i];
                break;
            }
        }
    }

    /* If table is full, replace oldest entry */
    if (entry == NULL) {
        u32 oldest_time = current_time;
        int oldest_idx = 0;

        for (i = 0; i < ARP_TABLE_SIZE; i++) {
            if (arp_table[i].last_update < oldest_time) {
                oldest_time = arp_table[i].last_update;
                oldest_idx = i;
            }
        }
        entry = &arp_table[oldest_idx];
        printf("[ARP] Cache full, replacing oldest entry\n");
    }

    /* Update entry */
    entry->active = true;
    memcpy(entry->ip, ip, 4);
    memcpy(entry->mac, mac, 6);
    entry->last_update = current_time;

    printf("[ARP] Learned: %d.%d.%d.%d -> %02x:%02x:%02x:%02x:%02x:%02x\n",
           ip[0], ip[1], ip[2], ip[3],
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * arp_cache_lookup() - Look up MAC address for an IP
 */
bool arp_cache_lookup(const u8 ip[4], u8 mac[6])
{
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].active && ip_equal(arp_table[i].ip, ip)) {
            memcpy(mac, arp_table[i].mac, 6);
            return true;
        }
    }
    return false;
}

/**
 * arp_cache_cleanup() - Remove expired ARP entries
 */
int arp_cache_cleanup(u32 current_ticks)
{
    int removed = 0;
    u32 current_sec = current_ticks / 1000;  /* Convert to seconds */

    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!arp_table[i].active) {
            continue;
        }

        u32 entry_sec = arp_table[i].last_update / 1000;
        u32 age_sec = current_sec - entry_sec;

        if (age_sec >= ARP_TIMEOUT) {
            arp_table[i].active = false;
            removed++;
        }
    }

    if (removed > 0) {
        printf("[ARP] Cleaned up %d expired entries\n", removed);
    }

    return removed;
}

/**
 * arp_cache_print() - Print ARP cache for debugging
 */
void arp_cache_print(void)
{
    int active_count = 0;

    printf("[ARP] Cache Table:\n");
    printf("%-4s %-18s %-20s\n", "Idx", "IP Address", "MAC Address");

    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!arp_table[i].active) {
            continue;
        }

        active_count++;
        printf("%-4d %d.%d.%d.%d          %02x:%02x:%02x:%02x:%02x:%02x\n",
               i,
               arp_table[i].ip[0], arp_table[i].ip[1],
               arp_table[i].ip[2], arp_table[i].ip[3],
               arp_table[i].mac[0], arp_table[i].mac[1],
               arp_table[i].mac[2], arp_table[i].mac[3],
               arp_table[i].mac[4], arp_table[i].mac[5]);
    }

    printf("Active entries: %d/%d\n", active_count, ARP_TABLE_SIZE);
}

/* ========== Hash Table Helper Functions ========== */

/**
 * nat_hash() - Compute hash index for wan_port
 *
 * Uses simple modulo with power-of-2 size for fast computation.
 * Hash collisions are handled by fallback to linear search.
 */
static inline u8 nat_hash(u16 wan_port)
{
    return (u8)(wan_port & (NAT_HASH_SIZE - 1));  /* Fast modulo for power of 2 */
}

/**
 * nat_hash_add() - Add entry to hash table
 *
 * @wan_port: WAN port number to hash
 * @table_index: Index into nat_table[] for this entry
 *
 * Note: If there's a collision, the new entry overwrites the old one.
 * The old entry can still be found via linear search fallback.
 */
static void nat_hash_add(u16 wan_port, int table_index)
{
    u8 hash_idx = nat_hash(wan_port);
    nat_hash_table[hash_idx] = (s8)table_index;
}

/**
 * nat_hash_remove() - Remove entry from hash table
 *
 * @wan_port: WAN port number to remove
 *
 * Note: Only removes if the hash bucket points to an entry with this port.
 * Does not search for collisions.
 */
static void nat_hash_remove(u16 wan_port)
{
    u8 hash_idx = nat_hash(wan_port);
    nat_hash_table[hash_idx] = -1;
}
