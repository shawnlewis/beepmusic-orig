#ifndef STREAMBUF_H
#define STREAMBUF_H

#include <stdbool.h>
#include <sys/types.h>
#include <stdint.h>

#define STREAMBUF_SIZE (1 * 1024 * 1024)

// Old style methods.
size_t streambuf_get_size(void);
size_t streambuf_get_freebytes(void);
size_t streambuf_get_usedbytes(void);
size_t streambuf_fast_usedbytes(void);
bool streambuf_would_wait_for(size_t bytes);
void streambuf_get_status(size_t *size, size_t *usedbytes, uint32_t *bytesL, uint32_t *bytesH);
void streambuf_flush(void);
void streambuf_feed(uint8_t *buf, size_t size);
size_t streambuf_read(uint8_t *buf, size_t min, size_t max, bool *streaming);
void streambuf_set_streaming(bool is_streaming);
bool streambuf_is_streaming(void);
void streambuf_init(void);

// New functions.
bool streambuf_data_enq(uint8_t *buf, size_t size);
size_t streambuf_data_deq(uint8_t *buf, size_t min, size_t max, bool *streaming);
uint32_t streambuf_cmd_ready(void);
bool streambuf_cmd_enq(uint8_t *buf, size_t size);
size_t streambuf_cmd_deq(uint8_t *buf, size_t size);
size_t streambuf_discard(size_t size, bool data);
size_t streambuf_get_data_usedbytes(void);
void streambuf_fifo_debug(size_t *stale_bytes, size_t *fresh_bytes,
    size_t *free_bytes, size_t *sptr, size_t *rptr, size_t *wptr);
void streambuf_lock_state(bool lock);
void streambuf_restore_state(void);
size_t streambuf_sync_read_state(uint8_t *buf, size_t size);
size_t streambuf_sync_read_data(uint8_t *buf, size_t offset, size_t size);
size_t streambuf_sync_write_state(uint8_t *buf, size_t size);
size_t streambuf_sync_write_data(uint8_t *buf, size_t size);

// Figure out if these are still needed.
bool streambuf_is_copyright(void);
bool streambuf_is_icy(void);

#endif // STREAMBUF_H
