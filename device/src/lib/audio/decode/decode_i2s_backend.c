/*
** Copyright 2007-2008 Logitech. All Rights Reserved.
**
** This file is licensed under BSD. Please see the LICENSE file for details.
*/

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* for real-time behaviour */
#include <malloc.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/shm.h>
#include <sys/utsname.h>

#include "audio/fifo.h"
#include "audio/fixed_math.h"
#include "audio/mqueue.h"
#include "audio/decode/decode.h"
#include "audio/decode/decode_priv.h"

#include "beep/log.h"
#include "beep/debug.h"

int NO_AUDIO = 0;


/* ioctls */
struct i2s_sync {
    struct timeval tv;
    uint32_t samples;
    uint32_t buffered_samples;
};
#define I2S_FREQ        _IOW('N', 0x21, int)
#define I2S_DSIZE       _IOW('N', 0x22, int)
#define I2S_GET_SYNC    _IOWR('N', 0x2a, struct i2s_sync*)

typedef uint32_t frame_t;
typedef int32_t sample_t;


/* debug switches */
#define TEST_LATENCY 0
#define DEBUG_PAGEFAULTS 0


uint8_t *decode_fifo_buf;
uint8_t *effect_fifo_buf;
struct decode_audio *decode_audio;

#define FLAG_STREAM_PLAYBACK 0x01
#define FLAG_STREAM_EFFECTS  0x02
#define FLAG_STREAM_NOISE    0x04
#define FLAG_STREAM_LOOPBACK 0x08


struct decode_i2s {
    /* device configuration */
    uint32_t flags;
    unsigned int buffer_time;
    unsigned int period_count;

    /* i2s state */
    int i2s_fd;
    uint32_t period_size;

    /* playback state */
    uint32_t pcm_sample_rate;

    /* parent */
    pid_t parent_pid;
};

#define PCM_FRAMES_TO_BYTES(frames) (frames * sizeof(frame_t))
#define PCM_BYTES_TO_FRAMES(bytes) (bytes / sizeof(frame_t))


/* alsa debugging */
//static snd_output_t *output;

/* player state */
static struct decode_i2s state;


#define    timerspecsub(a, b, result) \
do { \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
    (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec; \
    if ((result)->tv_nsec < 0) { \
        --(result)->tv_sec; \
        (result)->tv_nsec += 1000000000; \
    } \
} while (0)


#if TEST_LATENCY
#define TIMER_INIT(TOUT)            \
        struct timespec _t1, _t2, _td;    \
        float _tf, _tout = TOUT;    \
        clock_gettime(CLOCK_MONOTONIC, &_t1);

#define TIMER_CHECK(NAME) {                        \
        clock_gettime(CLOCK_MONOTONIC, &_t2);            \
        timerspecsub(&_t2, &_t1, &_td);                \
        _tf = _td.tv_sec * 1000 + _td.tv_nsec / 1000000.0;    \
        if (_tf > _tout) {                    \
            LOG_WARN(log_beep_main, NAME " took too long %.3f ms limit %0.3f", _tf, _tout); \
            LOG_WARN(log_beep_main, NAME " %d.%d %d.%d\n", _t1.tv_sec, _t1.tv_nsec, _t2.tv_sec, _t2.tv_nsec); \
        }                            \
        memcpy(&_t1, &_t2, sizeof(struct timeval));        \
    }
#else
#define TIMER_INIT(TOUT)
#define TIMER_CHECK(NAME)
#endif


/* noise source for testing */
//static void generate_noise(void *outputBuffer,
//               unsigned long framesPerBuffer)
//{
//    sample_t val, *output_ptr = (sample_t *)outputBuffer;
//
//    while (framesPerBuffer--) {
//        val = rand() % 256 - 127;
//        *output_ptr++ += val << 15;
//    }
//}

void crazy(const char* s) {
    LOG_ERROR(log_beep_main, "CRAZY %s\n", s);
}

/*
 * This function is called by to copy samples from the output buffer to
 * the alsa buffer.
 *
 * Called with fifo-lock held.
 */
