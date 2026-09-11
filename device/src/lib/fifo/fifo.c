#include <stdlib.h>
#include <string.h>

#include "fifo/fifo.h"

#define FIFO_CHLOCK(FIFO) ((FIFO)->options & FIFO_LOCK_SUPPORT)
#define ASSERT___FIFO_CHLOCK_LOCKED(FIFO) \
    assert(((FIFO)->options & FIFO_LOCK_SUPPORT) && (FIFO)->lock)

int __fifo_init(struct __fifo *fifo, uint8_t *base, size_t size, int options) {
    if (!fifo || !base)
        return -1;

    memset(fifo, 0, sizeof(struct __fifo));
    fifo->base = base;
    fifo->end = base + size;
    fifo->size = size;
    fifo->options = options;
    fifo->sptr = fifo->base;
    fifo->rptr = fifo->base;
    fifo->wptr = fifo->base;

    if (FIFO_CHLOCK(fifo)) {
        pthread_mutexattr_t mutex_attr;
        pthread_condattr_t cond_attr;
        int err;

        if ((err = pthread_mutexattr_init(&mutex_attr)) < 0) {
            return err;
        }

        if (fifo->options & FIFO_PRIO_INHERIT) {
            if ((err = pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED)) < 0) {
                return err;
            }
#if defined(_POSIX_THREAD_PRIO_INHERIT) && 0
            if ((err = uname(&utsname)) < 0) {
                return err;
            }
            if (!RUNNING_ON_VALGRIND && strstr(utsname.version, "PREEMPT") != NULL) {
                if ((err = pthread_mutexattr_setprotocol(&mutex_attr, PTHREAD_PRIO_INHERIT)) < 0) {
                    return err;
                }
            }
#endif  // _POSIX_THREAD_PRIO_INHERIT
        }
        if ((err = pthread_mutex_init(&fifo->mutex, &mutex_attr)) < 0) {
            return err;
        }

        if ((err = pthread_condattr_init(&cond_attr)) < 0) {
            return err;
        }

        if (fifo->options & FIFO_PRIO_INHERIT) {
            if ((err = pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED)) < 0) {
                return err;
            }
        }
        if ((err = pthread_cond_init(&fifo->cond, &cond_attr)) < 0) {
            return err;
        }
    }

    return 0;
}

void __fifo_free(struct __fifo *fifo) {
    if (FIFO_CHLOCK(fifo)) {
        pthread_cond_destroy(&fifo->cond);
        pthread_mutex_destroy(&fifo->mutex);
    }
    memset(fifo, 0, sizeof(struct __fifo));
}

bool __fifo_empty(struct __fifo *fifo) {
    ASSERT___FIFO_LOCKED(fifo);
    return (fifo->rptr == fifo->wptr);
}

size_t __fifo_bytes_used(struct __fifo *fifo, bool stale) {
    ASSERT___FIFO_LOCKED(fifo);

    if (stale && (fifo->options & FIFO_TRACK_STALE)) {
        return (fifo->wptr >= fifo->sptr) ? (fifo->wptr - fifo->sptr) :
            (fifo->wptr - fifo->sptr + fifo->size);
    } else {
        return (fifo->wptr >= fifo->rptr) ? (fifo->wptr - fifo->rptr) :
            (fifo->wptr - fifo->rptr + fifo->size);
    }
}

size_t __fifo_bytes_free(struct __fifo *fifo) {
    ASSERT___FIFO_LOCKED(fifo);

    return (fifo->sptr > fifo->wptr) ? (fifo->sptr - fifo->wptr - 1) :
        (fifo->sptr - fifo->wptr + fifo->size - 1);
}

void __fifo_reset(struct __fifo *fifo) {
    ASSERT___FIFO_LOCKED(fifo);
    fifo->sptr = fifo->base;
    fifo->rptr = fifo->base;
    fifo->wptr = fifo->base;
}

static inline uint8_t *__fifo_inc_ptr(struct __fifo *fifo, uint8_t *ptr, size_t bytes) {
    ptr += bytes;
    if (ptr >= fifo->end)
        ptr -= fifo->size;
    return ptr;
}

