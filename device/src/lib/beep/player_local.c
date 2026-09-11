/* Player for playing audio over a local audio device.
 *
 * This implementation uses Squeezeplay's audio core to play audio.
 *
 * TODO: Consider dying on the second time we return STOPPED. This would
 *    prevent loops for callers who forgot to call start after a stop.
 *
 * TODO: Have st_begin return a track id, require the caller to use that
 *    in subsequent calls to buffer().
 *
 * TODO: Enforce maximum two tracks in pcm fifo.
 */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "audio/audio.h"
#include "audio/decode/decode.h"  // For DECODE_STATE_UNDERRUN
#include "audio/streambuf.h"
#include "beep/player.h"
#include "beep/debug.h"

#define ASSERT_PLAYER_LOCAL_LOCKED(player_local) \
    assert((player_local)->locked)

typedef struct {
    BeepPlayer _;

    pthread_t* thread;
    bool stop_thread;
    pthread_mutex_t mutex;
    bool locked;

    bool started;

    bool syncing;

    SongStartedCb song_started_cb;
    void* user_data;

    // List of pointers to user song data (void*) for songs which have
    // been prepared but not yet started.
    QUEUE pending_tracks;

    uint32_t tracks_started;
} BeepPlayerLocal;

void beep_player_local_lock(BeepPlayerLocal* player_local) {
    int mutex_lock_result = pthread_mutex_lock(&player_local->mutex);
    assert(mutex_lock_result == 0);
    player_local->locked = true;
}

void beep_player_local_unlock(BeepPlayerLocal* player_local) {
    ASSERT_PLAYER_LOCAL_LOCKED(player_local);
    player_local->locked = false;
    int mutex_unlock_result = pthread_mutex_unlock(&player_local->mutex);
    assert(mutex_unlock_result == 0);
}

bool beep_player_local_can_prepare_decoder(BeepPlayer* player) {
    BeepPlayerLocal* player_local = (BeepPlayerLocal*) player;

    ASSERT_PLAYER_LOCAL_LOCKED(player_local);

    BeepAudioStatus status = audio_status();
    if (!status.decode_state
            || (!streambuf_is_streaming()
                && status.decode_state & DECODE_STATE_UNDERRUN)) {
        return true;
    } else {
        return false;
    }
}

int beep_player_local_can_prepare(BeepPlayer* player) {
    BeepPlayerLocal* player_local = (BeepPlayerLocal*) player;

    ASSERT_PLAYER_LOCAL_LOCKED(player_local);

    int ret = BEEP_PLAYER_AGAIN;
    if (!player_local->started) {
        LOG_DEBUG(log_beep_main, "Can_prepare returning STOPPED");
        ret = BEEP_PLAYER_STOPPED;
    } else {
        if (beep_player_local_can_prepare_decoder(player)) {
            ret = BEEP_PLAYER_OK;
        } else {
            ret = BEEP_PLAYER_AGAIN;
        }
    }

    return ret;
}

int beep_player_local_st_begin(
        BeepPlayer *player,
        int file_type,
        uint32_t transition_type,
        uint32_t transition_period,
        uint32_t replay_gain,
        uint32_t output_threshold,
        uint32_t polarity_inversion,
        uint32_t output_channels,
        void* song_data) {
    BeepPlayerLocal* player_local = (BeepPlayerLocal*) player;

    beep_player_local_lock(player_local);

    int can_prepare = beep_player_local_can_prepare(player);
    if (can_prepare != BEEP_PLAYER_OK) {
        beep_player_local_unlock(player_local);
        return can_prepare;
    }

    qEnque(player_local->pending_tracks, song_data);

    streambuf_set_streaming(true);

    audio_decoder_start(
            file_type,
            transition_type,
            transition_period,
            replay_gain,
            output_threshold,
            polarity_inversion,
            output_channels);
    audio_decoder_resume();

    beep_player_local_unlock(player_local);

    return BEEP_PLAYER_OK;
}

