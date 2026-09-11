#include <stdlib.h>
#include <string.h>

#include "audio/fifo2.h"
#include "audio/streambuf.h"
#include "audio/decode/decode.h"
#include "audio/decode/decode_priv.h"

#define MAX_STREAM_CMDS 8

static uint8_t streambuf_data[STREAMBUF_SIZE];
static uint32_t streambuf_slot_cnt[MAX_STREAM_CMDS + 1];
static uint32_t streambuf_cmd_cnt = 0;
static uint32_t eslot = 0;
static uint32_t dslot = 0;
static struct fifo2 streambuf_fifo;
static bool streambuf_streaming = false;
static bool streambuf_allow_auto_discard = false;
static bool streambuf_allow_discard = true;
static bool streambuf_allow_writes = true;  // reports free space as 0.
static uint64_t streambuf_bytes_received = 0;

// Used to track stale state (read but not discarded).
static size_t discard_data_on_thaw = 0;
static size_t discard_cmd_on_thaw = 0;
static uint32_t streambuf_cmd_cnt_stale = 0;
static uint32_t sslot = 0;
// Begin sync data.
static uint32_t streambuf_slot_cnt_stale[MAX_STREAM_CMDS + 1];
// streambuf_cmd_cnt_stale, eslot, sslot, streambuf_data used data.
static uint32_t streambuf_state_sync[4];
// End sync data.

// Streambuf is owned by audio decode.
extern LOG_CATEGORY *log_audio_decode;

size_t streambuf_get_size(void) {
    return STREAMBUF_SIZE;
}

size_t streambuf_get_freebytes(void) {
    size_t n;

    fifo2_lock(&streambuf_fifo);

    if (streambuf_allow_writes == true) {
        n = fifo2_bytes_free(&streambuf_fifo);
    } else {
        n = 0;
    }

    fifo2_unlock(&streambuf_fifo);

    return n;
}

size_t streambuf_get_usedbytes(void) {
    size_t n;

    fifo2_lock(&streambuf_fifo);

    n = fifo2_bytes_used(&streambuf_fifo, false);

    fifo2_unlock(&streambuf_fifo);

    return n;
}

size_t streambuf_fast_usedbytes(void) {
    ASSERT_FIFO_LOCKED(&streambuf_fifo);

    return fifo2_bytes_used(&streambuf_fifo, false);
}

/* returns true if the stream is still open but cannot yet supply the requested bytes */
bool streambuf_would_wait_for(size_t bytes) {
    if (!streambuf_streaming) {
        return false;
    }
    return streambuf_get_data_usedbytes() < bytes;
}

void streambuf_get_status(size_t *size, size_t *usedbytes, uint32_t *bytesL, uint32_t *bytesH) {
    fifo2_lock(&streambuf_fifo);

    *size = STREAMBUF_SIZE;
    // Report streambuf is full to prevent buffer/writes.
    if (streambuf_allow_writes == true) {
        *usedbytes = fifo2_bytes_used(&streambuf_fifo, true);
    } else {
        *usedbytes = STREAMBUF_SIZE;
    }
    *bytesL = streambuf_bytes_received & 0xFFFFFFFF;
    *bytesH = streambuf_bytes_received >> 32;

    fifo2_unlock(&streambuf_fifo);
}

void streambuf_flush(void) {
    fifo2_lock(&streambuf_fifo);

    fifo2_flush(&streambuf_fifo);
    //streambuf_streaming = false;
    streambuf_allow_auto_discard = false;
    streambuf_allow_discard = true;
    discard_data_on_thaw = 0;
    discard_cmd_on_thaw = 0;
    streambuf_cmd_cnt = 0;
    streambuf_cmd_cnt_stale = 0;
    eslot = 0;
    dslot = 0;
    sslot = 0;
    memset(streambuf_slot_cnt, 0, sizeof(streambuf_slot_cnt));
    memset(streambuf_slot_cnt_stale, 0, sizeof(streambuf_slot_cnt_stale));

    fifo2_unlock(&streambuf_fifo);
}

void streambuf_feed(uint8_t *buf, size_t size) {
    if (streambuf_allow_writes == false) {
        assert(0);
    }

    streambuf_data_enq(buf, size);
}

size_t streambuf_read(uint8_t *buf, size_t min, size_t max, bool *streaming) {
    return streambuf_data_deq(buf, min, max, streaming);
}

void streambuf_set_streaming(bool is_streaming) {
    streambuf_streaming = is_streaming;
}