static void playback_callback(struct decode_i2s *state,
                  void *output_buf,
                  size_t output_frames) {
    size_t decode_frames, skip_frames = 0;
    int add_silence_ms;
    bool reached_start_point;
    uint8_t *output_buffer = (uint8_t *)output_buf;

    ASSERT_AUDIO_LOCKED();

    decode_frames = BYTES_TO_SAMPLES(fifo_bytes_used(&decode_audio->fifo));

    if (output_frames != 192) {
        crazy("output_frames too high");
    }
    if (decode_frames > 900000) {
        crazy("decode_frames too high");
    }

    /* Should we start the audio now based on having enough decoded data? */
    if (decode_audio->state & DECODE_STATE_AUTOSTART
            && decode_frames > (output_frames * (3 + state->period_count))
            && decode_frames > (decode_audio->output_threshold * state->pcm_sample_rate / 10)
        )
    {
        uint64_t now = beep_millis();
        LOG_INFO(log_beep_main, "Starting audio: %llu", now);

        // If we've missed the audio resume time, drop samples to catch up.
        if (decode_audio->start_at_jiffies < now) {
            uint32_t delta_ms = now - decode_audio->start_at_jiffies;
            decode_audio->skip_ahead_bytes =
                PCM_FRAMES_TO_BYTES(delta_ms * state->pcm_sample_rate / 1000);
            LOG_INFO(log_beep_main, "Skipping %ums to start audio", delta_ms);
        } else if (decode_audio->start_at_jiffies > now && now > decode_audio->start_at_jiffies - 5000) {
            /* This does not consider any delay in the ALSA output chain - usually 1 period which is 10ms by default */
            decode_audio->add_silence_ms = decode_audio->start_at_jiffies - now;
            LOG_INFO(log_beep_main, "Waiting %ums to start audio", decode_audio->add_silence_ms);
        }

        decode_audio->state &= ~DECODE_STATE_AUTOSTART;
        decode_audio->state |= DECODE_STATE_RUNNING;
    }

    add_silence_ms = decode_audio->add_silence_ms;
    if (add_silence_ms < 0) {
        crazy("add_silence_ms < 0");
    }
    if (add_silence_ms > 2000) {
        crazy("add_silence_ms > 2000");
    }
    if (add_silence_ms) {
        unsigned int add_frames;

        add_frames = (add_silence_ms * state->pcm_sample_rate) / 1000;
        if (add_frames > output_frames) {
            add_frames = output_frames;
        }
        memset(output_buffer, 0, PCM_FRAMES_TO_BYTES(add_frames));
        output_buffer += PCM_FRAMES_TO_BYTES(add_frames);
        output_frames -= add_frames;
        add_silence_ms -= (add_frames * 1000) / state->pcm_sample_rate;
        if (add_silence_ms < 2) {
            add_silence_ms = 0;
        }

        decode_audio->add_silence_ms = add_silence_ms;

        if (!output_frames) {
            return;
        }
    }

    /* only skip if it will not cause an underrun */
    if (decode_frames >= output_frames && decode_audio->skip_ahead_bytes > 0) {
        skip_frames = decode_frames - output_frames;
        if (skip_frames > BYTES_TO_SAMPLES(decode_audio->skip_ahead_bytes)) {
            skip_frames = BYTES_TO_SAMPLES(decode_audio->skip_ahead_bytes);
        }
    }

    if (decode_frames > output_frames) {
        decode_frames = output_frames;
    }

    /* audio underrun? */
    if ((decode_audio->state & DECODE_STATE_RUNNING)
            && decode_frames < output_frames) {
        memset(output_buffer + PCM_FRAMES_TO_BYTES(decode_frames), 0, PCM_FRAMES_TO_BYTES(output_frames) - PCM_FRAMES_TO_BYTES(decode_frames));

        if ((decode_audio->state & DECODE_STATE_UNDERRUN) == 0) {
            LOG_ERROR(log_beep_main,
                    "Audio underrun: used %zu frames, requested %zu frames."
                    "written track samples: %u",
                    decode_frames,
                    output_frames,
                    decode_audio->written_track_samples);
        }

        decode_audio->state |= DECODE_STATE_UNDERRUN;
    }
    else {
        decode_audio->state &= ~DECODE_STATE_UNDERRUN;
    }

    if (skip_frames) {
        size_t wrap_frames;

        LOG_DEBUG(log_beep_main, "Skipping %d frames", (int)skip_frames);

        wrap_frames = BYTES_TO_SAMPLES(fifo_bytes_until_rptr_wrap(&decode_audio->fifo));

        if (wrap_frames < skip_frames) {
            fifo_rptr_incby(&decode_audio->fifo, SAMPLES_TO_BYTES(wrap_frames));
            decode_audio->skip_ahead_bytes -= SAMPLES_TO_BYTES(wrap_frames);
            decode_audio->written_track_samples += wrap_frames;
            skip_frames -= wrap_frames;
        }

        fifo_rptr_incby(&decode_audio->fifo, SAMPLES_TO_BYTES(skip_frames));
        decode_audio->skip_ahead_bytes -= SAMPLES_TO_BYTES(skip_frames);
        decode_audio->written_track_samples += skip_frames;
    }

    /* audio running? */
    // This check is performed after the skip_ahead logic so that we may
    // perform a skip_ahead when not in DECODE_STATE_RUNNING (ie when paused).
    if (!(decode_audio->state & DECODE_STATE_RUNNING)) {
        memset(output_buffer, 0, PCM_FRAMES_TO_BYTES(output_frames));

        return;
    }

    while (decode_frames) {
        size_t wrap_frames, frames_write, frames_cnt;
        int32_t lgain, rgain;

        lgain = decode_audio->lgain;
        rgain = decode_audio->rgain;

        wrap_frames = BYTES_TO_SAMPLES(fifo_bytes_until_rptr_wrap(&decode_audio->fifo));
        if (wrap_frames > 900000) {
            crazy("wrap_frames is insane");
        }

        frames_write = decode_frames;
        if (wrap_frames < frames_write) {
            frames_write = wrap_frames;
        }

        frames_cnt = frames_write;

        /* Handle fading and delayed fading */
        if (decode_audio->samples_to_fade) {
            crazy("fading? wtf");
            if (decode_audio->samples_until_fade > frames_write) {
                decode_audio->samples_until_fade -= frames_write;
            }
            else {
                decode_audio->samples_until_fade = 0;

                /* initialize transition parameters */
                if (!decode_audio->transition_gain_step) {
                    size_t nbytes;
                    fft_fixed interval;

                    interval = determine_transition_interval(decode_audio->transition_sample_rate, (uint32_t)(decode_audio->samples_to_fade / decode_audio->transition_sample_rate), &nbytes);
                    if (!interval)
                        interval = 1;

                    decode_audio->transition_gain_step = fixed_div(FIXED_ONE, fixed_mul(interval, int32_to_fixed(TRANSITION_STEPS_PER_SECOND)));
                    decode_audio->transition_gain = FIXED_ONE;
                    decode_audio->transition_sample_step = decode_audio->transition_sample_rate / TRANSITION_STEPS_PER_SECOND;
                    decode_audio->transition_samples_in_step = 0;

                    LOG_DEBUG(log_beep_main, "Starting FADEOUT over %d seconds, transition_gain_step %d, transition_sample_step %d",
                        fixed_to_s32(interval), decode_audio->transition_gain_step, decode_audio->transition_sample_step);
                }

                /* Apply transition gain to left/right gain values */
                lgain = fixed_mul(lgain, decode_audio->transition_gain);
                rgain = fixed_mul(rgain, decode_audio->transition_gain);

                /* Reduce transition gain when we've processed enough samples */
                decode_audio->transition_samples_in_step += frames_write;
                while (decode_audio->transition_gain && decode_audio->transition_samples_in_step >= decode_audio->transition_sample_step) {
                    decode_audio->transition_samples_in_step -= decode_audio->transition_sample_step;
                    decode_audio->transition_gain -= decode_audio->transition_gain_step;
                }
            }
        }

        /* 16-bit samples */
        sample_t *decode_ptr;
        int16_t *output_ptr;
        uint16_t out_sample, flip_sample;

        output_ptr = (int16_t *)(void *)output_buffer;
        decode_ptr = (sample_t *)(void *)(decode_fifo_buf + decode_audio->fifo.rptr);
        //printf("frames: %d\n", frames_cnt);
        while (frames_cnt--) {
            //*(output_ptr++) = fixed_mul(lgain, *(decode_ptr++)) >> 16;
            //*(output_ptr++) = fixed_mul(rgain, *(decode_ptr++)) >> 16;

            // Left channel: flip bytes
            out_sample = fixed_mul(lgain, *(decode_ptr++)) >> 16;
            flip_sample = (((out_sample & 0xff) << 8)
                           | ((out_sample & 0xff00) >> 8));
            *(output_ptr++) = flip_sample;

            // Right channel: flip bytes
            out_sample = fixed_mul(rgain, *(decode_ptr++)) >> 16;
            flip_sample = (((out_sample & 0xff) << 8)
                           | ((out_sample & 0xff00) >> 8));
            *(output_ptr++) = flip_sample;
        }

        fifo_rptr_incby(&decode_audio->fifo, SAMPLES_TO_BYTES(frames_write));
        decode_audio->written_track_samples += frames_write;

        output_buffer += PCM_FRAMES_TO_BYTES(frames_write);
        decode_frames -= frames_write;
    }

    reached_start_point = decode_check_start_point();
    if (reached_start_point) {
        decode_audio->samples_to_fade = 0;
        decode_audio->transition_gain_step = 0;

        if (decode_audio->track_sample_rate != state->pcm_sample_rate) {
            decode_audio->set_sample_rate = decode_audio->track_sample_rate;
        }

        LOG_DEBUG(log_beep_main, "playback started, copyright %s asserted", (decode_audio->track_copyright)?"is":"not");
    }
}

