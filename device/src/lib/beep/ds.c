#include <stdlib.h>

#include "ds.h"

int qLen(QUEUE q) {
    int len = 0;
    qForEach(void*, i, q) {
        len++;
    }
    return len;
}

void* qNth(QUEUE q, int n) {
    int i = 0;
    qForEach(void*, el, q) {
        if (n == i) {
            return el;
        }
        i++;
    }
    return NULL;
}