bool streambuf_is_streaming(void) {
    return streambuf_streaming;
}

void streambuf_init(void) {
    fifo2_init(&streambuf_fifo, streambuf_data, STREAMBUF_SIZE, false);
}

static size_t streambuf_dec_cnt_stale(size_t size) {
    size_t bytes_left = size;

    ASSERT_FIFO_LOCKED(&streambuf_fifo);

    do {
        if (bytes_left >= streambuf_slot_cnt_stale[sslot]) {
            bytes_left -= streambuf_slot_cnt_stale[sslot];
            streambuf_slot_cnt_stale[sslot] = 0;
            // With current design inline cmds don't take up bytes in slot cnt
            // so they will always get discarded from the stale counters.
            while ((sslot != eslot) &&
                (streambuf_slot_cnt_stale[sslot] == 0)) {
                sslot = (sslot == MAX_STREAM_CMDS) ? 0 : sslot + 1;
                streambuf_cmd_cnt_stale--;
            }
        } else {
            streambuf_slot_cnt_stale[sslot] -= bytes_left;
            bytes_left = 0;
        }
    } while(bytes_left);

    // This function shouldn't be called with more bytes than is tracked
    // in the stale counters so always return requested dec size.
    return size;
}

bool streambuf_data_enq(uint8_t *buf, size_t size) {
    fifo2_lock(&streambuf_fifo);

    streambuf_streaming = true;
    streambuf_bytes_received += size;
    streambuf_slot_cnt[eslot] += size;
    streambuf_slot_cnt_stale[eslot] += size;
    fifo2_enq(&streambuf_fifo, buf, size);

    fifo2_unlock(&streambuf_fifo);

    return true;
}

size_t streambuf_data_deq(uint8_t *buf, size_t min, size_t max, bool *streaming) {
    size_t size;

    fifo2_lock(&streambuf_fifo);

    size = streambuf_slot_cnt[dslot];
    if (size < min)
        size = 0;
    size = (size > max) ? max : size;

    if (streaming) {
        *streaming = streambuf_streaming;
    }

    if (size) {
        fifo2_deq(&streambuf_fifo, buf, size, streambuf_allow_auto_discard);
        streambuf_slot_cnt[dslot] -= size;
    }

    fifo2_signal(&streambuf_fifo);

    fifo2_unlock(&streambuf_fifo);

    return size;
}

uint32_t streambuf_cmd_ready(void) {
    uint32_t code = 0;

    fifo2_lock(&streambuf_fifo);

    if (streambuf_cmd_cnt && !streambuf_slot_cnt[dslot]) {
        fifo2_read(&streambuf_fifo, (uint8_t *)&code, 0, sizeof(uint32_t),
            false);
    }

    fifo2_unlock(&streambuf_fifo);

    return code;
}

bool streambuf_cmd_enq(uint8_t *buf, size_t size) {
    bool ret = false;

    fifo2_lock(&streambuf_fifo);

    if ((streambuf_cmd_cnt_stale != MAX_STREAM_CMDS) &&
        (size < fifo2_bytes_free(&streambuf_fifo))) {
        fifo2_enq(&streambuf_fifo, buf, size);
        eslot = (eslot == MAX_STREAM_CMDS) ? 0 : eslot + 1;
        streambuf_cmd_cnt++;
        streambuf_cmd_cnt_stale++;
        ret = true;
    }

    fifo2_unlock(&streambuf_fifo);

    if (!ret) {
        LOG_ERROR(log_audio_decode, "could not enq cmd_size: %zu", size);
        exit(2);
    }

    return ret;
}

size_t streambuf_cmd_deq(uint8_t *buf, size_t size) {
    size_t deqed = 0;

    fifo2_lock(&streambuf_fifo);

    if (streambuf_cmd_cnt && !streambuf_slot_cnt[dslot]) {
        deqed = fifo2_deq(&streambuf_fifo, buf, size,
            streambuf_allow_auto_discard);
        if (deqed) {
            dslot = (dslot == MAX_STREAM_CMDS) ? 0 : dslot + 1;
            streambuf_cmd_cnt--;
        }
    }

    fifo2_unlock(&streambuf_fifo);

    return deqed;
}