int beep_player_local_st_begin_blocking(
        BeepPlayer *player,
        int file_type,
        uint32_t transition_type,
        uint32_t transition_period,
        uint32_t replay_gain,
        uint32_t output_threshold,
        uint32_t polarity_inversion,
        uint32_t output_channels,
        void* song_data) {
    LOG_DEBUG(log_beep_main, "st_begin_blocking");
    while (1) {
        int res = beep_player_local_st_begin(
                player,
                file_type,
                transition_type,
                transition_period,
                replay_gain,
                output_threshold,
                polarity_inversion,
                output_channels,
                song_data);
        if (res == BEEP_PLAYER_STOPPED || res == BEEP_PLAYER_OK) {
            return res;
        }
        usleep(100000);
    }
}

int beep_player_local_st_end(BeepPlayer* player) {
    LOG_DEBUG(log_beep_main, "st_end");

    BeepPlayerLocal* player_local = (BeepPlayerLocal*) player;

    beep_player_local_lock(player_local);

    if (!player_local->started) {
        beep_player_local_unlock(player_local);
        LOG_INFO(log_beep_main, "st_end called when stopped");
        return BEEP_PLAYER_STOPPED;
    }

    if (!streambuf_is_streaming()) {
        LOG_ERROR(log_beep_main, "st_end called when streambuf not streaming");
        abort();
    }

    audio_decoder_st_end();

    beep_player_local_unlock(player_local);

    return BEEP_PLAYER_OK;
}

int beep_player_local_can_buffer(BeepPlayer* player, int len) {
    BeepPlayerLocal* player_local = (BeepPlayerLocal*) player;

    ASSERT_PLAYER_LOCAL_LOCKED(player_local);

    int ret = BEEP_PLAYER_AGAIN;
    if (!player_local->started) {
        LOG_DEBUG(log_beep_main, "can_buffer returning STOPPED");
        ret = BEEP_PLAYER_STOPPED;
    } else if (len <= streambuf_get_freebytes()) {
        ret = BEEP_PLAYER_OK;
    }

    return ret;
}

int beep_player_local_buffer(
        BeepPlayer *player, uint8_t* data, size_t len) {
    BeepPlayerLocal* player_local = (BeepPlayerLocal*) player;

    beep_player_local_lock(player_local);

    int can_buffer = beep_player_local_can_buffer(player, len);
    if (can_buffer != BEEP_PLAYER_OK) {
        beep_player_local_unlock(player_local);
        return can_buffer;
    }

    streambuf_feed(data, len);

    audio_wakeup_decode_thread();

    beep_player_local_unlock(player_local);

    return BEEP_PLAYER_OK;
}

int beep_player_local_buffer_blocking(
        BeepPlayer *player, uint8_t* data, size_t len) {
    while (1) {
        int res = beep_player_local_buffer(player, data, len);
        if (res == BEEP_PLAYER_STOPPED || res == BEEP_PLAYER_OK) {
            return res;
        }
        usleep(100000);
    }
}

void beep_player_local_flush(BeepPlayer *player, int play_cookie) {
    BeepPlayerLocal* player_local = (BeepPlayerLocal*) player;

    beep_player_local_lock(player_local);

    streambuf_flush();
    audio_decoder_flush(play_cookie);

    beep_player_local_unlock(player_local);
}

int beep_player_local_start(BeepPlayer* player) {
    LOG_DEBUG(log_beep_main, "start");
    BeepPlayerLocal* player_local = (BeepPlayerLocal*) player;
    beep_player_local_lock(player_local);
    if (player_local->started) {
        LOG_WARN(log_beep_main, "beep_player_local_start called when already started");
    }
    player_local->started = true;
    beep_player_local_unlock(player_local);

    return BEEP_PLAYER_OK;
}

int beep_player_local_stop(BeepPlayer *player, int play_cookie) {
    LOG_DEBUG(log_beep_main, "stop");

    BeepPlayerLocal* player_local = (BeepPlayerLocal*) player;

    audio_pause(0);

    beep_player_local_lock(player_local);

    if (!player_local->started) {
        LOG_WARN(log_beep_main, "beep_player_local_stop called when already stopped");
    }
    player_local->started = false;
    player_local->tracks_started = 0;
    // TODO: This needs to clear pending tracks.

    BeepAudioStatus status;
    audio_decoder_stop(play_cookie);
    while (1) {
        status = audio_status();
        //LOG_DEBUG(log_beep_main, "STATE: %d %d", status.decode_state, status.num_tracks_started);
        if (status.decode_state == 0 && status.num_tracks_started == 0) {
            break;
        }
        usleep(10000);
    }

    streambuf_set_streaming(false);
    streambuf_flush();

    beep_player_local_unlock(player_local);

    return BEEP_PLAYER_OK;
}

