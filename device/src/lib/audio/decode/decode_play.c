/*
** Copyright 2007-2008 Logitech. All Rights Reserved.
**
** This file is licensed under BSD. Please see the LICENSE file for details.
*/

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "audio/fifo.h"
#include "audio/fixed_math.h"
#include "audio/mqueue.h"
#include "audio/streambuf.h"
#include "audio/decode/decode.h"
#include "audio/decode/decode_priv.h"
#include "beep/config.h"

#ifdef _MIPS_ARCH
#define DEFAULT_DECODE_BACKEND "beepi2s"
#else
#define DEFAULT_DECODE_BACKEND "beepalsa"
#endif


#define ALSA_DEFAULT_DEVICE "default"
#define ALSA_DEFAULT_BUFFER_TIME 30000
#define ALSA_DEFAULT_PERIOD_COUNT 3

#define FLAG_STREAM_PLAYBACK 0x01
#define FLAG_STREAM_EFFECTS  0x02
#define FLAG_STREAM_NOISE    0x04

#define PATH_MAX 256

pid_t play_effect_pid = -1;
pid_t play_playback_pid = -1;

char* decode_backend_process;

static void decode_play_check_pid(pid_t pid, const char* pid_name) {
    int result;
    if (pid >= 0) {
        result = waitpid(play_playback_pid, NULL, WNOHANG);
        if (result == -1) {
            // This happens if there are no children, for example (the
            // child died).
            LOG_ERROR(log_beep_main, "Exit, could not wait for %s child: %s",
                    pid_name, strerror(errno));
            exit(-1);
        } else if (result == play_effect_pid) {
            // This doesn't seem to fire when we do 'killall beepi2s',
            // so the above branch was added which does fire.
            LOG_ERROR(log_beep_main, "Exit, %s child is dead", pid_name);
            exit(-1);
        }
    }
}

void decode_play_check_pids(void) {
    decode_play_check_pid(play_effect_pid, "effect");
    decode_play_check_pid(play_playback_pid, "playback");
}


static void decode_play_start(void) {
    LOG_DEBUG(log_beep_main, "decode_play_start");

    ASSERT_AUDIO_LOCKED();

    decode_audio->set_sample_rate = decode_audio->track_sample_rate;

    decode_play_check_pids();
}


static void decode_play_resume(void) {
    LOG_DEBUG(log_beep_main, "decode_play_resume");

    ASSERT_AUDIO_LOCKED();

    decode_play_check_pids();
}


static void decode_play_pause(void) {
    LOG_DEBUG(log_beep_main, "decode_play_pause");

    ASSERT_AUDIO_LOCKED();

    decode_play_check_pids();
}


static void decode_play_stop(void) {
    LOG_DEBUG(log_beep_main, "decode_play_stop");

    ASSERT_AUDIO_LOCKED();

    decode_play_check_pids();
}


static pid_t decode_play_fork(const char *device, const char *capture, unsigned int buffer_time, unsigned int period_count, unsigned int sample_size, uint32_t flags)
{
    char *path, b[10], p[10], f[10], s[10];
    char *cmd[20];
    pid_t pid;
    int i, idx = 0;

    path = alloca(PATH_MAX);

    /* beepalsa [-v] -d <device> -b <buffer_time> -p <period_count> -f <flags> */

    cmd[idx++] = decode_backend_process;

    if (IS_LOG_PRIORITY(log_beep_main, LOG_PRIORITY_DEBUG)) {
        cmd[idx++] = "-v";
    }

    cmd[idx++] = "-d";
    cmd[idx++] = (char *)device;

    if (capture) {
        cmd[idx++] = "-c";
        cmd[idx++] = (char *)capture;
    }

    snprintf(b, sizeof(b), "%d", buffer_time);
    cmd[idx++] = "-b";
    cmd[idx++] = b;

    snprintf(p, sizeof(p), "%d", period_count);
    cmd[idx++] = "-p";
    cmd[idx++] = p;

    snprintf(s, sizeof(s), "%d", sample_size);
    cmd[idx++] = "-s";
    cmd[idx++] = s;

    snprintf(f, sizeof(f), "%d", flags);

    cmd[idx++] = "-f";
    cmd[idx++] = f;

    cmd[idx] = '\0';

    if (IS_LOG_PRIORITY(log_beep_main, LOG_PRIORITY_DEBUG)) {
        path[0] = '\0';
        for (i=0; i<idx; i++) {
            strncat(path, cmd[i], PATH_MAX);
            strncat(path, " ", PATH_MAX);
        }
        LOG_DEBUG(log_beep_main, "fork %s", path);
    }

    /* command path */
    getcwd(path, PATH_MAX);
    strncat(path, "/", PATH_MAX);
    strncat(path, decode_backend_process, PATH_MAX);

    decode_audio_lock();
    decode_audio->running = false;

    /* fork + exec */
    pid = vfork();
    if (pid < 0) {
        LOG_ERROR(log_beep_main, "fork failed %d", errno);
        _exit(-1);
    }
    if (pid == 0) {
        /* child */
        execv(path, cmd);

        LOG_ERROR(log_beep_main, "execv failed %d. Do you have %s?",
                errno, decode_backend_process);
        _exit(-1);
    }

    /* wait for backend process to start */
    while (1) {
        fifo_wait_timeout(&decode_audio->fifo, 500);

        if (decode_audio->running) {
            break;
        }

        if (waitpid(pid, NULL, WNOHANG) == pid) {
            decode_audio_unlock();

            LOG_ERROR(log_beep_main, "%s failed to start", cmd[0]);
            _exit(1);
        }

    }
    decode_audio_unlock();

    return pid;
}


