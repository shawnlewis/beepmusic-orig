/*	$NetBSD: i2sio.h,v 1 2007/03/29 14:46:24 jacob Exp $	*/

/* ioctl numbers for i2s driver */

#include <stdint.h>

#define NUM_DESC        4
#define I2S_BUF_SIZE    192

struct i2s_sync {
    struct timeval tv;
    uint32_t samples;
    uint32_t buffered_samples;
};

#define I2S_VOLUME      _IOW('N', 0x20, int)
#define I2S_FREQ        _IOW('N', 0x21, int)
#define I2S_DSIZE       _IOW('N', 0x22, int)
#define I2S_MODE        _IOW('N', 0x23, int)
#define I2S_FINE        _IOW('N', 0x24, int)
#define I2S_COUNT       _IOWR('N', 0x25, int)
#define I2S_PAUSE       _IOWR('N', 0x26, int)
#define I2S_RESUME      _IOWR('N', 0x27, int)
#define I2S_MCLK        _IOW('N', 0x28, int)
#define I2S_CLEAR_OUT_SAMPLE_COUNT _IOWR('N', 0x29, uint32_t*)
#define I2S_GET_SYNC    _IOWR('N', 0x2a, struct i2s_sync*)
