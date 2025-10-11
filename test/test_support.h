#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint64_t test_timer_read_cycles(void);
uint32_t test_cycles_to_us(uint64_t start_cycles, uint64_t end_cycles);
uint16_t test_checksum16(const void *data, size_t length);
void test_store_be16(uint8_t *dst, uint16_t value);
bool test_mac_is_zero(const uint8_t mac[6]);

#endif /* TEST_SUPPORT_H */
