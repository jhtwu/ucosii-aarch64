#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#include "portable_libc.h"
#include "pl011.h"
#include "uart.h"

/*
 * Minimal C runtime support to allow building with Ubuntu's aarch64-linux-gnu toolchain.
 * Provides lightweight implementations for common libc entry points expected by the
 * firmware. Formatting support is intentionally limited to the conversion specifiers
 * used throughout this project.
 */

extern void *_sbrk(ptrdiff_t size);
struct _reent;

/* -------------------------------------------------------------------------- */
/* errno handling                                                             */
/* -------------------------------------------------------------------------- */

static int g_errno_storage;

int *__errno_location(void)
{
    return &g_errno_storage;
}

/* -------------------------------------------------------------------------- */
/* Stack protector hooks                                                      */
/* -------------------------------------------------------------------------- */

uintptr_t __stack_chk_guard = UINT64_C(0xdeadbeefcafebabe);

void __stack_chk_fail(void)
{
    uart_puts("*** stack smashing detected ***\n");
    while (1) {
    }
}

/* -------------------------------------------------------------------------- */
/* Memory primitives                                                          */
/* -------------------------------------------------------------------------- */

void *memset(void *dest, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    for (size_t i = 0; i < n; ++i) {
        d[i] = (unsigned char)c;
    }
    return dest;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0u) {
        return dest;
    }

    if (d < s) {
        for (size_t i = 0; i < n; ++i) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }

    return dest;
}

size_t strlen(const char *s)
{
    size_t len = 0;
    while (*s++ != '\0') {
        ++len;
    }
    return len;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++) != '\0') {
    }
    return dest;
}

char *strncat(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (*d != '\0') {
        ++d;
    }
    for (size_t i = 0; i < n && src[i] != '\0'; ++i) {
        *d++ = src[i];
    }
    *d = '\0';
    return dest;
}

/* -------------------------------------------------------------------------- */
/* Minimal heap primitives                                                    */
/* -------------------------------------------------------------------------- */

typedef struct alloc_header {
    size_t size;
} alloc_header_t;

void *malloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }

    size_t total = size + sizeof(alloc_header_t);
    alloc_header_t *hdr = (alloc_header_t *)_sbrk((ptrdiff_t)total);
    if (hdr == (alloc_header_t *)-1) {
        return NULL;
    }
    hdr->size = size;
    return (void *)(hdr + 1);
}

void free(void *ptr)
{
    (void)ptr;
    /* Simple bump allocator: free() is intentionally a no-op. */
}

/* -------------------------------------------------------------------------- */
/* Number formatting helpers                                                  */
/* -------------------------------------------------------------------------- */

static void reverse_buffer(char *buf, size_t len)
{
    for (size_t i = 0; i < len / 2; ++i) {
        char tmp = buf[i];
        buf[i] = buf[len - 1 - i];
        buf[len - 1 - i] = tmp;
    }
}

static size_t utoa_base(unsigned long long value, unsigned base, bool upper, char *out)
{
    static const char digits_lc[] = "0123456789abcdef";
    static const char digits_uc[] = "0123456789ABCDEF";
    const char *digits = upper ? digits_uc : digits_lc;

    if (base < 2u || base > 16u) {
        return 0u;
    }

    size_t len = 0u;
    do {
        out[len++] = digits[value % base];
        value /= base;
    } while (value != 0u);

    reverse_buffer(out, len);
    return len;
}

static int append_char(char **buffer, size_t *remaining, size_t *written, char c)
{
    if (*remaining > 1u) {
        **buffer = c;
        (*buffer)++;
        (*remaining)--;
    }
    (*written)++;
    return 0;
}

static int append_string(char **buffer, size_t *remaining, size_t *written, const char *s)
{
    while (*s != '\0') {
        append_char(buffer, remaining, written, *s++);
    }
    return 0;
}

static int pad_with(char **buffer, size_t *remaining, size_t *written, char pad, int count)
{
    for (int i = 0; i < count; ++i) {
        append_char(buffer, remaining, written, pad);
    }
    return 0;
}

static int print_signed(char **buffer, size_t *remaining, size_t *written,
                        long long value, unsigned base, bool upper,
                        int width, bool zero_pad)
{
    char tmp[32];
    bool negative = false;
    unsigned long long abs_val;

    if (base == 10u && value < 0) {
        negative = true;
        abs_val = (unsigned long long)(-value);
    } else {
        abs_val = (unsigned long long)value;
    }

    size_t len = utoa_base(abs_val, base, upper, tmp);
    size_t digits_len = len;
    size_t sign_len = negative ? 1u : 0u;
    int pad_len = width - (int)(digits_len + sign_len);

    if (!zero_pad) {
        pad_with(buffer, remaining, written, ' ', pad_len > 0 ? pad_len : 0);
        pad_len = 0;
    }

    if (negative) {
        append_char(buffer, remaining, written, '-');
    }

    if (zero_pad) {
        pad_with(buffer, remaining, written, '0', pad_len > 0 ? pad_len : 0);
    }

    for (size_t i = 0; i < digits_len; ++i) {
        append_char(buffer, remaining, written, tmp[i]);
    }

    return 0;
}