static int decode_play_init(void) {
    const char *playback_device;
    const char *capture_device;
    const char *effects_device;
    unsigned int buffer_time;
    unsigned int period_count;
    unsigned int sample_size;
    int shmid;
    void *buf;

    LOG_INFO(log_beep_main, "START DECODE PLAY INIT");

    /* allocate memory */

    // Use a unique key so that multiple instances can safely run in
    // parallel
    key_t shmkey = 56833 + getpid();

    // Destroy it if it exists.
    shmid = shmget(shmkey, 0, 0600);
    if (shmid != -1) {
        if (shmctl(shmid, IPC_RMID, NULL) < 0) {
            LOG_ERROR(log_beep_main, "shmctl error %s", strerror(errno));
            abort();
        }
    }

    // Create the segment, but die if it already exists.
    shmid = shmget(shmkey, DECODE_AUDIO_BUFFER_SIZE, 0600
            | IPC_CREAT | IPC_EXCL);
    if (shmid == -1) {
        // XXXX errors
        LOG_ERROR(log_beep_main, "shmget error %s", strerror(errno));
        abort();
    }

    struct shmid_ds ds;
    shmctl(shmid, IPC_STAT, &ds);
    LOG_DEBUG(log_beep_main, "ds.nattach: %d", (int)ds.shm_nattch);

    // Attach to the segment
    buf = shmat(shmid, 0, 0);
    if (buf == (void *)-1) {
        // XXXX errors
        LOG_ERROR(log_beep_main, "shmgat error %s", strerror(errno));
        abort();
    }

    shmctl(shmid, IPC_STAT, &ds);
    LOG_DEBUG(log_beep_main, "ds.nattach: %d %d", (int)ds.shm_nattch, ds.shm_perm.mode);

    shmctl(shmid, IPC_STAT, &ds);
    LOG_DEBUG(log_beep_main, "ds.nattach: %d %d", (int)ds.shm_nattch, ds.shm_perm.mode);

    int rc;
    if ((rc = beep_config_devel_get_int("dummy_audio_output", 0))) {
       decode_backend_process = "beepdummy";
    } else {
       decode_backend_process = DEFAULT_DECODE_BACKEND;
    }

    decode_init_buffers(buf, true);


    /* start threads */
    playback_device = ALSA_DEFAULT_DEVICE;
    capture_device = ALSA_DEFAULT_DEVICE;
    effects_device = NULL;
    sample_size = 16;


#if 0
    /* test if device is available */
    if (pcm_test(playback_device, &playback_max_rate) < 0) {
        lua_pop(L, 2);
        return 0;
    }

    if (effects_device && pcm_test(effects_device, NULL) < 0) {
        effects_device = NULL;
    }
#endif


    /* effects device */
    if (effects_device) {
        LOG_DEBUG(log_beep_main, "Effects device: %s", effects_device);

        buffer_time = ALSA_DEFAULT_BUFFER_TIME;
        period_count = ALSA_DEFAULT_PERIOD_COUNT;

        play_effect_pid = decode_play_fork(effects_device, NULL, buffer_time, period_count, 16, FLAG_STREAM_EFFECTS);
    }


    /* playback device */
    LOG_DEBUG(log_beep_main, "Playback device: %s", playback_device);

    buffer_time = ALSA_DEFAULT_BUFFER_TIME;
    period_count = ALSA_DEFAULT_PERIOD_COUNT;

    play_playback_pid = decode_play_fork(playback_device, capture_device, buffer_time, period_count, sample_size,
                    (effects_device) ? FLAG_STREAM_PLAYBACK : FLAG_STREAM_PLAYBACK | FLAG_STREAM_EFFECTS /*| FLAG_STREAM_NOISE*/);

    // Now set the removal flag so the shared memory segment will be
    // deallocated when the processes using it exit. This can only be
    // done after the child process attaches, otherwise for some reason
    // it will be destroyed immediately.
    if (shmctl(shmid, IPC_RMID, NULL) < 0) {
        LOG_ERROR(log_beep_main, "shmctl error %s", strerror(errno));
        abort();
    }

    return 0;
}


struct decode_audio_func decode_play = {
    decode_play_init,
    decode_play_start,
    decode_play_pause,
    decode_play_resume,
    decode_play_stop,
};
