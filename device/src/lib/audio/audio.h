#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define DEFAULT_VOLUME 52428 // 80%

typedef struct {
    size_t output_used;
    size_t output_size;
    uint32_t output_time;
    uint32_t written_track_time;
    uint32_t sync_played_time;
    uint64_t sync_timestamp;
    uint32_t num_tracks_started;
    uint32_t decoder_id;
    uint32_t audio_state;
    uint32_t decode_state;
    size_t streambuf_size;
    size_t streambuf_used;
    uint32_t streambuf_bytes_received_l;
    uint32_t streambuf_bytes_received_h;
    int32_t gain;
    int32_t play_cookie;
    int32_t bitrate;
} BeepAudioStatus;

int audio_init(void);
void audio_wakeup_decode_thread(void);
int audio_decoder_start(uint32_t decoder,
                        uint32_t transition_type,
                        uint32_t transition_period,
                        uint32_t replay_gain,
                        uint32_t output_threshold,
                        uint32_t polarity_inversion,
                        uint32_t output_channels);
int audio_decoder_st_end(void);
int audio_pause(uint32_t interval_ms);
int audio_resume(uint64_t start_jiffies);
int audio_decoder_resume(void);
int audio_decoder_stop(int32_t play_cookie);
int audio_decoder_flush(int32_t play_cookie);
int audio_skip_ahead(uint32_t interval_us);

int audio_prepare_sync_to(bool start);
int audio_sync_state_send(uint8_t *buf, uint32_t *len);
int audio_prepare_sync_wait(bool start);
int audio_sync_state_recv(uint8_t *buf, uint32_t len);
BeepAudioStatus audio_status(void);
int audio_gain(int32_t gain);
int audio_adjust_gain(int32_t gain_delta);
