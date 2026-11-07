/* stdlib.h ridotto di RegiScope: malloc/free sono tracciati nel pannello RAM. */
#ifndef REGISCOPE_STDLIB_H
#define REGISCOPE_STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void *calloc(size_t count, size_t n);
void *realloc(void *p, size_t n);
void free(void *p);
int abs(int x);
long labs(long x);
int atoi(const char *s);
int rand(void);
void srand(unsigned seed);
#define RAND_MAX 0x7fff
#endif