static inline void __fifo_copy_out(struct __fifo *fifo, uint8_t *ptr, uint8_t *buf,
    size_t size) {
    size_t contb;

    contb = fifo->end - ptr;
    if (contb >= size) {
        memcpy(buf, ptr, size);
    } else {
        memcpy(buf, ptr, contb);
        memcpy(buf + contb, fifo->base, size - contb);
    }
}

size_t __fifo_enq(struct __fifo *fifo, uint8_t *buf, size_t size) {
    size_t contb;

    ASSERT___FIFO_LOCKED(fifo);
    if (__fifo_bytes_free(fifo) < size)
        return 0;

    contb = fifo->end - fifo->wptr;
    if (contb >= size) {
        memcpy(fifo->wptr, buf, size);
        fifo->wptr = ((fifo->wptr + size) == fifo->end) ? fifo->base : fifo->wptr + size;
    } else {
        memcpy(fifo->wptr, buf, contb);
        memcpy(fifo->base, buf + contb, size - contb);
        fifo->wptr = fifo->base + size - contb;
    }
    return size;
}

size_t __fifo_deq(struct __fifo *fifo, uint8_t *buf, size_t size, bool discard) {
    ASSERT___FIFO_LOCKED(fifo);
    if (__fifo_bytes_used(fifo, false) < size) {
        return 0;
    }

    __fifo_copy_out(fifo, fifo->rptr, buf, size);
    fifo->rptr = __fifo_inc_ptr(fifo, fifo->rptr, size);
    if (discard || !(fifo->options & FIFO_TRACK_STALE)) {
        fifo->sptr = fifo->rptr;
    }
    return size;
}

size_t __fifo_read(struct __fifo *fifo, uint8_t *buf, size_t offset,
    size_t size, bool stale) {
    ASSERT___FIFO_LOCKED(fifo);
    if (__fifo_bytes_used(fifo, stale) < (size + offset)) {
        return 0;
    }

    uint8_t *ptr = __fifo_inc_ptr(fifo, stale ? fifo->sptr : fifo->rptr, offset);
    __fifo_copy_out(fifo, ptr, buf, size);
    return size;
}

size_t __fifo_discard(struct __fifo *fifo, size_t size) {
    size_t diff;

    ASSERT___FIFO_LOCKED(fifo);

    // Used stale data should always be greater than or equal to used fresh
    // data.
    diff = __fifo_bytes_used(fifo, true) - __fifo_bytes_used(fifo, false);
    if (diff <= size) {
        fifo->sptr = fifo->rptr;
        return diff;
    }

    fifo->sptr = __fifo_inc_ptr(fifo, fifo->sptr, size);
    return size;
}

int __fifo_lock(struct __fifo *fifo) {
    int ret;

    assert(fifo->options & FIFO_LOCK_SUPPORT);

#ifdef DEBUG_FIFO
    if (DEBUG_FIFO > 0 ) {
        printf(">> LOCK %p\n", fifo);
    }
#endif

    ret = pthread_mutex_lock(&fifo->mutex);

    fifo->lock = true;
    return ret;
}

int __fifo_unlock(struct __fifo *fifo) {
    ASSERT___FIFO_CHLOCK_LOCKED(fifo);

#ifdef DEBUG_FIFO
    if (DEBUG_FIFO > 0) {
        printf("<< UNLOCK %p\n", fifo);
    }
#endif

    fifo->lock = false;
    return pthread_mutex_unlock(&fifo->mutex);
}

int __fifo_signal(struct __fifo *fifo) {
    ASSERT___FIFO_CHLOCK_LOCKED(fifo);
    return pthread_cond_signal(&fifo->cond);
}

int __fifo_wait_timeout(struct __fifo *fifo, uint32_t ms) {
    struct timeval delta;
    struct timespec abstime;
    int ret;

    ASSERT___FIFO_CHLOCK_LOCKED(fifo);
    fifo->lock = false;

    gettimeofday(&delta, NULL);

    abstime.tv_sec = delta.tv_sec + (ms / 1000);
    abstime.tv_nsec = (delta.tv_usec + (ms % 1000) * 1000) * 1000;
    if ( abstime.tv_nsec >= 1000000000 ) {
        abstime.tv_sec += 1;
        abstime.tv_nsec -= 1000000000;
    }

    do {
        ret = pthread_cond_timedwait(&fifo->cond, &fifo->mutex, &abstime);
    } while (ret == EINTR);

    fifo->lock = true;
    return ret;
}