static int pcm_open(struct decode_i2s *state) {
    uint32_t sample_rate;

    decode_audio_lock();
    sample_rate = decode_audio->set_sample_rate;
    decode_audio_unlock();

    if (state->i2s_fd > 0) {
        close(state->i2s_fd);
        state->i2s_fd = -1;
    }

    // Only currently used for start threshold
    state->period_count = 10;

    if (!NO_AUDIO) {
        state->i2s_fd = open("/dev/i2s", O_WRONLY);

        if (state->i2s_fd < 0) {
            LOG_ERROR(log_beep_main, "Couldn\'t open i2s device\n");
            return -1;
        }

        if (ioctl(state->i2s_fd, I2S_DSIZE, 16) < 0) {
            LOG_ERROR(log_beep_main, "Couldn\'t set ioctl I2S_DSIZE failed\n");
            return -1;
        }

        if (ioctl(state->i2s_fd, I2S_FREQ, sample_rate) < 0) {
            LOG_ERROR(log_beep_main, "Couldn\'t set ioctl I2S_FREQ failed\n");
            return -1;
        }
    }

    state->pcm_sample_rate = sample_rate;

    return 0;
}


static void *audio_thread_execute(void *data) {
    struct decode_i2s *state = (struct decode_i2s *)data;
    int ret;
    int do_open = 1;

    // TODO: This needs to be exact... why?
    state->period_size = 768 / 4;
    void *buf = malloc(PCM_FRAMES_TO_BYTES(state->period_size));

    LOG_DEBUG(log_beep_main, "audio_thread_execute");

    /* assume we'll be 44.1k to start with */
    decode_audio->set_sample_rate = 44100;

    while (1) {
        TIMER_INIT(10.0f); /* 10 ms limit */

        if (do_open) {
            do_open = 0;

            if (pcm_open(state) < 0) {
                LOG_ERROR(log_beep_main, "Playback open failed");
                goto thread_error;
            }
        }

#if DEBUG_PAGEFAULTS
        debug_pagefaults();
#endif

        if (kill(state->parent_pid, 0) < 0) {
            /* parent is dead, exit */
            LOG_ERROR(log_beep_main, "exit, parent is dead");
            goto thread_error;
        }

        decode_audio_lock();
        playback_callback(state, buf, state->period_size);

        /* sample rate changed? we do this check while the
         * fifo is locked, so we don't need to lock it twice
         * per loop.
         */
        do_open = decode_audio->set_sample_rate && (decode_audio->set_sample_rate != state->pcm_sample_rate);
        decode_audio_unlock();

        if (!NO_AUDIO) {
            do  {
                ret = write(state->i2s_fd, buf, PCM_FRAMES_TO_BYTES(state->period_size));
            } while (ret == -ERESTART);

            struct i2s_sync i2s_sync;
            if (ioctl(state->i2s_fd, I2S_GET_SYNC, &i2s_sync) < 0) {
                LOG_ERROR(log_beep_main,
                        "Couldn\'t fetch sync info from driver.");
                goto thread_error;
            }
            decode_audio->sync_played_samples =
                    decode_audio->written_track_samples
                    - i2s_sync.buffered_samples;
            decode_audio->sync_timestamp =
                1000 * i2s_sync.tv.tv_sec + i2s_sync.tv.tv_usec / 1000;
        } else {
            decode_audio->sync_played_samples = 1000000;
            decode_audio->sync_timestamp = 1000;

            // Assumes 44.1kHz
            usleep(4354);
        }
    }

 thread_error:
    free(buf);
    LOG_ERROR(log_beep_main, "Audio thread exited");
    return (void *)-1;
}


