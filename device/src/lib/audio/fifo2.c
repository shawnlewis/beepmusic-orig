
// This also could have been an extension on fifo.c.
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>
#include <errno.h>

#include "audio/fifo2.h"

//#define DEBUG_FIFO 1

#ifdef DEBUG_FIFO

#include <execinfo.h>

// Note: this may break if run in realtime.
static void print_trace(void)
{
    void *array[4];
    size_t size;
    char **strings;
    size_t i;

    /* backtrace */
    size = backtrace(array, sizeof(array)/sizeof(void *));
    strings = backtrace_symbols(array, size);

    printf("Backtrack:\n");
    for (i = 0; i < size; i++) {
        printf("\t%s\n", strings[i]);
    }

    free(strings);
}

#endif

int fifo2_init(struct fifo2 *fifo, uint8_t *base, size_t size, bool prio_inherit) {
    /* linux multi-process locking */
    pthread_mutexattr_t mutex_attr;
    pthread_condattr_t cond_attr;
    //struct utsname utsname;
    int err;

    if ((err = pthread_mutexattr_init(&mutex_attr)) < 0) {
        return err;
    }
    if (prio_inherit) {
        if ((err = pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED)) < 0) {
            return err;
        }
#ifdef _POSIX_THREAD_PRIO_INHERIT
        /* only on PREEMPT kernels */
        if ((err = uname(&utsname)) < 0) {
            return err;
        }
        if (!RUNNING_ON_VALGRIND && strstr(utsname.version, "PREEMPT") != NULL) {
            if ((err = pthread_mutexattr_setprotocol(&mutex_attr, PTHREAD_PRIO_INHERIT)) < 0) {
                return err;
            }
        }
#endif /* _POSIX_THREAD_PRIO_INHERIT */
    }
    if ((err = pthread_mutex_init(&fifo->mutex, &mutex_attr)) < 0) {
        return err;
    }

    if ((err = pthread_condattr_init(&cond_attr)) < 0) {
        return err;
    }
    if (prio_inherit) {
        if ((err = pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED)) < 0) {
            return err;
        }
    }
    if ((err = pthread_cond_init(&fifo->cond, &cond_attr)) < 0) {
        return err;
    }
    fifo->base = base;
    fifo->sptr = 0;
    fifo->rptr = 0;
    fifo->wptr = 0;
    fifo->size = size;

    return 0;
}

void fifo2_free(struct fifo2 *fifo) {
    /* linux multi-process locking */
    pthread_cond_destroy(&fifo->cond);
    pthread_mutex_destroy(&fifo->mutex);
    fifo->base = NULL;
    fifo->sptr = 0;
    fifo->rptr = 0;
    fifo->wptr = 0;
    fifo->size = 0;
}

bool fifo2_empty(struct fifo2 *fifo) {
    ASSERT_FIFO_LOCKED(fifo);

    return (fifo->rptr == fifo->wptr);
}

size_t fifo2_bytes_used(struct fifo2 *fifo, bool stale) {
    ASSERT_FIFO_LOCKED(fifo);

    if (stale) {
        return (fifo->wptr >= fifo->sptr) ? (fifo->wptr - fifo->sptr ) :
            (fifo->wptr - fifo->sptr + fifo->size);
    } else {
        return (fifo->wptr >= fifo->rptr) ? (fifo->wptr - fifo->rptr ) :
            (fifo->wptr - fifo->rptr + fifo->size);
    }
}

size_t fifo2_bytes_free(struct fifo2 *fifo) {
    ASSERT_FIFO_LOCKED(fifo);

    return (fifo->sptr > fifo->wptr) ? (fifo->sptr - fifo->wptr - 1) :
        (fifo->sptr - fifo->wptr + fifo->size - 1);
}

size_t fifo2_bytes_until_rptr_wrap(struct fifo2 *fifo) {
    ASSERT_FIFO_LOCKED(fifo);

    return (fifo->size-fifo->rptr);
}

size_t fifo2_bytes_until_wptr_wrap(struct fifo2 *fifo) {
    ASSERT_FIFO_LOCKED(fifo);

    return (fifo->size-fifo->wptr);
}

void fifo2_rptr_incby(struct fifo2 *fifo, size_t incby) {
    ASSERT_FIFO_LOCKED(fifo);

    if (fifo->rptr + incby == fifo->size) {
        fifo->rptr = 0;
    } else {
        fifo->rptr += incby;
    }
}

void fifo2_wptr_incby(struct fifo2 *fifo, size_t incby) {
    ASSERT_FIFO_LOCKED(fifo);

    if (fifo->wptr + incby == fifo->size) {
        fifo->wptr = 0;
    } else {
        fifo->wptr += incby;
    }
}

void fifo2_flush(struct fifo2 *fifo) {
    ASSERT_FIFO_LOCKED(fifo);

    fifo->sptr = 0;
    fifo->rptr = 0;
    fifo->wptr = 0;
}

