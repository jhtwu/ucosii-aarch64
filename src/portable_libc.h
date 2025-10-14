#ifndef PORTABLE_LIBC_H
#define PORTABLE_LIBC_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

int printf(const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int puts(const char *s);
int putchar(int c);

void *memset(void *dest, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);

void *malloc(size_t size);
void free(void *ptr);
void exit(int status);

int *__errno_location(void);
extern uintptr_t __stack_chk_guard;
void __stack_chk_fail(void);

#endif /* PORTABLE_LIBC_H */
