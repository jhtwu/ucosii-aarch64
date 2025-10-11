#include "test_support.h"

static uint64_t test_timer_frequency_hz(void)
{
    static uint64_t cached_freq = 0u;

    if (cached_freq == 0u) {
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(cached_freq));
        if (cached_freq == 0u) {
            cached_freq = 1u;
        }
    }

    return cached_freq;
}

uint64_t test_timer_read_cycles(void)
{
    uint64_t cycles;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(cycles));
    return cycles;
}

uint32_t test_cycles_to_us(uint64_t start_cycles, uint64_t end_cycles)
{
    uint64_t freq = test_timer_frequency_hz();
    uint64_t delta = end_cycles - start_cycles;

    __uint128_t numerator = (__uint128_t)delta * 1000000u + ((__uint128_t)freq / 2u);
    return (uint32_t)(numerator / freq);
}

uint16_t test_checksum16(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0u;

    while (length > 1u) {
        sum += (uint32_t)(((uint32_t)bytes[0] << 8) | bytes[1]);
        bytes += 2u;
        length -= 2u;
    }

    if (length == 1u) {
        sum += (uint32_t)bytes[0] << 8;
    }

    while ((sum >> 16u) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16u);
    }

    return (uint16_t)(~sum);
}

void test_store_be16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)(value & 0xFFu);
}

bool test_mac_is_zero(const uint8_t mac[6])
{
    for (size_t i = 0; i < 6u; ++i) {
        if (mac[i] != 0u) {
            return false;
        }
    }
    return true;
}
