#ifndef BEEP_FIFO_H
#define BEEP_FIFO_H

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#define FIFO_NO_OPTIONS         (0)
#define FIFO_LOCK_SUPPORT     (1<<0)
#define FIFO_PRIO_INHERIT       (1<<1)
#define FIFO_TRACK_STALE        (1<<2)

#define ASSERT___FIFO_LOCKED(__FIFO__) \
    do { \
        if ((__FIFO__)->options & FIFO_LOCK_SUPPORT) { \
            assert((__FIFO__)->lock); \
        } \
    } while(0)

struct __fifo {
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    uint8_t *base;
    uint8_t *end;
    uint8_t *wptr;
    uint8_t *rptr;
    uint8_t *sptr;
    size_t size;
    int options;
    bool lock;
};

// init fifo with options.
int __fifo_init(struct __fifo *fifo, uint8_t *base, size_t size, int options);

// cleanup fifo.
void __fifo_free(struct __fifo *fifo);

// fifo bytes free.
size_t __fifo_bytes_free(struct __fifo *fifo);

// if fifo is empty (i.e. size - 1)
bool __fifo_empty(struct __fifo *fifo);

// fifo bytes used, optionally including stale bytes.
size_t __fifo_bytes_used(struct __fifo *fifo, bool stale);

// reset fifo pointers.
void __fifo_reset(struct __fifo *fifo);

// enqueue data, update free pointer.
size_t __fifo_enq(struct __fifo *fifo, uint8_t *buf, size_t size);

// dequeue data, update fresh pointer, optionally update stale pointer.
size_t __fifo_deq(struct __fifo *fifo, uint8_t *buf, size_t size, bool discard);

// read from either fresh or stale data, no pointers updated.
size_t __fifo_read(struct __fifo *fifo, uint8_t *buf, size_t offset,
        size_t size, bool stale);

// discard data updating stale pointer no further than fresh pointer.
size_t __fifo_discard(struct __fifo *fifo, size_t size);

// fifo thread support.
int __fifo_lock(struct __fifo *fifo);
int __fifo_unlock(struct __fifo *fifo);
int __fifo_signal(struct __fifo *fifo);
int __fifo_wait_timeout(struct __fifo *fifo, uint32_t ms);

#endif  // BEEP_FIFO_H
