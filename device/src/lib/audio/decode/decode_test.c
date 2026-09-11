#include <inttypes.h>
#include <math.h>
#include <string.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "audio/audio.h"
#include "audio/streambuf.h"
#include "audio/decode/decode.h"
#include "audio/decode/decode_priv.h"

struct decode_test {
    uint64_t start_millis;
    sample_t *tbuf;
    uint8_t *drain;
    size_t max_samples;
    size_t tbuf_size;
    size_t drain_size;
    size_t offset;
    size_t recv_bytes;
    int sample_rate;
    int bit_depth;
    bool stereo;
    int tone_freq;
    int period_samples;
};

static void create_tone_pattern(struct decode_test *self) {
    int period_samples = (self->sample_rate / self->tone_freq) *
            (self->stereo ? 2 : 1);
    // round up next number of whole periods plus one for overlap.
    int tbuf_size = ((self->max_samples + (2 * period_samples) - 1) /
            period_samples) * period_samples;
    double mag_f = ((self->bit_depth == 16) ? 32767.5f : 8388607.5f) * 0.8f;
    double angle_f = 2.0f * M_PI / period_samples;
    int i;

    self->tbuf = (sample_t *)malloc(tbuf_size * sizeof(sample_t));
    assert(self->tbuf);
    self->offset = 0;
    self->period_samples = period_samples;
    self->tbuf_size = tbuf_size;

    for (i = 0; i < period_samples; i += (self->stereo ? 2 : 1)) {
        sample_t v = (int16_t)(mag_f * sin(angle_f * (double)i));
        v <<= 16;
        self->tbuf[i] = v;
        if (self->stereo)
            self->tbuf[i + 1] = v;
    }

    for (i = 1; i < (tbuf_size / period_samples); i++) {
        memcpy(self->tbuf + (period_samples * i), self->tbuf,
                period_samples * sizeof(int32_t));
    }
}

static void *decode_test_start(uint8_t *params, uint32_t num_params) {
    struct decode_test *self = (struct decode_test *)malloc(
            sizeof(struct decode_test));
    assert(self);
    memset(self, 0, sizeof(struct decode_test));

    self->drain_size = 16 * 1024;
    self->sample_rate = 44100;
    self->tone_freq = 1000;
    self->max_samples = 4096;
    self->bit_depth = 16;
    self->stereo = true;

    self->drain = (uint8_t *)malloc(self->drain_size);
    assert(self->drain);

    create_tone_pattern(self);

    LOG_INFO(log_audio_codec, "sample_rate: %d bit_depth: %d max_samples: %zu",
            self->sample_rate, self->bit_depth, self->max_samples);
    LOG_INFO(log_audio_codec, "tone_freq: %d drain_size: %zu",
            self->tone_freq, self->drain_size);

    self->start_millis = beep_millis();

    return self;
}

static void decode_test_stop(void *data) {
    struct decode_test *self = (struct decode_test *)data;
    uint64_t stop_millis = beep_millis();
    double rate = ((((double)self->recv_bytes) / (stop_millis -
            self->start_millis)) * 1000) / 1024;

    LOG_INFO(log_audio_codec, "received %zu bytes in %" PRIu64
            " ms (%0.3f kB/s)",
            self->recv_bytes,
            (stop_millis - self->start_millis),
            rate);

    if (self) {
        if (self->tbuf)
            free(self->tbuf);
        if (self->drain)
            free(self->drain);
        free(self);
    }
}

static size_t decode_test_samples(void *data) {
    struct decode_test *self = (struct decode_test *)data;
    return self->max_samples;
}

static bool decode_test_callback(void *data) {
    struct decode_test *self = (struct decode_test *)data;
    size_t deq_size;
    bool done = (streambuf_get_usedbytes() !=
            (streambuf_get_size() - streambuf_get_freebytes() - 1));
    int c = 0;

    current_decoder_state &= ~DECODE_STATE_UNDERRUN;

    while ((streambuf_get_data_usedbytes() > self->drain_size) || done) {
        c++;
        deq_size = streambuf_data_deq(self->drain, 0, self->drain_size, NULL);
        self->recv_bytes += deq_size;
        if (deq_size)
            streambuf_discard(deq_size, true);
        if (deq_size != self->drain_size)
            break;
    }

    decode_output_samples(self->tbuf + self->offset,
            (self->stereo ? self->max_samples / 2 : self->max_samples),
            self->sample_rate);
    self->offset = (self->offset + self->max_samples) % self->period_samples;

    return true;
}

struct decode_module decode_test = {
    't',
    "test",
    decode_test_start,
    decode_test_stop,
    decode_test_samples,
    decode_test_callback
};