static size_t fifo2_bytes_to_ptr_wrap(struct fifo2 *fifo, size_t ptr) {
    return (fifo->size - ptr);
}

static size_t fifo2_inc_ptr(struct fifo2 *fifo, size_t ptr, size_t bytes) {
    ptr += bytes;
    if (ptr >= fifo->size)
        ptr -= fifo->size;
    return ptr;
}

static void fifo2_copy_out(struct fifo2 *fifo, size_t ptr, uint8_t *buf,
    size_t size) {
    size_t contb;

    if (!buf) {
        return;
    }

    contb = fifo2_bytes_to_ptr_wrap(fifo, ptr);
    if (contb >= size) {
        memcpy(buf, fifo->base + ptr, size);
    } else {
        memcpy(buf, fifo->base + ptr, contb);
        memcpy(buf + contb, fifo->base, size - contb);
    }
}

size_t fifo2_enq(struct fifo2 *fifo, const uint8_t *buf, size_t size) {
    size_t contb;

    ASSERT_FIFO_LOCKED(fifo);
    if (fifo2_bytes_free(fifo) < size)
        return 0;

    contb = fifo2_bytes_until_wptr_wrap(fifo);
    if (contb >= size) {
        memcpy(fifo->base + fifo->wptr, buf, size);
        fifo2_wptr_incby(fifo, size);
    } else {
        memcpy(fifo->base + fifo->wptr, buf, contb);
        memcpy(fifo->base, buf + contb, size - contb);
        fifo2_wptr_incby(fifo, contb);
        fifo2_wptr_incby(fifo, size - contb);
    }
    return size;
}

size_t fifo2_deq(struct fifo2 *fifo, uint8_t *buf, size_t size, bool auto_discard) {
    ASSERT_FIFO_LOCKED(fifo);
    if (fifo2_bytes_used(fifo, false) < size) {
        return 0;
    }

    fifo2_copy_out(fifo, fifo->rptr, buf, size);
    fifo->rptr = fifo2_inc_ptr(fifo, fifo->rptr, size);
    if (auto_discard) {
        fifo->sptr = fifo->rptr;
    }
    return size;
}

size_t fifo2_read(struct fifo2 *fifo, uint8_t *buf, size_t offset,
    size_t size, bool stale) {
    ASSERT_FIFO_LOCKED(fifo);
    if (fifo2_bytes_used(fifo, stale) < (size + offset)) {
        return 0;
    }

    offset = fifo2_inc_ptr(fifo, stale ? fifo->sptr : fifo->rptr, offset);
    fifo2_copy_out(fifo, offset, buf, size);
    return size;
}

size_t fifo2_discard(struct fifo2 *fifo, size_t size) {
    size_t diff;

    ASSERT_FIFO_LOCKED(fifo);

    // Used stale data should always be greater than or equal to used fresh
    // data.
    diff = fifo2_bytes_used(fifo, true) - fifo2_bytes_used(fifo, false);
    if (diff <= size) {
        fifo->sptr = fifo->rptr;
        return diff;
    }

    fifo->sptr = fifo2_inc_ptr(fifo, fifo->sptr, size);
    return size;
}

int fifo2_lock(struct fifo2 *fifo) {
    int r;

#ifdef DEBUG_FIFO
    if (DEBUG_FIFO > 0 ) {
        printf(">> LOCK %p\n", fifo);
        if (DEBUG_FIFO > 1) {
            print_trace();
        }
    }
#endif

    r = pthread_mutex_lock(&fifo->mutex);

    fifo->lock++;
    return r;
}

int fifo2_unlock(struct fifo2 *fifo) {
    ASSERT_FIFO_LOCKED(fifo);

#ifdef DEBUG_FIFO
    if (DEBUG_FIFO > 0) {
        printf("<< UNLOCK %p\n", fifo);
    }
#endif

    fifo->lock--;
    /* linux multi-process locking */
    return pthread_mutex_unlock(&fifo->mutex);
}

int fifo2_signal(struct fifo2 *fifo) {
    ASSERT_FIFO_LOCKED(fifo);

    /* linux multi-process locking */
    return pthread_cond_signal(&fifo->cond);
}

int fifo2_wait_timeout(struct fifo2 *fifo, uint32_t ms) {
    struct timeval delta;
    struct timespec abstime;
    int r;

    ASSERT_FIFO_LOCKED(fifo);
    fifo->lock--;

    /* linux multi-process locking */
    gettimeofday(&delta, NULL);

    abstime.tv_sec = delta.tv_sec + (ms/1000);
    abstime.tv_nsec = (delta.tv_usec + (ms%1000) * 1000) * 1000;
        if ( abstime.tv_nsec > 1000000000 ) {
        abstime.tv_sec += 1;
        abstime.tv_nsec -= 1000000000;
        }

    do {
        r = pthread_cond_timedwait(&fifo->cond, &fifo->mutex, &abstime);
    } while (r == EINTR);

    fifo->lock++;
    return r;
}
