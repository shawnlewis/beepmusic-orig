#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

struct i2s_sync {
    struct timeval tv;
    uint32_t samples;
    uint32_t buffered_samples;
};
#define I2S_FREQ        _IOW('N', 0x21, int)
#define I2S_DSIZE       _IOW('N', 0x22, int)
#define I2S_GET_SYNC    _IOWR('N', 0x2a, struct i2s_sync*)

int i2s_fd = -1;

void i2s_close(void) {
    close(i2s_fd);
}

void i2s_open(void) {
    i2s_fd = open("/dev/i2s", O_WRONLY);

    if (i2s_fd < 0) {
        fprintf(stderr, "Couldn\'t open i2s device\n");
        exit(1);
    }

    if (ioctl(i2s_fd, I2S_DSIZE, 16) < 0) {
        fprintf(stderr, "Couldn\'t set ioctl I2S_DSIZE failed\n");
        exit(1);
    }

    if (ioctl(i2s_fd, I2S_FREQ, 44100) < 0) {
        fprintf(stderr, "Couldn\'t set ioctl I2S_FREQ failed\n");
        exit(1);
    }
}


void i2s_write(const char* buf, size_t len) {
    int ret;
    do  {
        ret = write(i2s_fd, buf, len);
    } while (ret == -ERESTART);
    if (ret != len) {
        fprintf(stderr, "write returned unexpected value\n");
    }

    struct i2s_sync i2s_sync;
    if (ioctl(i2s_fd, I2S_GET_SYNC, &i2s_sync) < 0) {
        fprintf(stderr, "Couldn\'t fetch sync info from driver.");
        exit(1);
    }
}

void gen_noise(char* buf, size_t buf_len) {
    for (int i=0; i<buf_len; i+=4) {
        int sample = random();
        memcpy(&buf[i], &sample, 4);
    }
}

#define BUF_LEN 768

char buf[BUF_LEN];  // 768 / 4 = 192 samples

int main(int argc, char** argv) {
    while (1) {
        i2s_open();

        int seconds = random() % 5;
        fprintf(stderr, "writing %d seconds of audio\n", seconds);
        for (int i=0; i < 229 * seconds; i++) {
            gen_noise(buf, BUF_LEN);
            i2s_write(buf, BUF_LEN);
        }

        i2s_close();
    }
}
