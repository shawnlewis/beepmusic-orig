#ifndef AUDIO_FIFO2_H
#define AUDIO_FIFO2_H

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define ASSERT_FIFO_LOCKED(fifo) assert((fifo)->lock)

struct fifo2 {
    // thread control.
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool lock;

    // pointer of caller provided buffer.
    uint8_t *base;
    // size of caller provided buffer.
    size_t size;
    // counter to beginning of free data.
    size_t wptr;
    // counter to beginning of fresh data.
    size_t rptr;
    // counter to beginning of stale data.
    size_t sptr;
};

int fifo2_init(struct fifo2 *fifo, uint8_t *base, size_t size, bool prio_inherit);
void fifo2_free(struct fifo2 *fifo);
//bool fifo2_empty(struct fifo2 *fifo);
//size_t fifo2_bytes_used(struct fifo2 *fifo);
size_t fifo2_bytes_free(struct fifo2 *fifo);
//size_t fifo2_bytes_until_rptr_wrap(struct fifo2 *fifo);
//size_t fifo2_bytes_until_wptr_wrap(struct fifo2 *fifo);
//void fifo2_rptr_incby(struct fifo2 *fifo, size_t incby);
//void fifo2_wptr_incby(struct fifo2 *fifo, size_t incby);

// New functions, everything will assert if the fifo is unlocked.
// fresh and optionally stale data used.
size_t fifo2_bytes_used(struct fifo2 *fifo, bool stale);
// reset fifo2 pointers.
void fifo2_flush(struct fifo2 *fifo);
// enqueue data, update free pointer.
size_t fifo2_enq(struct fifo2 *fifo, const uint8_t *buf, size_t size);
// dequeue data, update fresh pointer, optionally update stale pointer.
size_t fifo2_deq(struct fifo2 *fifo, uint8_t *buf, size_t size, bool auto_discard);
// read from either fresh to stale data, no pointers updated.
size_t fifo2_read(struct fifo2 *fifo, uint8_t *buf, size_t offset,
    size_t size, bool stale);
// discard data updating stale pointer no further than fresh pointer.
size_t fifo2_discard(struct fifo2 *fifo, size_t size);

// fifo thread support.
int fifo2_lock(struct fifo2 *fifo);
int fifo2_unlock(struct fifo2 *fifo);
int fifo2_signal(struct fifo2 *fifo);
int fifo2_wait_timeout(struct fifo2 *fifo, uint32_t ms);

#endif // AUDIO_FIFO2_H