size_t streambuf_discard(size_t size, bool data) {
    size_t discarded = 0;

    fifo2_lock(&streambuf_fifo);

    if (streambuf_allow_discard) {
        discarded = fifo2_discard(&streambuf_fifo, size);
        if (data) {
            streambuf_dec_cnt_stale(discarded);
        } else {
            // This will remove commands from the stale counters at the
            // correct time.
            streambuf_dec_cnt_stale(0);
        }
    } else {
        // Find out how much we can discard, this allows the caller mark bytes
        // it doesn't care about before they've been read.  This result should
        // never be negative so continue using unsigned.
        discarded = fifo2_bytes_used(&streambuf_fifo, true) -
            fifo2_bytes_used(&streambuf_fifo, false) -
            discard_data_on_thaw - discard_cmd_on_thaw;
        discarded = (discarded < size) ? discarded : size;
        if (data) {
            discard_data_on_thaw += discarded;
        } else {
            discard_cmd_on_thaw += discarded;
        }
    }

    fifo2_unlock(&streambuf_fifo);

    return discarded;
}

size_t streambuf_get_data_usedbytes(void) {
    size_t size;

    fifo2_lock(&streambuf_fifo);

    size = streambuf_slot_cnt[dslot];

    fifo2_unlock(&streambuf_fifo);

    return size;
}

void streambuf_fifo_debug(size_t *stale_bytes, size_t *fresh_bytes,
    size_t *free_bytes, size_t *sptr, size_t *rptr, size_t *wptr) {
    fifo2_lock(&streambuf_fifo);

    if (stale_bytes)
        *stale_bytes = fifo2_bytes_used(&streambuf_fifo, true);
    if (fresh_bytes)
        *fresh_bytes = fifo2_bytes_used(&streambuf_fifo, false);
    if (free_bytes)
        *free_bytes = fifo2_bytes_free(&streambuf_fifo);
    if (sptr)
        *sptr = streambuf_fifo.sptr;
    if (rptr)
        *rptr = streambuf_fifo.rptr;
    if (wptr)
        *wptr = streambuf_fifo.wptr;

    fifo2_unlock(&streambuf_fifo);
}

// right now streambuf cannot produce errors up to audio.
void streambuf_lock_state(bool lock) {
    fifo2_lock(&streambuf_fifo);

    if (lock) {
        // Since we can't stop decoder from reading out of streambuf need
        // to copy state and disable discarding any data from fifo.  Writes
        // need to be disabled in audio/player code.
        streambuf_allow_auto_discard = false;
        streambuf_allow_discard = false;
        streambuf_allow_writes = false;
        streambuf_state_sync[0] = streambuf_cmd_cnt_stale;
        streambuf_state_sync[1] = eslot;
        streambuf_state_sync[2] = sslot;
        streambuf_state_sync[3] = fifo2_bytes_used(&streambuf_fifo, true);
        LOG_TRACE(log_audio_decode, "streambuf_slot_cnt: %d %d %d %d %d %d %d %d %d",
                streambuf_slot_cnt[0],
                streambuf_slot_cnt[1],
                streambuf_slot_cnt[2],
                streambuf_slot_cnt[3],
                streambuf_slot_cnt[4],
                streambuf_slot_cnt[5],
                streambuf_slot_cnt[6],
                streambuf_slot_cnt[7],
                streambuf_slot_cnt[8]);
        LOG_TRACE(log_audio_decode, "streambuf_slot_cnt_stale: %d %d %d %d %d %d %d %d %d",
                streambuf_slot_cnt_stale[0],
                streambuf_slot_cnt_stale[1],
                streambuf_slot_cnt_stale[2],
                streambuf_slot_cnt_stale[3],
                streambuf_slot_cnt_stale[4],
                streambuf_slot_cnt_stale[5],
                streambuf_slot_cnt_stale[6],
                streambuf_slot_cnt_stale[7],
                streambuf_slot_cnt_stale[8]);
        LOG_TRACE(log_audio_decode, "sslot: %d dslot: %d eslot: %d streambuf_cmd_cnt: %d streambuf_cmd_cnt_stale: %d",
                sslot,
                dslot,
                eslot,
                streambuf_cmd_cnt,
                streambuf_cmd_cnt_stale);
        LOG_TRACE(log_audio_decode, "data_on_thaw: %zu cmd_on_thaw: %zu",
                discard_data_on_thaw,
                discard_cmd_on_thaw);
    } else {  // thaw
        streambuf_allow_auto_discard = false;
        streambuf_allow_discard = true;
        streambuf_allow_writes = true;
        // replay discard commands during freeze.
        streambuf_dec_cnt_stale(discard_data_on_thaw);
        fifo2_discard(&streambuf_fifo, discard_data_on_thaw + discard_cmd_on_thaw);
        discard_data_on_thaw = 0;
        discard_cmd_on_thaw = 0;
    }

    fifo2_unlock(&streambuf_fifo);
}