int beep_player_local_pause(BeepPlayer *player) {
    audio_pause(0);

    return BEEP_PLAYER_OK;
}

int beep_player_local_resume(BeepPlayer *player, uint64_t millis) {
    LOG_DEBUG(log_beep_main, "RESUMING AT %" PRIu64, millis);
    audio_resume(millis);

    return BEEP_PLAYER_OK;
}

int beep_player_local_skip_ahead(BeepPlayer *player, uint32_t interval_us) {
    audio_skip_ahead(interval_us);

    return BEEP_PLAYER_OK;
}

int beep_player_local_set_volume(BeepPlayer *player, int32_t gain) {
    audio_gain(gain);

    return BEEP_PLAYER_OK;
}

int beep_player_local_adjust_volume(BeepPlayer *player, int32_t gain) {
    audio_adjust_gain(gain);

    return BEEP_PLAYER_OK;
}

int beep_player_local_shutdown(BeepPlayer *player) {
    BeepPlayerLocal* player_local = (BeepPlayerLocal*) player;

    beep_player_local_lock(player_local);
    player_local->stop_thread = true;
    beep_player_local_unlock(player_local);

    return BEEP_PLAYER_OK;
}

BeepPlayerStatus beep_player_local_status(BeepPlayer* player) {
    BeepPlayerLocal* player_local = (BeepPlayerLocal*) player;

    beep_player_local_lock(player_local);

    BeepPlayerStatus status;
    status.status_fetch_error = false;

    BeepAudioStatus a_status = audio_status();
    status.num_tracks_started = a_status.num_tracks_started;
    status.output_used = a_status.output_used;
    status.output_size = a_status.output_size;
    status.streambuf_size = a_status.streambuf_size;
    status.streambuf_used = a_status.streambuf_used;
    status.can_prepare_decoder = beep_player_local_can_prepare_decoder(player);
    status.written_track_time = a_status.written_track_time;
    status.sync_played_time = a_status.sync_played_time;
    status.sync_timestamp = a_status.sync_timestamp;
    status.gain = a_status.gain;
    status.play_cookie = a_status.play_cookie;
    status.bitrate = a_status.bitrate;

    beep_player_local_unlock(player_local);

    return status;
}

// everyone else, 0 = ok/done, 1 more, -1 error.
// Implementation:
// * Prepare to/for sync should call prepare on each owned functional object
// and if all successful, wait for any pending state changes (that can not be
// suspended), then prepare itself.  This should ensure state correctness to be
// sent/received.
// * Sending state should send its state first, then each owned functional
// object.  Each layer of state sent should be transparent to all other
// layers simply returning more, done, or error.  If possible the bulk
// of streambuf should be sent allowing the new player to start decoding
// before syncing is completed.
// * Receiving state is simply the reverse of sending state.
// * TODO: Figure out what to do for errors or unexpected input (skip, next
// song, stop).  Right nwo this will probably just fail horribly.
int beep_player_local_prepare_sync_to(BeepPlayer *player, bool start) {
    BeepPlayerLocal *player_local = (BeepPlayerLocal *)player;
    int ret;

    beep_player_local_lock(player_local);

    LOG_DEBUG(log_beep_main, "start: %d", start);

    ret = audio_prepare_sync_to(start);

    if ((ret == 0) && start) {
        player_local->syncing = true;
    } else {
        // set false if error or requested.
        player_local->syncing = false;
    }

    beep_player_local_unlock(player_local);

    return (ret == 0) ? BEEP_PLAYER_OK : BEEP_PLAYER_ERROR;
}

int beep_player_local_sync_state_send(BeepPlayer *player, uint8_t *buf, uint32_t *len) {
    BeepPlayerLocal *player_local = (BeepPlayerLocal *)player;
    int ret = BEEP_PLAYER_ERROR;

    beep_player_local_lock(player_local);

    if (player_local->syncing == true) {
        ret = audio_sync_state_send(buf, len);
        switch (ret) {
            case 0:
                ret = BEEP_PLAYER_OK;
                break;
            case 1:
                ret = BEEP_PLAYER_AGAIN;
                break;
            case -1:
            default:
                ret = BEEP_PLAYER_ERROR;
                break;
        }
    }

    beep_player_local_unlock(player_local);

    return ret;
}

