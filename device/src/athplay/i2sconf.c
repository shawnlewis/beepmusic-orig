/*
 * =====================================================================================
 *
 *       Filename:  i2sconf.c
 *
 *    Description:  Application to control i2s device
 *
 *        Version:  1.0
 *        Created:  Tuesday 23 March 2010 04:20:46  IST
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (),
 *        Company:
 *
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>

#include "i2sio.h"

char *audev = "/dev/i2s";


void usage(void)
{
    printf("i2sconf -hmM\n");
    printf("-h : display help message\n");
    printf("-p: pause dma\n");
    printf("-r: resume dma\n");
    printf("-c: clear counters\n");
    printf("-i: dump info\n");
    printf("-r <1/0> : Enable/Disable external master clock\n");
    printf("-m <1/0> : Enable/Disable external master clock\n");
    printf("-f <44100/48000> : Sampling frequency\n");
}

int main (int argc, char *argv[])
{
    int audio;
    int mclk_sel = 0;
    int freq = 0;

    int ret;
    int	optc;		/* For getopt */

    audio = open(audev, O_WRONLY);

	if (audio < 0) {
		exit (-1);
	}

    int num_opts = 0;
    struct i2s_sync i2s_sync;

	while ((optc = getopt(argc, argv, "hHprcim:f:")) != -1) {
        num_opts++;
		switch (optc) {
            case 'p':
                if (ioctl(audio, I2S_PAUSE, 0) < 0) {
                    perror("I2S_PAUSE");
                }
                break;
            case 'r':
                if (ioctl(audio, I2S_RESUME, 0) < 0) {
                    perror("I2S_RESUME");
                }
                break;
            case 'c':
                if (ioctl(audio, I2S_CLEAR_OUT_SAMPLE_COUNT, NULL) < 0) {
                    perror("I2S_CLEAR_OUT_SAMPLE_COUNT");
                }
                break;
            case 'i':
                if (ioctl(audio, I2S_GET_SYNC, &i2s_sync) < 0) {
                    perror("I2S_GET_SYNC");
                }
                printf("Buffered samples: %u\n", i2s_sync.buffered_samples);
                printf("Samples: %u\n", i2s_sync.buffered_samples);
                printf("Time: %u %u\n",
                        (uint32_t) i2s_sync.tv.tv_sec,
                        (uint32_t) i2s_sync.tv.tv_usec);
                break;
            case 'm':
                mclk_sel = atoi(optarg)?1:0;
                if (ioctl(audio, I2S_MCLK, mclk_sel) < 0) {
                    perror("I2S_MCLK");
                }
                break;
            case 'f':
                freq = atoi(optarg);
                if (ioctl(audio, I2S_FREQ, freq) < 0) {
                    perror("I2S_FREQ");
                }
                break;
            case 'h':
            case 'H':
                usage();
                break;
			default:
                break;
		}
	}
    if (!num_opts) {
        usage();
    }

rep:
	ret = close(audio);
	if (ret == EAGAIN) {
	    goto rep;
	}
	return 0;
}