void streambuf_restore_state(void) {
    fifo2_lock(&streambuf_fifo);

    LOG_TRACE(log_audio_decode, "streambuf_slot_cnt_stale: %d %d %d %d %d %d %d %d %d",
            streambuf_slot_cnt_stale[0],
            streambuf_slot_cnt_stale[1],
            streambuf_slot_cnt_stale[2],
            streambuf_slot_cnt_stale[3],
            streambuf_slot_cnt_stale[4],
            streambuf_slot_cnt_stale[5],
            streambuf_slot_cnt_stale[6],
            streambuf_slot_cnt_stale[7],
            streambuf_slot_cnt_stale[8]);
    LOG_TRACE(log_audio_decode, "streambuf_state_sync: %d %d %d %d",
            streambuf_state_sync[0],
            streambuf_state_sync[1],
            streambuf_state_sync[2],
            streambuf_state_sync[3]);
    // This has to be done atomically and after there is enough data
    // in streambuf to not try and pop a command that isn't there.
    memcpy(streambuf_slot_cnt, streambuf_slot_cnt_stale,
        sizeof(streambuf_slot_cnt));
    streambuf_cmd_cnt = streambuf_state_sync[0];
    streambuf_cmd_cnt_stale = streambuf_state_sync[0];
    eslot = streambuf_state_sync[1];
    // dslot and sslot will be same on restore.
    dslot = streambuf_state_sync[2];
    sslot = streambuf_state_sync[2];

    fifo2_unlock(&streambuf_fifo);
}

size_t streambuf_sync_read_state(uint8_t *buf, size_t size) {
    size_t ret = sizeof(streambuf_slot_cnt_stale) + sizeof(streambuf_state_sync);

    fifo2_lock(&streambuf_fifo);

    if (ret <= size) {
        memcpy(buf, streambuf_slot_cnt_stale,
            sizeof(streambuf_slot_cnt_stale));
        memcpy(buf + sizeof(streambuf_slot_cnt_stale), streambuf_state_sync,
            sizeof(streambuf_state_sync));
    } else {
        ret = 0;
    }
    fifo2_unlock(&streambuf_fifo);

    return ret;
}

size_t streambuf_sync_read_data(uint8_t *buf, size_t offset, size_t size) {
    size_t bytes_read;

    fifo2_lock(&streambuf_fifo);

    // fifo2_read is all or nothing so limit how much we can read.
    bytes_read = fifo2_bytes_used(&streambuf_fifo, true);
    if (bytes_read < (size + offset)) {
        size = bytes_read - offset;
    }
    if (size) {
        bytes_read = fifo2_read(&streambuf_fifo, buf, offset, size, true);
    } else {
        bytes_read = 0;
    }

    fifo2_unlock(&streambuf_fifo);

    return bytes_read;
}

size_t streambuf_sync_write_state(uint8_t *buf, size_t size) {
    size_t ret = 0;
    // Reset state information.  This will also reset the fifo.  This will lock
    // and unlock streambuf so do here before we lock for this function.
    streambuf_flush();

    fifo2_lock(&streambuf_fifo);

    // This should have been sent as a single packet so only accept the
    // correct size.
    if (size == (sizeof(streambuf_slot_cnt_stale) +
        sizeof(streambuf_state_sync))) {
        memcpy(streambuf_slot_cnt_stale, buf, sizeof(streambuf_slot_cnt_stale));
        memcpy(streambuf_state_sync, buf + sizeof(streambuf_slot_cnt_stale),
            sizeof(streambuf_state_sync));
        ret = streambuf_state_sync[3];
    }

    fifo2_unlock(&streambuf_fifo);

    return ret;
}

size_t streambuf_sync_write_data(uint8_t *buf, size_t size) {
    fifo2_lock(&streambuf_fifo);

    // fifo2_enq is all or nothing so this will return 0 if out of space.
    size = fifo2_enq(&streambuf_fifo, buf, size);

    fifo2_unlock(&streambuf_fifo);

    return size;
}

// Figure out if these are still needed.  There is a flag in mp3 frames we can
// check from mad MAD_FLAG_COPYRIGHT but right now we don't look at it.
bool streambuf_is_copyright(void) {
    return false;
}

bool streambuf_is_icy(void) {
    return false;
}