int beep_player_local_prepare_sync_wait(BeepPlayer *player, bool start) {
    BeepPlayerLocal *player_local = (BeepPlayerLocal *)player;
    int ret;

    beep_player_local_lock(player_local);

    LOG_DEBUG(log_beep_main, "start: %d", start);

    ret = audio_prepare_sync_wait(start);

    if ((ret == 0) && start) {
        player_local->syncing = true;
    } else {
        // set false if error or requested.
        player_local->syncing = false;
    }

    beep_player_local_unlock(player_local);

    return (ret == 0) ? BEEP_PLAYER_OK : BEEP_PLAYER_ERROR;
}

int beep_player_local_sync_state_recv(BeepPlayer *player, uint8_t *buf, uint32_t len) {
    BeepPlayerLocal *player_local = (BeepPlayerLocal *)player;
    int ret = BEEP_PLAYER_ERROR;

    beep_player_local_lock(player_local);

    if (player_local->syncing == true) {
        ret = audio_sync_state_recv(buf, len);
        switch (ret) {
            case 0:
                ret = BEEP_PLAYER_OK;
                break;
            case 1:
                ret = BEEP_PLAYER_AGAIN;
                break;
            case -1:
            default:
                ret = BEEP_PLAYER_ERROR;
                break;
        }
    }

    beep_player_local_unlock(player_local);

    return ret;
}

void* _beep_player_notify_thread(void* arg) {
    BeepPlayerLocal *player_local = arg;
    BeepAudioStatus status;
    while (1) {
        beep_player_local_lock(player_local);
        if (player_local->stop_thread) {
            beep_player_local_unlock(player_local);
            break;
        }
        status = audio_status();
        //LOG_DEBUG(log_beep_main, "%d %d", player_local->tracks_started,
        //                  status.num_tracks_started);
        if (status.num_tracks_started > player_local->tracks_started) {
            for (int i=player_local->tracks_started;
                     i<status.num_tracks_started;
                     i++) {
                void* song_data = qDeque(player_local->pending_tracks);
                LOG_DEBUG(log_beep_main, "song data: %p", song_data);
                player_local->song_started_cb(player_local->user_data,
                                              song_data);
            }

            player_local->tracks_started = status.num_tracks_started;
        }
        beep_player_local_unlock(player_local);

        usleep(100000);
    }

    return NULL;
}

BeepPlayer* beep_player_local_init(SongStartedCb song_started_cb, int32_t gain,
                                   void* user_data) {
    BeepPlayer *player = malloc(sizeof(BeepPlayerLocal));
    memset(player, 0, sizeof(BeepPlayerLocal));
    player->st_begin = beep_player_local_st_begin;
    player->st_begin_blocking = beep_player_local_st_begin_blocking;
    player->st_end = beep_player_local_st_end;
    player->buffer = beep_player_local_buffer;
    player->buffer_blocking = beep_player_local_buffer_blocking;
    player->flush = beep_player_local_flush;
    player->pause = beep_player_local_pause;
    player->resume = beep_player_local_resume;
    player->start = beep_player_local_start;
    player->stop = beep_player_local_stop;
    player->skip_ahead = beep_player_local_skip_ahead;
    player->set_volume = beep_player_local_set_volume;
    player->adjust_volume = beep_player_local_adjust_volume;
    player->shutdown = beep_player_local_shutdown;
    player->status = beep_player_local_status;

    audio_init();
    audio_gain(gain);

    BeepPlayerLocal* player_local = (BeepPlayerLocal*) player;

    player_local->song_started_cb = song_started_cb;
    player_local->user_data = user_data;
    player_local->pending_tracks = qMake();
    player_local->started = false;
    player_local->syncing = false;
    player_local->tracks_started = 0;
    pthread_mutex_init(&player_local->mutex, NULL);

    player_local->stop_thread = false;
    player_local->thread = malloc(sizeof(pthread_t));
    pthread_create(player_local->thread, NULL, _beep_player_notify_thread,
            player);

    return player;
}

void beep_player_local_shutdown_wait(BeepPlayer *player) {
    BeepPlayerLocal *player_local = (BeepPlayerLocal *)player;

    pthread_join(*player_local->thread, NULL);
    free(player_local->thread);
    qClose(player_local->pending_tracks);
    free(player_local);
}