static int decode_realtime_process(struct decode_i2s *state)
{
    struct sched_param sched_param;
    int err;

    /* Set realtime scheduler policy. Use 45 as the PREEMPT_PR patches
     * use 50 as the default prioity of the kernel tasklets and irq
     * handlers.
     *
     * For the best performance on a tuned RT kernel, make non-audio
     * threads have a priority < 45.
     */
    sched_param.sched_priority = (state->flags & FLAG_STREAM_PLAYBACK) ? 45 : 35;

    if ((err = sched_setscheduler(0, SCHED_FIFO, &sched_param)) == -1) {
        if (errno == EPERM) {
            LOG_INFO(log_beep_main, "Can't set audio thread priority");
            return -1;
        }
        else {
            LOG_ERROR(log_beep_main, "sched_setscheduler: %s", strerror(errno));
            return -1;
        }
    }

    return 0;
}

static int decode_lock_memory(void)
{
    size_t i, page_size;

    /* lock all current and future pages into ram */
    if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
        LOG_WARN(log_beep_main, "mlockall failed");
        return -1;
    }

    /* Turn off malloc trimming.*/
       mallopt(M_TRIM_THRESHOLD, -1);

       /* Turn off mmap usage. */
       mallopt(M_MMAP_MAX, 0);

    page_size = sysconf(_SC_PAGESIZE);

    /* touch each page of buffer */
    for (i=0; i<DECODE_AUDIO_BUFFER_SIZE; i+=page_size) {
        *(decode_fifo_buf + i) = 0;
    }

