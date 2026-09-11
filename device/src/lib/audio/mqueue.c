/*
** Copyright 2007-2008 Logitech. All Rights Reserved.
**
** This file is licensed under BSD. Please see the LICENSE file for details.
*/

#include <errno.h>
#include <string.h>

#include "audio/fifo.h"
#include "audio/mqueue.h"
#include "audio/decode/decode_priv.h"


void mqueue_init(struct mqueue *mqueue, void *buffer, size_t buffer_size) {
    fifo_init(&mqueue->fifo, buffer_size, false);
    mqueue->buffer = buffer;
}


static void mqueue_read_buf(struct mqueue *mqueue, uint8_t *b, size_t n) {
    size_t bytes_read;

    while (n) {
        bytes_read = fifo_bytes_until_rptr_wrap(&mqueue->fifo);
        if (n < bytes_read) {
            bytes_read = n;
        }

        memcpy(b, mqueue->buffer + mqueue->fifo.rptr, bytes_read);
        fifo_rptr_incby(&mqueue->fifo, bytes_read);

        b += bytes_read;
        n -= bytes_read;
    }
}


uint32_t mqueue_read_request(struct mqueue *mqueue, uint32_t timeout) {
    int err;

    if (fifo_lock(&mqueue->fifo) == -1) {
        LOG_ERROR(log_audio_decode, "Failed to lock mutex %s", strerror(errno));
        return 0;
    }

    /* Any queued messages? */
    if (fifo_bytes_used(&mqueue->fifo)) {
        uint32_t code;
        mqueue_read_buf(mqueue, (uint8_t *)&code, sizeof(code));

        /* Mutex remains locked until mqueue_read_complete */
        return code;
    }

    if (!timeout) {
        fifo_unlock(&mqueue->fifo);
        return 0;
    }

    /* Wait until timeout */
    err = fifo_wait_timeout(&mqueue->fifo, timeout);
    if (err == ETIMEDOUT) {
        fifo_unlock(&mqueue->fifo);
        return 0;
    } else if (err != 0) {
        LOG_ERROR(log_audio_decode, "Failed to wait on condition. ret: %d sterrror: %s", err, strerror(errno));

        fifo_unlock(&mqueue->fifo);
        return 0;
    } else {
        // We were signaled to wakeup. Check if there's actually a command.
        if (fifo_bytes_used(&mqueue->fifo)) {
            uint32_t code;
            mqueue_read_buf(mqueue, (uint8_t *)&code, sizeof(code));

            /* Mutex remains locked until mqueue_read_complete */
            return code;
        }

        fifo_unlock(&mqueue->fifo);
        return 0;
    }
}


void mqueue_read_complete(struct mqueue *mqueue) {
    /* Unlock mutex */
    fifo_unlock(&mqueue->fifo);
}


uint8_t mqueue_read_u8(struct mqueue *mqueue) {
    uint8_t v;
    mqueue_read_buf(mqueue, (uint8_t *)&v, sizeof(v));
    return v;
}


uint16_t mqueue_read_u16(struct mqueue *mqueue) {
    uint16_t v;
    mqueue_read_buf(mqueue, (uint8_t *)&v, sizeof(v));
    return v;
}


uint32_t mqueue_read_u32(struct mqueue *mqueue) {
    uint32_t v;
    mqueue_read_buf(mqueue, (uint8_t *)&v, sizeof(v));
    return v;
}

uint64_t mqueue_read_u64(struct mqueue *mqueue) {
    uint64_t v;
    mqueue_read_buf(mqueue, (uint8_t *)&v, sizeof(v));
    return v;
}


void mqueue_read_array(struct mqueue *mqueue, uint8_t *array, size_t len)
{
    mqueue_read_buf(mqueue, array, len);
}


static void mqueue_write_buf(struct mqueue *mqueue, uint8_t *b, size_t n) {
    size_t bytes_write;

    while (n) {
        bytes_write = fifo_bytes_until_wptr_wrap(&mqueue->fifo);
        if (n < bytes_write) {
            bytes_write = n;
        }

        memcpy(mqueue->buffer + mqueue->fifo.wptr, b, bytes_write);
        fifo_wptr_incby(&mqueue->fifo, bytes_write);

        b += bytes_write;
        n -= bytes_write;
    }
}


int mqueue_write_request(struct mqueue *mqueue, uint32_t code, size_t len) {
    if (fifo_lock(&mqueue->fifo) == -1) {
        LOG_ERROR(log_audio_decode, "Failed to lock mutex %s", strerror(errno));
        return 0;
    }

    /* Check there is enough room in the mqueue */
    if (len > fifo_bytes_free(&mqueue->fifo)) {
        fifo_unlock(&mqueue->fifo);
        return 0;
    }

    /* Write handler function */
    mqueue_write_buf(mqueue, (uint8_t *)&code, sizeof(code));

    /* Mutex remains locked until mqueue_write_complete */
    return 1;
}


void mqueue_write_complete(struct mqueue *mqueue) {
    /* Signal reader and unlock mutex */
    fifo_signal(&mqueue->fifo);
    fifo_unlock(&mqueue->fifo);
}


void mqueue_write_u8(struct mqueue *mqueue, uint8_t val) {
    mqueue_write_buf(mqueue, (uint8_t *)&val, sizeof(val));
}


void mqueue_write_u16(struct mqueue *mqueue, uint16_t val) {
    mqueue_write_buf(mqueue, (uint8_t *)&val, sizeof(val));
}


void mqueue_write_u32(struct mqueue *mqueue, uint32_t val) {
    mqueue_write_buf(mqueue, (uint8_t *)&val, sizeof(val));
}

void mqueue_write_u64(struct mqueue *mqueue, uint64_t val) {
    mqueue_write_buf(mqueue, (uint8_t *)&val, sizeof(val));
}

void mqueue_write_array(struct mqueue *mqueue, uint8_t *array, size_t len)
{
    mqueue_write_buf(mqueue, array, len);
}

