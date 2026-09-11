#ifndef BEEP_PLAYER_H
#define BEEP_PLAYER_H

#include <stdint.h>
#include <stdlib.h>

#include "libds/ds.h"
#include "beep/beeplib.h"
#include "beep/beep_stream_ch.h"

typedef void (*SongStartedCb)(void* user_data, void* song_data);

enum {
    BEEP_PLAYER_OK = 0,
    BEEP_PLAYER_AGAIN,
    BEEP_PLAYER_STOPPED,
    BEEP_PLAYER_ERROR,
};

typedef struct {
    bool status_fetch_error;
    uint32_t num_tracks_started;
    size_t output_size;
    size_t output_used;
    size_t streambuf_size;
    size_t streambuf_used;
    bool can_prepare_decoder;
    uint32_t written_track_time;
    uint32_t sync_played_time;
    uint32_t sync_timestamp;
    int32_t gain;
    int32_t play_cookie;
    int32_t bitrate;
} BeepPlayerStatus;

typedef struct BeepPlayer_ {
    int (*st_begin)(struct BeepPlayer_* player,
                    int file_type,
                    uint32_t transition_type,
                    uint32_t transition_period,
                    uint32_t replay_gain,
                    uint32_t output_threshold,
                    uint32_t polarity_inversion,
                    uint32_t output_channels,
                    void* song_data);
    int (*st_begin_blocking)(struct BeepPlayer_* player,
                    int file_type,
                    uint32_t transition_type,
                    uint32_t transition_period,
                    uint32_t replay_gain,
                    uint32_t output_threshold,
                    uint32_t polarity_inversion,
                    uint32_t output_channels,
                    void* song_data);
    int (*st_end)(struct BeepPlayer_* player);

    int (*buffer)(struct BeepPlayer_* player, uint8_t* data, size_t len);
    int (*buffer_blocking)(
            struct BeepPlayer_* player, uint8_t* data, size_t len);

    void (*flush)(struct BeepPlayer_* player, int32_t play_cookie);

    int (*start)(struct BeepPlayer_* player);
    int (*stop)(struct BeepPlayer_* player, int32_t play_cookie);

    int (*pause)(struct BeepPlayer_* player);
    int (*resume)(struct BeepPlayer_* player, uint64_t time);

    int (*skip_ahead)(struct BeepPlayer_*, uint32_t interval);

    // 1 << 16: 1.0
    int (*set_volume)(struct BeepPlayer_*, int32_t gain);
    int (*adjust_volume)(struct BeepPlayer_*, int32_t gain);

    int (*shutdown)(struct BeepPlayer_*);

    BeepPlayerStatus (*status)(struct BeepPlayer_* player);
} BeepPlayer;


#define NUM_START_TIMES 100
#define MIN_START_TIMES 20

typedef struct {
    BeepPlayer _;

    int command_fd;
    int stream_fd;
    BeepStreamChCtx stream_ctx;

    pthread_t* thread;
    bool stop_thread;
    pthread_mutex_t mutex;
    bool locked;
    BeepPlayerStatus status_val;

    int reported_gain;

    bool started;

    pthread_mutex_t command_mutex;

    // This is a circular buffer, could generalize it.
    int num_apparent_start_times;
    int next_apparent_start_time;
    uint32_t apparent_start_times[NUM_START_TIMES];
    uint32_t apparent_start_time;
} BeepPlayerRemote;


#define MAX_GROUP_SIZE 50

typedef struct {
    BeepPlayer _;

    // TODO: convert players to a SET
    BeepStaticVector* players;  // Contains BeepPlayer*
    SET devices;

    pthread_mutex_t mutex;
    int locked;

    SongStartedCb song_started_cb;
    void* user_data;

    uint32_t tracks_started;
} BeepPlayerGroup;

BeepPlayer* beep_player_local_init(SongStartedCb song_started_cb, int32_t gain,
                                   void* user_data);
void beep_player_local_shutdown_wait(BeepPlayer *player);

int beep_player_local_prepare_sync_to(BeepPlayer *player, bool start);
int beep_player_local_sync_state_send(BeepPlayer *player, uint8_t *buf, uint32_t *len);
int beep_player_local_prepare_sync_wait(BeepPlayer *player, bool start);
int beep_player_local_sync_state_recv(BeepPlayer *player, uint8_t *buf, uint32_t len);

#endif  // BEEP_PLAYER_H
