#ifndef BEEP_DS_H
#define BEEP_DS_H

#include "libds/ds.h"

#define qForEach(type, it_name, q) \
    for (type it_name = (type) qFirst(q); it_name; it_name = (type) qNext(q))

#define qForEachReverse(type, it_name, q) \
    for (type it_name = (type) qLast(q); it_name; it_name = (type) qPrev(q))

int qLen(QUEUE q);
void* qNth(QUEUE q, int n);

#endif  // BEEP_DS_H