#if DEBUG_PAGEFAULTS
    debug_pagefaults();
#endif

    return 0;
}


static int decode_i2s_shared_mem_attach(void)
{
    int shmid;

    // Use a unique key so that multiple instances can safely run in
    // parallel
    key_t shmkey = 56833 + getppid();

    /* attach to shared memory */
    shmid = shmget(shmkey, 0, 0);
    if (shmid == -1) {
        LOG_ERROR(log_beep_main, "shmget failed: %s", strerror(errno));
        if (errno == EINVAL) {
            LOG_ERROR(log_beep_main, "shm segment didn\'t exist");
        }
        exit(1);
    }

    decode_audio = shmat(shmid, 0, 0);
    // XXXX errors

    decode_fifo_buf = (((uint8_t *)decode_audio) + sizeof(struct decode_audio));
    effect_fifo_buf = ((uint8_t *)decode_fifo_buf) + DECODE_FIFO_SIZE;

    return 0;
}


int main(int argv, char **argc)
{
    struct utsname utsname;
    int err;

    log_beep_main = LOG_CATEGORY_GET("decode_i2s_backend");
    log_category_set_priority (log_beep_main, LOG_PRIORITY_DEBUG);

    /* attach to shared memory buffer */
    if (decode_i2s_shared_mem_attach() != 0) {
        LOG_ERROR(log_beep_main, "Can't attach to shared memory");
        exit(-1);
    }

    /* do this on RT Linux only, as it seems to interfere with suspend/resume on jive */
    if ((err = uname(&utsname)) < 0) {
        LOG_ERROR(log_beep_main, "uname failed: %s", strerror(err));
        exit(-1);
    }
    if (strstr(utsname.version, "PREEMPT") != NULL) {
        decode_lock_memory();
    }

    state.i2s_fd = -1;

    /* who is our parent */
    state.parent_pid = getppid();

    /* set real-time properties */
    decode_realtime_process(&state);
    setpriority(PRIO_PROCESS, 0, -20);

    /* wake parent */
    decode_audio_lock();
    decode_audio->running = true;
    fifo_signal(&decode_audio->fifo);
    decode_audio_unlock();

    /* start thread */
    audio_thread_execute(&state);

    exit(0);
}