static int print_unsigned(char **buffer, size_t *remaining, size_t *written,
                          unsigned long long value, unsigned base, bool upper,
                          int width, bool zero_pad, bool prefix)
{
    char tmp[32];
    size_t len = utoa_base(value, base, upper, tmp);

    size_t prefix_len = prefix ? 2u : 0u;
    int pad_len = width - (int)(len + prefix_len);
    if (!zero_pad) {
        pad_with(buffer, remaining, written, ' ', pad_len > 0 ? pad_len : 0);
        pad_len = 0;
    }

    if (prefix) {
        append_char(buffer, remaining, written, '0');
        append_char(buffer, remaining, written, upper ? 'X' : 'x');
    }

    if (zero_pad) {
        pad_with(buffer, remaining, written, '0', pad_len > 0 ? pad_len : 0);
    }

    for (size_t i = 0; i < len; ++i) {
        append_char(buffer, remaining, written, tmp[i]);
    }

    return 0;
}

static int mini_vsnprintf(char *out, size_t size, const char *fmt, va_list args)
{
    char *buffer = out;
    size_t remaining = (size > 0u) ? size : 0u;
    size_t written = 0u;

    while (*fmt != '\0') {
        if (*fmt != '%') {
            append_char(&buffer, &remaining, &written, *fmt++);
            continue;
        }

        ++fmt; /* Skip '%' */

        bool zero_pad = false;
        int width = 0;
        bool long_mod = false;
        bool long_long_mod = false;

        if (*fmt == '0') {
            zero_pad = true;
            ++fmt;
        }

        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            ++fmt;
        }

        if (*fmt == 'l') {
            ++fmt;
            if (*fmt == 'l') {
                long_long_mod = true;
                ++fmt;
            } else {
                long_mod = true;
            }
        }

        char spec = *fmt++;
        switch (spec) {
            case 'd':
            case 'i': {
                long long val;
                if (long_long_mod) {
                    val = va_arg(args, long long);
                } else if (long_mod) {
                    val = va_arg(args, long);
                } else {
                    val = va_arg(args, int);
                }
                print_signed(&buffer, &remaining, &written, val, 10u, false, width, zero_pad);
                break;
            }
            case 'u': {
                unsigned long long val;
                if (long_long_mod) {
                    val = va_arg(args, unsigned long long);
                } else if (long_mod) {
                    val = va_arg(args, unsigned long);
                } else {
                    val = va_arg(args, unsigned int);
                }
                print_unsigned(&buffer, &remaining, &written, val, 10u, false, width, zero_pad, false);
                break;
            }
            case 'x':
            case 'X': {
                unsigned long long val;
                if (long_long_mod) {
                    val = va_arg(args, unsigned long long);
                } else if (long_mod) {
                    val = va_arg(args, unsigned long);
                } else {
                    val = va_arg(args, unsigned int);
                }
                bool upper = (spec == 'X');
                bool prefix = false;
                print_unsigned(&buffer, &remaining, &written, val, 16u, upper, width, zero_pad, prefix);
                break;
            }
            case 'p': {
                uintptr_t ptr = (uintptr_t)va_arg(args, void *);
                print_unsigned(&buffer, &remaining, &written, (unsigned long long)ptr, 16u, false, (int)(sizeof(uintptr_t) * 2 + 2), true, true);
                break;
            }
            case 'c': {
                int c = va_arg(args, int);
                append_char(&buffer, &remaining, &written, (char)c);
                break;
            }
            case 's': {
                const char *str = va_arg(args, const char *);
                if (str == NULL) {
                    str = "(null)";
                }
                append_string(&buffer, &remaining, &written, str);
                break;
            }
            case '%': {
                append_char(&buffer, &remaining, &written, '%');
                break;
            }
            default:
                append_char(&buffer, &remaining, &written, spec);
                break;
        }
    }

    if (size > 0u) {
        if (remaining == 0u) {
            out[size - 1u] = '\0';
        } else {
            *buffer = '\0';
        }
    }

    return (int)written;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
    return mini_vsnprintf(str, size, format, ap);
}

int snprintf(char *str, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = mini_vsnprintf(str, size, format, args);
    va_end(args);
    return result;
}

int sprintf(char *str, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = mini_vsnprintf(str, SIZE_MAX, format, args);
    va_end(args);
    return result;
}

int printf(const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    int result = mini_vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    uart_puts(buffer);
    return result;
}

int puts(const char *s)
{
    uart_puts(s);
    uart_putc('\n');
    return (int)strlen(s) + 1;
}

int putchar(int c)
{
    uart_putc((char)c);
    return c;
}

void exit(int status)
{
    (void)status;
    uart_puts("System exit requested\n");
    while (1) {
    }
}

/* Provide weak aliases for compatibility with certain toolchain expectations. */
void *_malloc_r(struct _reent *r, size_t n) __attribute__((weak, alias("malloc")));
void _free_r(struct _reent *r, void *p) __attribute__((weak, alias("free")));
