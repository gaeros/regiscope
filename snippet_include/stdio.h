/* stdio.h ridotto di RegiScope: le funzioni sono fornite dall'app. */
#ifndef REGISCOPE_STDIO_H
#define REGISCOPE_STDIO_H
#include <stddef.h>
int printf(const char *fmt, ...);
int puts(const char *s);
int putchar(int c);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t n, const char *fmt, ...);
#endif
