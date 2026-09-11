/*
** Copyright 2007-2008 Logitech. All Rights Reserved.
**
** This file is licensed under BSD. Please see the LICENSE file for details.
*/

#include "audio/fifo.h"


struct mqueue {
    char *buffer;
    struct fifo fifo;
};

typedef void (*mqueue_func_t)(bool swap);

extern void mqueue_init(struct mqueue *mqueue, void *buffer, size_t buffer_size);

extern uint32_t mqueue_read_request(struct mqueue *mqueue, uint32_t timeout);

extern uint8_t mqueue_read_u8(struct mqueue *mqueue);
extern uint16_t mqueue_read_u16(struct mqueue *mqueue);
extern uint32_t mqueue_read_u32(struct mqueue *mqueue);
uint64_t mqueue_read_u64(struct mqueue *mqueue);
extern void mqueue_read_array(struct mqueue *mqueue, uint8_t *array, size_t len);
extern void mqueue_read_complete(struct mqueue *mqueue);

extern int mqueue_write_request(struct mqueue *mqueue, uint32_t code, size_t len);
extern void mqueue_write_u8(struct mqueue *mqueue, uint8_t val);
extern void mqueue_write_u16(struct mqueue *mqueue, uint16_t val);
extern void mqueue_write_u32(struct mqueue *mqueue, uint32_t val);
extern void mqueue_write_u64(struct mqueue *mqueue, uint64_t val);
extern void mqueue_write_array(struct mqueue *mqueue, uint8_t *array, size_t len);
extern void mqueue_write_complete(struct mqueue *mqueue);

