#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "fifo/fifo.h"

#define BUF_SIZE (64 * 1024)

int main(int argc, char **argv) {
    assert((BUF_SIZE % 2) == 0);
    struct __fifo fifo;
    uint8_t *buf = (uint8_t *)malloc(BUF_SIZE);
    uint8_t *t = (uint8_t *)malloc(BUF_SIZE);
    int i, j, k, l;
    int c = 0;
    struct timeval start;
    struct timeval stop;
    double iter;
    uint64_t start_us;
    uint64_t stop_us;
    assert(buf);
    assert(t);
    memset(buf, 0, BUF_SIZE);
    memset(t, 0, BUF_SIZE);

    __fifo_init(&fifo, buf, BUF_SIZE, FIFO_TRACK_STALE);

    // check init values.
    assert(__fifo_bytes_free(&fifo) == BUF_SIZE - 1);
    assert(__fifo_bytes_used(&fifo, true) == 0);
    assert(__fifo_empty(&fifo));

    // enq/deq.
    i = BUF_SIZE / 2;
    c++;
    memset(t, c, i);
    assert(__fifo_enq(&fifo, t, i) == i);
    assert(__fifo_bytes_free(&fifo) == BUF_SIZE - i - 1);
    assert(__fifo_bytes_used(&fifo, true) == i);
    memset(t, 0, i);
    assert(__fifo_deq(&fifo, t, i, true) == i);
    while (i--)
        assert(t[i] == c);
    assert(__fifo_bytes_free(&fifo) == BUF_SIZE - 1);
    assert(__fifo_bytes_used(&fifo, true) == 0);

    // enq/deq wrapping around.
    i = (BUF_SIZE * 3) / 4;
    c++;
    memset(t, c, i);
    assert(__fifo_enq(&fifo, t, i) == i);
    assert(__fifo_bytes_free(&fifo) == BUF_SIZE - i - 1);
    assert(__fifo_bytes_used(&fifo, true) == i);
    memset(t, 0, i);
    assert(__fifo_deq(&fifo, t, i, true) == i);
    while (i--)
        assert(t[i] == c);
    assert(__fifo_bytes_free(&fifo) == BUF_SIZE - 1);
    assert(__fifo_bytes_used(&fifo, true) == 0);

    // multiple enq/deq.
    i = BUF_SIZE / 8;
    c++;
    j = 0;
    while (j++ < 6) {
        memset(t, c, i);
        assert(__fifo_enq(&fifo, t, i) == i);
        assert(__fifo_bytes_free(&fifo) == BUF_SIZE - (j * i) - 1);
        assert(__fifo_bytes_used(&fifo, true) == j * i);
        c++;
    }
    c -= j - 1;
    j--;
    while (j-- > 0) {
        memset(t, 0, i);
        assert(__fifo_deq(&fifo, t, i, true) == i);
        assert(__fifo_bytes_free(&fifo) == BUF_SIZE - (j * i) - 1);
        assert(__fifo_bytes_used(&fifo, true) == j * i);
        k = i;
        while (k--)
            assert(t[k] == c);
        c++;
    }
    c--;

    // ptr boundaries.
    for (j = -1; j < 2; j++) {
        __fifo_reset(&fifo);
        i = ((BUF_SIZE * 3) / 4) + j;
        __fifo_enq(&fifo, t, i);
        __fifo_deq(&fifo, t, i, true);
        i = BUF_SIZE / 4;
        c++;
        memset(t, c, i);
        assert(__fifo_enq(&fifo, t, i) == i);
        assert(__fifo_bytes_free(&fifo) == BUF_SIZE - i - 1);
        c++;
        memset(t, c, i);
        assert(__fifo_enq(&fifo, t, i) == i);
        c--;
        memset(t, 0, i);
        assert(__fifo_deq(&fifo, t, i, true) == i);
        k = i;
        while (k--)
            assert(t[k] == c);
        c++;
        memset(t, 0, i);
        assert(__fifo_deq(&fifo, t, i, true) == i);
        k = i;
        while (k--)
            assert(t[k] == c);
    }

    // read/stale tracking.
    i = BUF_SIZE / 8;
    c++;
    j = 0;
    l = 6;
    while (j++ < l) {
        memset(t, c, i);
        assert(__fifo_enq(&fifo, t, i) == i);
        assert(__fifo_bytes_free(&fifo) == BUF_SIZE - (j * i) - 1);
        assert(__fifo_bytes_used(&fifo, true) == j * i);
        c++;
    }
    c -= j - 1;
    j--;
    while (j-- > 0) {
        memset(t, 0, i);
        if (j > l - 3) {
            assert(__fifo_deq(&fifo, t, i, false) == i);
            assert(__fifo_bytes_free(&fifo) == BUF_SIZE - (l * i) - 1);
            assert(__fifo_bytes_used(&fifo, true) == l * i);
            assert(__fifo_bytes_used(&fifo, false) == j * i);
        } else {
            assert(__fifo_read(&fifo, t, (l - j - 3) * i, i, false) == i);
        }
        k = i;
        while (k--)
            assert(t[k] == c);
        c++;
    }
    c -= l;
    j = l;
    while (j-- > 0) {
        memset(t, 0, i);
        assert(__fifo_read(&fifo, t, (l - j - 1) * i, i, true) == i);
        k = i;
        while (k--)
            assert(t[k] == c);
        c++;
    }

    c -= l;
    j = 0;
    // Only l - 3 blocks are stale.
    while (++j < l - 3) {
        memset(t, 0, i);
        assert(__fifo_read(&fifo, t, 0, i, true) == i);
        assert(__fifo_discard(&fifo, i) == i);
        k = i;
        while (k--)
            assert(t[k] == c);
        c++;
    }

    c--;

    __fifo_free(&fifo);

    // no stale tracking.
    __fifo_init(&fifo, buf, BUF_SIZE, FIFO_NO_OPTIONS);
    i = BUF_SIZE / 2;
    c++;
    memset(t, c, i);
    assert(__fifo_enq(&fifo, t, i) == i);
    assert(__fifo_deq(&fifo, t, i, false) == i);
    assert(__fifo_bytes_used(&fifo, true) == 0);
    assert(__fifo_bytes_used(&fifo, false) == 0);
    assert(__fifo_bytes_free(&fifo) == BUF_SIZE - 1);

    // no stale tracking perf.
    __fifo_free(&fifo);
    __fifo_init(&fifo, buf, BUF_SIZE, FIFO_NO_OPTIONS);
    i = 4;
    j = 0;
    gettimeofday(&start, NULL);
    for (; j < 1000; j++) {
        while(__fifo_bytes_free(&fifo) > i) {
            __fifo_enq(&fifo, t, i);
        }
        while(__fifo_bytes_used(&fifo, true) > 0) {
            __fifo_deq(&fifo, t, i, true);
        }
    }
    gettimeofday(&stop, NULL);
    iter = ((BUF_SIZE - 1) / i) * 2 * j;
    iter *= 1000;
    start_us = (start.tv_sec * 1000000) + start.tv_usec;
    stop_us = (stop.tv_sec * 1000000) + stop.tv_usec;
    printf("no options iter/ms: %f\n", (iter / (stop_us - start_us)));

    // stale tracking perf.
    __fifo_free(&fifo);
    __fifo_init(&fifo, buf, BUF_SIZE, FIFO_TRACK_STALE);
    i = 4;
    j = 0;
    gettimeofday(&start, NULL);
    for (; j < 1000; j++) {
        while(__fifo_bytes_free(&fifo) > i) {
            __fifo_enq(&fifo, t, i);
        }
        while(__fifo_bytes_used(&fifo, true) > 0) {
            __fifo_deq(&fifo, t, i, true);
        }
    }
    gettimeofday(&stop, NULL);
    iter = ((BUF_SIZE - 1) / i) * 2 * j;
    iter *= 1000;
    start_us = (start.tv_sec * 1000000) + start.tv_usec;
    stop_us = (stop.tv_sec * 1000000) + stop.tv_usec;
    printf("stale track iter/ms: %f\n", (iter / (stop_us - start_us)));

    // locking perf.
    __fifo_free(&fifo);
    __fifo_init(&fifo, buf, BUF_SIZE, FIFO_LOCK_SUPPORT);
    i = 4;
    j = 0;
    gettimeofday(&start, NULL);
    for (; j < 1000; j++) {
        while(!__fifo_lock(&fifo) && (__fifo_bytes_free(&fifo) > i)) {
            __fifo_enq(&fifo, t, i);
            __fifo_unlock(&fifo);
        }
        __fifo_unlock(&fifo);
        while(!__fifo_lock(&fifo) && (__fifo_bytes_used(&fifo, true) > 0)) {
            __fifo_deq(&fifo, t, i, true);
            __fifo_unlock(&fifo);
        }
        __fifo_unlock(&fifo);
    }
    gettimeofday(&stop, NULL);
    iter = ((BUF_SIZE - 1) / i) * 2 * j;
    iter *= 1000;
    start_us = (start.tv_sec * 1000000) + start.tv_usec;
    stop_us = (stop.tv_sec * 1000000) + stop.tv_usec;
    printf("locking iter/ms: %f\n", (iter / (stop_us - start_us)));
    __fifo_free(&fifo);

}

