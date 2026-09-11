#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machdefs.h"

#ifdef BMEM_COUNTER
static int64_t bblocks;

__attribute__((destructor))
static void dump_bblocks(void) {
    printf("exit bblocks value: %" PRId64 "\n", bblocks);
}
#endif

// This will make sense later for safety checks and security.
void *bmalloc(size_t size) {
#ifdef BMEM_COUNTER
    bblocks++;
#endif
    void *p = malloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

void bfree(void *ptr) {
#ifdef BMEM_COUNTER
    bblocks--;
#endif
    free(ptr);
}

char *bstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = (char *)bmalloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

char *bstrndup(const char *s, size_t n) {
    n = strnlen(s, n);
    char *d = (char *)bmalloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = '\0';
    }
    return d;
}
