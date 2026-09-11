/*
 * Copyright 2013 Beep.
 *
 * This module is a c version on Squeezeplay's src/audio/decode.c.
 */

#include <byteswap.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "audio/audio.h"
#include "audio/mqueue.h"
#include "audio/fifo.h"
#include "audio/streambuf.h"
#include "audio/decode/decode.h"
#include "audio/decode/decode_priv.h"

// Note: DECODE_MAX_INTERVAL was 500 before, but that meant we'd wait up
// to 500ms before starting to decode data after starting a new stream.
// It'd be much better for performance to trigger the decode loop when
// someone wants to start a stream, rather than polling with this. But for
// now we just lower the interval to 50ms.
#define DECODE_MAX_INTERVAL 500
#define DECODE_WAIT_INTERVAL 100

#define DECODE_MQUEUE_SIZE 512

#define DECODE_METADATA_SIZE 128

// in use: DECODE_RESUME_DECODER_OP DECODE_START_OP DECODE_ST_END_OP
#define AUDIO_OP_NULL               0
#define AUDIO_OP_MAX                127
#define AUDIO_OP_THIS_ENDIAN        (1<<31)
#define DECODE_RESUME_DECODER_OP    1
#define DECODE_STOP_OP              2
#define DECODE_FLUSH_OP             3
#define AUDIO_PAUSE_AUDIO_OP        4
#define DECODE_RESUME_AUDIO_OP      5
#define DECODE_START_OP             6
#define DECODE_ST_END_OP            7
#define DECODE_SKIP_AHEAD_OP        8

/* loggers */
LOG_CATEGORY *log_audio_decode;
LOG_CATEGORY *log_audio_codec;
LOG_CATEGORY *log_audio_output;

// Whenever a flush or stop is requested this should be set to a new value.
// The client can use this to discard stale state updates.
static int play_cookie = -1;

/* decoder thread */
pthread_t decode_thread;

// Currently only used for making audio_decoder_flush synchronous
pthread_mutex_t thread_sync_mutex;
pthread_cond_t thread_sync_cond;


/* current decoder state */
uint32_t current_decoder_state = 0;


/* state variables for the current track */
bool decode_first_buffer = false;


/* decoder fifo used to store decoded samples */
uint8_t *decode_fifo_buf;

/* decoder mqueue */
struct mqueue decode_mqueue;
static uint32_t decode_mqueue_buffer[DECODE_MQUEUE_SIZE / sizeof(uint32_t)];

/* audio instance */
struct decode_audio *decode_audio;

/* decoder instance */
static struct decode_module *decoder;
static void *decoder_data;


/* installed decoders */
static struct decode_module *all_decoders[] = {
    /* in order of preference */
    &decode_mad,
    &decode_pcm,
    &decode_test,
    &decode_aac,
    &decode_vorbis,
    &decode_flac,
};

enum {
    SYNC_IDLE,
    SYNC_READY_SYNC_TO,
    SYNC_SEND_AUDIO_STATE,
    SYNC_SEND_SB_STATE,
    SYNC_SEND_SB_DATA,
    SYNC_SEND_DONE,
    SYNC_SEND_ERROR,
    SYNC_READY_SYNC_WAIT,
    SYNC_RECV_AUDIO_STATE,
    SYNC_RECV_SB_STATE,
    SYNC_RECV_SB_DATA,
    SYNC_RECV_DONE,
    SYNC_RECV_ERROR
};

static int sync_state = SYNC_IDLE;
static bool restore_state_on_resume = false;
static uint64_t current_track_discarded_samples = 0;

// Begin sync data.
static uint64_t current_track_jiffies = 0;
static uint64_t sync_discarded_samples = 0;
// End sync data.

// TODO: Need to get this through a defined interface.
extern size_t discarded_samples_offset;

static mqueue_func_t decode_audio_op(uint32_t code, uint16_t *size, bool *swap);

static bool audio_decode_timer_interval(uint32_t *delay) {
    size_t free_bytes, max_samples;
    uint32_t sample_rate;

    // Shawn added: streambuf_is_streaming clause. This fixes a cpu burning
    // bug where we'd continue to return delay = 0 after a track finished
    // decoding. We could also ->stop the decoder when it has reached the
    // end of a song so that we stop trying to run it.
    if (!decoder
            || (!streambuf_is_streaming() && (current_decoder_state & DECODE_STATE_UNDERRUN))
            || (current_decoder_state & (DECODE_STATE_RUNNING|DECODE_STATE_ERROR)) != DECODE_STATE_RUNNING) {
        *delay = DECODE_MAX_INTERVAL;

        return false;
    }

    // If the decoder has underrun while streaming delay a bit before
    // trying again.
    if (streambuf_is_streaming()
            && (current_decoder_state & DECODE_STATE_UNDERRUN)) {
        *delay = DECODE_WAIT_INTERVAL;
        return true;
    }

    // This breaks with inline commands since it does not allow the decoder to
    // read from streambuf unless there is 512 bytes in the stream.  Right now
    // mad will accept (0,2890).  If flac really does need 25000 bytes at once
    // before it can start this will need to be something like
    // if (bytes_from_start() >= DECODE_MINIMUM_BYTES_FLAC).
    // Disabling for until it can be investigated further.  For now gate
    // can_decode if there is data in streambuf that can be read right now.
    // Note: This will also gate against streambuf_streaming.  This is a bad
    // design.
    // Never mind that streambuf_would_wait_for will always return false if
    // streambuf_streaming is false (i.e. a close command has been sent).
    if (streambuf_would_wait_for(1)) {
        *delay = DECODE_WAIT_INTERVAL;
        return false;
    }
    ///* Small delay if the stream empty but still streaming? */
    ///* special case for flac as it has a minimum number of bytes before the decoder processes anything */
    ////if (streambuf_would_wait_for(decoder == &decode_flac ? DECODE_MINIMUM_BYTES_FLAC : DECODE_MINIMUM_BYTES_OTHER)) {
    //if (streambuf_would_wait_for(DECODE_MINIMUM_BYTES_OTHER)) {
    //    *delay = DECODE_WAIT_INTERVAL;
    //    // I want to find out when and why this would happen.  The decoder has
    //    // its own buffer and should not have any requirements on can_decode
    //    // other than if there is data in the buffer.
    //    //
    //    // With the old implementation: streambuf_read does not wrap at the
    //    // end of the buffer.  If the end of the song does wrap there could
    //    // be a case where can_decode will be false with the decoder is
    //    // waiting to get the rest of the last frame.
    //    // With the new implementation: commands injected in the stream could
    //    // even be at frame boundaries but a similiar case as above just with
    //    // commands instead of wrap arounds.
    //    if (streambuf_get_data_usedbytes()) {
    //        // assert if buffer 0 < data < DECODE_MINIMUM_BYTES_OTHER (512 bytes).
    //        LOG_WARN(log_audio_decode, "0 < streambuf (%d) < %d",
    //            streambuf_get_data_usedbytes(), DECODE_MINIMUM_BYTES_OTHER);
    //        //assert(false);
    //    }
    //    return false;
    //}

    if (decoder == &decode_flac
            && streambuf_would_wait_for(DECODE_MINIMUM_BYTES_FLAC)) {
        *delay = DECODE_WAIT_INTERVAL;
        return false;
    }

    /* Variable delay based on output buffer fullness */
    max_samples = decoder->samples(decoder_data);

    decode_audio_lock();
    //state = decode_audio->state;
    sample_rate = decode_audio->track_sample_rate;
    free_bytes = fifo_bytes_free(&decode_audio->fifo);
    //used_bytes = fifo_bytes_used(&decode_audio->fifo);
    decode_audio_unlock();

    if (SAMPLES_TO_BYTES(max_samples) < free_bytes) {
        *delay = 0;

        return true;
    } else {
        *delay = ((max_samples * 1000) / sample_rate) + 1; /* ms */

        /* don't decode for every buffer, do it every other one */
        *delay *= 2;

        return false;
    }
}

static void print_decode_debug(void) {
    size_t decode_size, decode_used;
    size_t output_used, output_size;
    double dbuf, obuf;
    uint64_t elapsed;
    uint32_t bytesl, bytesh;

    decode_audio_lock();
    output_used = fifo_bytes_used(&decode_audio->fifo);
    output_size = decode_audio->fifo.size;

    if (decode_audio->track_sample_rate) {
        elapsed = decode_audio->written_track_samples;
        elapsed = (elapsed * 1000) / decode_audio->track_sample_rate;
    } else {
        elapsed = 0;
    }
    decode_audio_unlock();

    streambuf_get_status(&decode_size, &decode_used, &bytesl, &bytesh);

    dbuf = (decode_used * 100) / (double)decode_size;
    obuf = (output_used * 100) / (double)output_size;

    LOG_DEBUG(log_audio_decode, "elapsed:%llu buffers: %0.1f%%/%0.1f%%",
        (long long unsigned int)elapsed, dbuf, obuf);
}

void* audio_decode_thread_execute(void *unused) {
    int decode_debug;

    LOG_DEBUG(log_audio_decode, "decode_thread_execute");

    decode_debug = getenv("BEEP_DECODE_DEBUG") != NULL;

    while (true) {
        mqueue_func_t handler;
        uint32_t encoded_op;
        uint32_t delay;
        bool can_decode;

        decode_play_check_pids();

        can_decode = audio_decode_timer_interval(&delay);
        while ((encoded_op = mqueue_read_request(&decode_mqueue, delay))) {
            // for debugging race conditions
            //sleep(2);
            bool swap;
            handler = decode_audio_op(encoded_op, NULL, &swap);
            LOG_TRACE(log_audio_decode, "exec handler: %p", handler);
            handler(swap);

            can_decode = audio_decode_timer_interval(&delay);
        }

        if ((encoded_op = streambuf_cmd_ready())) {
            uint16_t cmd_size;
            uint8_t cmd_data[128];
            decode_audio_op(encoded_op, &cmd_size, NULL);
            if ((cmd_size > sizeof(cmd_data)) ||
                (!streambuf_cmd_deq(cmd_data, cmd_size))) {
                LOG_ERROR(log_audio_decode, "bad inline cmd_size: %u\n",
                    cmd_size);
                // Do not try to recover at this time.
                continue;
            } else {
                LOG_DEBUG(log_audio_decode, "deq inline cmd_size: %u\n",
                    cmd_size);
            }
            streambuf_discard(cmd_size, false);
            if (mqueue_write_request(&decode_mqueue, encoded_op, cmd_size)) {
                cmd_size -= sizeof(uint32_t);
                if (cmd_size) {
                    mqueue_write_array(&decode_mqueue,
                        cmd_data + sizeof(uint32_t), cmd_size);
                }
                mqueue_write_complete(&decode_mqueue);
            } else {
                LOG_ERROR(log_audio_decode,
                    "insufficient space in decode_mqueue inline cmd was dropped");
                assert(0);
            }
        } else if (can_decode && decoder
            && (current_decoder_state & DECODE_STATE_RUNNING)) {
            decoder->callback(decoder_data);

            /* Additional debugging enabled with an environment
             * variable, used to track decoder performance.
             */
            if (decode_debug) {
                print_decode_debug();
            }
        }
    }

    return 0;
}

static void decode_resume_decoder_handler(bool swap) {
    mqueue_read_complete(&decode_mqueue);

    current_decoder_state = DECODE_STATE_RUNNING;
    LOG_DEBUG(log_audio_decode, "resume_decoder decode state: %x audio state %x", current_decoder_state, decode_audio->state);
}

static void decode_stop_handler(bool swap) {
    play_cookie = mqueue_read_u32(&decode_mqueue);
    mqueue_read_complete(&decode_mqueue);

    LOG_DEBUG(log_audio_decode, "decode_stop_handler");

    decode_audio_lock();

    current_decoder_state = 0;
    decode_audio->state = 0;

    if (decoder) {
        decoder->stop(decoder_data);

        decoder = NULL;
        decoder_data = NULL;
    }

    decode_audio->num_tracks_started = 0;
    decode_audio->skip_ahead_bytes = 0;
    decode_first_buffer = false;
    decode_output_end();

    decode_metadata_clear();

    decode_audio_unlock();
}

static void decode_flush_handler(bool swap) {
    play_cookie = mqueue_read_u32(&decode_mqueue);

    pthread_mutex_lock(&thread_sync_mutex);

    mqueue_read_complete(&decode_mqueue);

    LOG_DEBUG(log_audio_decode, "decode_flush_handler");

    decode_audio_lock();

    decode_first_buffer = false;
    if (decoder && decoder->flush) {
        decoder->flush(decoder_data);
    }
    decode_output_end();

    decode_audio_unlock();

    pthread_cond_signal(&thread_sync_cond);
    pthread_mutex_unlock(&thread_sync_mutex);
}

static void audio_pause_audio_handler(bool swap) {
    uint32_t interval;

    interval = mqueue_read_u32(&decode_mqueue);
    mqueue_read_complete(&decode_mqueue);

    LOG_DEBUG(log_audio_decode, "decode_pause_handler interval=%d", interval);

    decode_audio_lock();

    if (interval) {
        decode_audio->add_silence_ms = interval;
    } else {
        if ((decode_audio->state & DECODE_STATE_RUNNING) != 0) {
            decode_audio->state &= ~DECODE_STATE_RUNNING;
            decode_audio->f->pause();
        }
    }

    decode_audio_unlock();

    LOG_DEBUG(log_audio_decode, "pause_audio decode state: %x audio state %x", current_decoder_state, decode_audio->state);
}

static void decode_resume_audio_handler(bool swap) {
    uint64_t start_jiffies;

    start_jiffies = mqueue_read_u64(&decode_mqueue);
    mqueue_read_complete(&decode_mqueue);

    LOG_DEBUG(log_audio_decode, "start_jiffies=%" PRIu64, start_jiffies);

    decode_audio_lock();

    if (restore_state_on_resume == true) {
        // TODO: 44100 should not be hardcoded as the sample rate.
        uint64_t sync_jiffies = current_track_jiffies +
            (((uint64_t) sync_discarded_samples * 1000ull) / 44100);
        LOG_DEBUG(log_audio_decode, "overriding with sync_jiffies: %" PRIu64,
            sync_jiffies);
        start_jiffies = sync_jiffies;

        // Set sample adjustment in decode_output.
        decode_audio->start_at_elapsed_samples = sync_discarded_samples;
        current_track_discarded_samples = sync_discarded_samples;
        sync_discarded_samples = 0;

        // Restore streambuf state.
        streambuf_restore_state();
        restore_state_on_resume = false;
    } else  {
        current_track_jiffies = start_jiffies;
        current_track_discarded_samples = 0;
    }

    if (((decode_audio->state & (DECODE_STATE_RUNNING | DECODE_STATE_AUTOSTART)) == 0)) {
        decode_audio->start_at_jiffies = start_jiffies;
        decode_audio->state = DECODE_STATE_AUTOSTART;
        decode_audio->f->resume();
    }

    decode_audio_unlock();

    LOG_DEBUG(log_audio_decode, "resume_audio decode state: %x audio state %x", current_decoder_state, decode_audio->state);
}

static void decode_start_handler(bool swap) {
    uint32_t decoder_id, transition_type, transition_period, replay_gain;
    uint32_t output_threshold, polarity_inversion, output_channels;
    uint32_t i, num_params;
    uint8_t params[DECODER_MAX_PARAMS];

    decoder_id = mqueue_read_u32(&decode_mqueue);
    transition_type = mqueue_read_u32(&decode_mqueue);
    transition_period = mqueue_read_u32(&decode_mqueue);
    replay_gain = mqueue_read_u32(&decode_mqueue);
    output_threshold = mqueue_read_u32(&decode_mqueue);
    polarity_inversion = mqueue_read_u32(&decode_mqueue);
    output_channels = mqueue_read_u32(&decode_mqueue);

    num_params = mqueue_read_u32(&decode_mqueue);
    if (num_params > DECODER_MAX_PARAMS) {
        num_params = DECODER_MAX_PARAMS;
    }
    for (i = 0; i < num_params; i++) {
        params[i] = mqueue_read_u8(&decode_mqueue);
    }
    mqueue_read_complete(&decode_mqueue);

    if (decoder) {
        decoder->stop(decoder_data);

        decoder = NULL;
        decoder_data = NULL;
    }

    for (i=0; i<(sizeof(all_decoders)/sizeof(struct decode_module *)); i++) {
        if (all_decoders[i]->id == decoder_id) {
            decoder = all_decoders[i];
            break;
        }
    }

    if (!decoder) {
        LOG_ERROR(log_audio_decode, "unknown decoder %x\n", decoder_id);
        return;
    }

    LOG_INFO(log_audio_decode, "init decoder %s", decoder->name);

    decode_first_buffer = true;
    decode_output_set_transition(transition_type, transition_period);
    decode_output_set_track_gain(replay_gain);
    decode_set_track_polarity_inversion(polarity_inversion);
    decode_set_output_channels(output_channels);

    decoder_data = decoder->start(params, num_params);

    decode_audio_lock();
    decode_audio->output_threshold = output_threshold;
    decode_output_begin();
    decode_audio_unlock();
}

static void decode_st_end_handler(bool swap) {
    size_t stale_bytes, fresh_bytes, free_bytes;

    mqueue_read_complete(&decode_mqueue);

    LOG_DEBUG(log_audio_decode, "decode_st_end_handler");
    streambuf_fifo_debug(&stale_bytes, &fresh_bytes, &free_bytes, NULL, NULL,
        NULL);
    LOG_DEBUG(log_audio_decode, "streambuf info: stale: %zu fresh: %zu free: %zu",
        stale_bytes, fresh_bytes, free_bytes);

    decode_audio_lock();

    streambuf_set_streaming(false);

    decode_audio_unlock();
}

static void decode_skip_ahead_handler(bool swap) {
    uint32_t interval;

    interval = mqueue_read_u32(&decode_mqueue);
    mqueue_read_complete(&decode_mqueue);

    LOG_DEBUG(log_audio_decode, "decode_skip_ahead_handler interval=%d", interval);

    decode_audio_lock();

    decode_audio->skip_ahead_bytes = SAMPLES_TO_BYTES(
            (uint32_t)(((uint64_t) interval *
                        (uint64_t) decode_audio->track_sample_rate)
                       / (1000 * 1000)));

    decode_audio_unlock();
}

static uint32_t encode_audio_op(uint8_t op, uint16_t size) {
    if (op > AUDIO_OP_MAX) {
        LOG_ERROR(log_audio_decode, "out of range op: %d", op);
        return 0;
    }
    return (AUDIO_OP_THIS_ENDIAN | (size << 8) | op);
}

static mqueue_func_t decode_audio_op(uint32_t code, uint16_t *size, bool *swap) {
    bool need_swap = false;
    if ((code & AUDIO_OP_THIS_ENDIAN) == 0) {
        need_swap = true;
        code = bswap_32(code);
    }

    if (swap) {
        *swap = need_swap;
    }
    if (size) {
        *size = (uint16_t)(code >> 8);
    }

    switch (code & 0xff) {

    case DECODE_RESUME_DECODER_OP:
        return decode_resume_decoder_handler;
    case DECODE_STOP_OP:
        return decode_stop_handler;
    case DECODE_FLUSH_OP:
        return decode_flush_handler;
    case AUDIO_PAUSE_AUDIO_OP:
        return audio_pause_audio_handler;
    case DECODE_RESUME_AUDIO_OP:
        return decode_resume_audio_handler;
    case DECODE_START_OP:
        return decode_start_handler;
    case DECODE_ST_END_OP:
        return decode_st_end_handler;
    case DECODE_SKIP_AHEAD_OP:
        return decode_skip_ahead_handler;
    default:
        LOG_ERROR(log_audio_decode, "invalid op: %u", code);
        return NULL;
    }
}

int audio_init() {
    struct decode_audio_func *f = NULL;

    log_audio_decode = LOG_CATEGORY_GET("audio.decode");
    log_audio_codec = LOG_CATEGORY_GET("audio.codec");
    log_audio_output = LOG_CATEGORY_GET("audio.output");

    log_category_set_priority(log_audio_decode, LOG_PRIORITY_DEBUG);
    log_category_set_priority(log_audio_codec, LOG_PRIORITY_DEBUG);
    log_category_set_priority(log_audio_output, LOG_PRIORITY_DEBUG);

    streambuf_init();

    if (decode_audio || decode_thread) {
        /* already initialized */
        return 0;
    }

    /* initialise audio output */
    f = &decode_play;

    /* audio initialization */
    if (f->init() != 0) {
        /* audio init failed */
        return -1;
    }

    assert(decode_audio);
    assert(decode_fifo_buf);

    // Gain to 1.0
    decode_audio->lgain = DEFAULT_VOLUME;
    decode_audio->rgain = DEFAULT_VOLUME;

    decode_audio->f = f;

    decode_metadata_init();

    /* start decoder thread */
    mqueue_init(&decode_mqueue, decode_mqueue_buffer, sizeof(decode_mqueue_buffer));

    pthread_mutex_init(&thread_sync_mutex, NULL);
    pthread_cond_init(&thread_sync_cond, NULL);

    //decode_thread = SDL_CreateThread(decode_thread_execute, NULL);
    pthread_create(&decode_thread, NULL, audio_decode_thread_execute, NULL);

    return 0;
}

int audio_decoder_resume() {

    /* stack is:
     * 1: self
     */

    LOG_DEBUG(log_audio_decode, "decode_resume_decoder");

#if 0
    if (mqueue_write_request(&decode_mqueue, decode_resume_decoder_handler, 0)) {
        mqueue_write_complete(&decode_mqueue);
    } else {
        LOG_DEBUG(log_audio_decode, "Full message queue, dropped resume message");
    }
#else
    struct {
        uint32_t encoded_op;
    } __attribute__((packed)) resume_message = {
        encode_audio_op(DECODE_RESUME_DECODER_OP, sizeof(resume_message))
        };
    // TODO: ignoring ret val.
    streambuf_cmd_enq((uint8_t *)&resume_message, sizeof(resume_message));
#endif

    return 0;
}

void audio_wakeup_decode_thread(void) {
    fifo_lock(&decode_mqueue.fifo);
    fifo_signal(&decode_mqueue.fifo);
    fifo_unlock(&decode_mqueue.fifo);
}

int audio_decoder_stop(int play_cookie) {
    uint32_t encoded_op = encode_audio_op(DECODE_STOP_OP, 0);
    LOG_DEBUG(log_audio_decode, "decode_stop");

#if 1
    // DECODE_STATE_STOPPING is not used anywhere.
    // TODO: mqueue_write_request len should not be 0.  It should always
    // be >= 4.
    if (mqueue_write_request(&decode_mqueue, encoded_op, 0)) {
        decode_audio_lock();
        decode_audio->state |= DECODE_STATE_STOPPING;
        decode_audio_unlock();

        mqueue_write_u32(&decode_mqueue, play_cookie);
        mqueue_write_complete(&decode_mqueue);
    } else {
        LOG_DEBUG(log_audio_decode, "Full message queue, dropped stop message");
    }
#else
    struct {
        uint32_t encoded_op;
    } __attribute__((packed)) stop_message = {
        encode_audio_op(DECODE_STOP_OP, sizeof(stop_message))
        };
    // TODO: ignoring ret val.
    streambuf_cmd_enq((uint8_t *)&stop_message, sizeof(stop_message));
#endif

    return 0;
}

int audio_decoder_flush(int play_cookie) {
    uint32_t encoded_op = encode_audio_op(DECODE_FLUSH_OP, 0);
    LOG_DEBUG(log_audio_decode, "decode_flush");

    if (mqueue_write_request(&decode_mqueue, encoded_op, 0)) {
        mqueue_write_u32(&decode_mqueue, play_cookie);
        pthread_mutex_lock(&thread_sync_mutex);
        mqueue_write_complete(&decode_mqueue);
    } else {
        LOG_ERROR(log_audio_decode, "Full message queue, dropped flush message");
        abort();
    }

    pthread_cond_wait(&thread_sync_cond, &thread_sync_mutex);
    pthread_mutex_unlock(&thread_sync_mutex);

    return 0;
}

int audio_pause(uint32_t interval_ms) {
    uint32_t encoded_op = encode_audio_op(AUDIO_PAUSE_AUDIO_OP, 0);
    LOG_DEBUG(log_audio_decode, "decode_pause_audio interval_ms=%d", interval_ms);

    if (mqueue_write_request(&decode_mqueue, encoded_op, sizeof(uint32_t))) {
        mqueue_write_u32(&decode_mqueue, interval_ms);
        mqueue_write_complete(&decode_mqueue);
    } else {
        LOG_DEBUG(log_audio_decode, "Full message queue, dropped pause message");
    }

    return 0;
}

int audio_resume(uint64_t start_jiffies) {
    uint32_t encoded_op = encode_audio_op(DECODE_RESUME_AUDIO_OP, 0);
    LOG_DEBUG(log_audio_decode, "decode_resume_audio start_jiffies=%" PRIu64, start_jiffies);

    if (mqueue_write_request(&decode_mqueue, encoded_op, sizeof(uint32_t))) {
        mqueue_write_u64(&decode_mqueue, start_jiffies);
        mqueue_write_complete(&decode_mqueue);
    } else {
        LOG_DEBUG(log_audio_decode, "Full message queue, dropped resume message");
    }

    return 0;
}

int audio_decoder_start(uint32_t decoder,
                        uint32_t transition_type,
                        uint32_t transition_period,
                        uint32_t replay_gain,
                        uint32_t output_threshold,
                        uint32_t polarity_inversion,
                        uint32_t output_channels) {
    LOG_DEBUG(log_audio_decode, "decode_start");

    /* stack is:
     * 1: self
     * 2: decoder
     * 3: transition_type
     * 4: transition_period
     * 5: reply_gain
     * 6: output_threshold
     * 7: polarity_inversion
     * 8: output_channels
     * 9: params...
     */

    /* Reset the decoder state in calling thread to avoid potential
     * race condition - we may incorrectly report a decoder underrun
     * if we wait till the decoder thread resets it.
     */
    current_decoder_state = 0;

#if 0
    // Place in mqueue right away.
    if (mqueue_write_request(&decode_mqueue, decode_start_handler, 0)) {
        mqueue_write_u32(&decode_mqueue, decoder);
        mqueue_write_u32(&decode_mqueue, transition_type);
        mqueue_write_u32(&decode_mqueue, transition_period);
        mqueue_write_u32(&decode_mqueue, replay_gain);
        mqueue_write_u32(&decode_mqueue, output_threshold);
        mqueue_write_u32(&decode_mqueue, polarity_inversion);
        mqueue_write_u32(&decode_mqueue, output_channels);

        // SHAWN
        mqueue_write_u32(&decode_mqueue, 0);
        //int num_params = lua_gettop(L) - 8;
        //mqueue_write_u32(&decode_mqueue, num_params);
        //for (i = 0; i < num_params; i++) {
        //    mqueue_write_u8(&decode_mqueue, (uint8_t) luaL_optinteger(L, 9 + i, 0));
        //}
        mqueue_write_complete(&decode_mqueue);
    } else {
        LOG_DEBUG(log_audio_decode, "Full message queue, dropped start message");
    }
#else
    // Place in streambuf and let audio_decode_thread_execute place in mqueue.
    // Assemble and atomically place in streambuf.
    // NOTE: Not safe to transfer between two devices.  Much less two
    // architectures.
    struct {
        uint32_t encoded_op;
        uint32_t decoder;
        uint32_t transition_type;
        uint32_t transision_period;
        uint32_t replay_gain;
        uint32_t output_threshold;
        uint32_t polarity_inversion;
        uint32_t output_channels;
        uint32_t variables;
    } __attribute__((packed)) start_message = {
        encode_audio_op(DECODE_START_OP, sizeof(start_message)),
        decoder,
        transition_type,
        transition_period,
        replay_gain,
        output_threshold,
        polarity_inversion,
        output_channels,
        0
        };
    // TODO: ignoring ret val.
    streambuf_cmd_enq((uint8_t *)&start_message, sizeof(start_message));
    audio_wakeup_decode_thread();
#endif

    return 0;
}

int audio_decoder_st_end() {
    // stack is:
    // 1: self

    LOG_DEBUG(log_audio_decode, "audio_decoder_st_end");

    struct {
        uint32_t encoded_op;
    } __attribute__((packed)) st_end_message = {
        encode_audio_op(DECODE_ST_END_OP, sizeof(st_end_message))
        };
    // TODO: ignoring ret val.
    streambuf_cmd_enq((uint8_t *)&st_end_message, sizeof(st_end_message));

    return 0;
}

int audio_skip_ahead(uint32_t interval_us) {
    uint32_t encoded_op = encode_audio_op(DECODE_SKIP_AHEAD_OP, 0);
    LOG_DEBUG(log_audio_decode, "decode_skip_ahead interval_us=%d", interval_us);

    if (mqueue_write_request(&decode_mqueue, encoded_op, sizeof(uint32_t))) {
        mqueue_write_u32(&decode_mqueue, interval_us);
        mqueue_write_complete(&decode_mqueue);
    } else {
        LOG_DEBUG(log_audio_decode, "Full message queue, dropped skip_ahead message");
    }

    return 0;
}

static size_t audio_sync_state_read(uint8_t *buf, size_t size) {
    size_t ret = sizeof(current_track_jiffies) +
        sizeof(sync_discarded_samples);

    ASSERT_AUDIO_LOCKED();

    if (size >= ret) {
        memcpy(buf, &current_track_jiffies, sizeof(current_track_jiffies));
        memcpy(buf + sizeof(current_track_jiffies), &sync_discarded_samples,
            sizeof(sync_discarded_samples));
        LOG_DEBUG(log_audio_decode,
            "send audio state current_track_jiffies: %" PRIu64
            " sync_discarded_samples: %" PRIu64,
            current_track_jiffies, sync_discarded_samples);
    } else {
        LOG_ERROR(log_audio_decode, "invalid size");
        ret = 0;
    }

    return ret;
}

static size_t audio_sync_state_write(uint8_t *buf, size_t size) {
    size_t ret = sizeof(current_track_jiffies) +
        sizeof(sync_discarded_samples);

    ASSERT_AUDIO_LOCKED();

    if (size == ret) {
        memcpy(&current_track_jiffies, buf, sizeof(current_track_jiffies));
        memcpy(&sync_discarded_samples, buf + sizeof(current_track_jiffies),
            sizeof(sync_discarded_samples));
        LOG_DEBUG(log_audio_decode,
            "send audio state current_track_jiffies: %" PRIu64
            "sync_discarded_samples %" PRIu64,
            current_track_jiffies, sync_discarded_samples);
    } else {
        LOG_ERROR(log_audio_decode, "invalid size");
        ret = 0;
    }

    return ret;
}

// 0 = ok/done, 1 more, -1 error.
int audio_prepare_sync_to(bool start) {
    decode_audio_lock();

    LOG_DEBUG(log_audio_decode, "start: %d", start);

    streambuf_lock_state(start);

    // TODO: need to wait for mqueue to be empty before locking state.
    // Currently there is no way to do that without altering mqueue, would be
    // a rare case and probably don't need to worry about it now.
    if (start) {
        sync_discarded_samples = current_track_discarded_samples;
        sync_discarded_samples += *(uint64_t *)((uint8_t *)(decoder_data) +
            discarded_samples_offset) + 1152;
        LOG_DEBUG(log_audio_decode,
            "current_track_discarded_samples: %" PRIu64
            "sync_discarded_samples: %" PRIu64,
            current_track_discarded_samples, sync_discarded_samples);
        sync_state = SYNC_READY_SYNC_TO;
    } else {
        sync_discarded_samples = 0;
        sync_state = SYNC_IDLE;
    }

    decode_audio_unlock();

    return 0;
}

int audio_sync_state_send(uint8_t *buf, uint32_t *len) {
    static size_t sync_offset = 0;
    // uint32_t <-> size_t conversion done here.  This should always be safe
    // as long as sizeof(size_t) >= sizeof(uint32_t) since we can never read out
    // more than UINT32_MAX.
    size_t size;
    int ret = -1;

    if (*len == 0 || buf == NULL) {
        return ret;
    }

    decode_audio_lock();

    switch (sync_state) {

    case SYNC_READY_SYNC_TO:
        sync_state = SYNC_SEND_AUDIO_STATE;
        sync_offset = 0;
        // fall through

    case SYNC_SEND_AUDIO_STATE:
        ret = 1;  // again.
        size = audio_sync_state_read(buf, *len);
        if (size == 0) {
            ret = -1;
        }
        *len = size;
        LOG_DEBUG(log_audio_decode, "send_audio_state done sent %d bytes", *len);
        sync_state = SYNC_SEND_SB_STATE;
        // Send audio state as one packet.
        break;

    case SYNC_SEND_SB_STATE:
        ret = 1;  // again.
        size = streambuf_sync_read_state(buf, *len);
        if (size == 0) {
            ret = -1;
        }
        *len = size;
        LOG_DEBUG(log_audio_decode, "send_sb_state done sent %d bytes", *len);
        sync_state = SYNC_SEND_SB_DATA;
        // Send streambuf state as one packet.
        break;

    case SYNC_SEND_SB_DATA:
        ret = 1;  // again.
        size = streambuf_sync_read_data(buf, sync_offset, *len);
        sync_offset += size;
        if (size == *len) {
            // buf is full, send, call again.
            *len = size;
            break;
        } else if (size != *len) {
            // buf not full so we're done.  If the last called filled buf and
            // completely drained streambuf data size may be 0 and caller will
            // have to handle it.
            ret = 0;  // done.
            *len = size;
            sync_state = SYNC_SEND_DONE;
            LOG_DEBUG(log_audio_decode, "send_sb_data done sent %zu bytes",
                sync_offset);
            break;
        }

    case SYNC_SEND_DONE:
        *len = 0;
        ret = -1;  // error.  we're done.
        break;

    case SYNC_SEND_ERROR:
    default:
        *len = 0;
        ret = -1;  // error. bad state.
        break;
    }

    decode_audio_unlock();

    return ret;
}

int audio_prepare_sync_wait(bool start) {
    decode_audio_lock();

    LOG_DEBUG(log_audio_decode, "start: %d", start);

    streambuf_lock_state(start);

    // TODO: need to wait for mqueue to be empty before locking state.
    // Currently there is no way to do that without altering mqueue, would be
    // a rare case and probably don't need to worry about it now.
    if (start) {
        sync_state = SYNC_READY_SYNC_WAIT;
    } else {
        sync_state = SYNC_IDLE;
    }

    decode_audio_unlock();

    return 0;
}

int audio_sync_state_recv(uint8_t *buf, uint32_t len) {
    static size_t sync_offset = 0;
    static size_t sync_data_len = 0;
    size_t size;
    int ret = -1;

    if (len == 0 || buf == NULL) {
        return ret;
    }

    decode_audio_lock();

    switch (sync_state) {

    case SYNC_READY_SYNC_WAIT:
        sync_state = SYNC_RECV_AUDIO_STATE;
        sync_offset = 0;
        sync_data_len = 0;
        // fall through

    case SYNC_RECV_AUDIO_STATE:
        ret = 1;  // again.
        size = audio_sync_state_write(buf, len);
        if (size == 0) {
            ret = -1;
        }
        LOG_DEBUG(log_audio_decode, "recv_audio_state done with %zu bytes",
                size);
        sync_state = SYNC_RECV_SB_STATE;
        break;

    case SYNC_RECV_SB_STATE:
        ret = 1;  // again.
        sync_data_len = streambuf_sync_write_state(buf, len);
        if (sync_data_len == 0) {
            ret = -1;
        }
        LOG_DEBUG(log_audio_decode,
            "recv_sb_state done with %u bytes expecting %zu data bytes",
            len, sync_data_len);
        restore_state_on_resume = true;
        sync_state = SYNC_RECV_SB_DATA;
        break;

    case SYNC_RECV_SB_DATA:
        ret = 1;  // again.
        if (streambuf_sync_write_data(buf, len) == 0) {
            ret = -1;
            break;
        }
        if (sync_offset == 0) {
            LOG_TRACE(log_audio_decode,
                "recv_sb_data start data: %02x%02x%02x%02x%02x%02x%02x%02x "
                "%02x%02x%02x%02x%02x%02x%02x%02x",
                buf[0], buf[1], buf[2], buf[3],
                buf[4], buf[5], buf[6], buf[7],
                buf[8], buf[9], buf[10], buf[11],
                buf[12], buf[13], buf[14], buf[15]);
        }
        sync_offset += len;
        if ((sync_data_len - sync_offset) == 0) {
            LOG_DEBUG(log_audio_decode, "recv_sb_data done recv %zu bytes",
                sync_data_len);
            ret = 0;  // done.
            sync_state = SYNC_RECV_DONE;
        }
        break;

    case SYNC_RECV_DONE:
        ret = -1;  // error.  we're done.
        break;

    case SYNC_RECV_ERROR:
    default:
        ret = -1;  // error. bad state.
        break;
    }

    decode_audio_unlock();

    return ret;
}

BeepAudioStatus audio_status() {
    size_t size, usedbytes;
    uint32_t bytesL, bytesH;
    uint64_t elapsed, output, elapsed_jiffies;

    BeepAudioStatus status;

    assert(decode_audio);

    decode_audio_lock();

    status.output_used = fifo_bytes_used(&decode_audio->fifo);
    status.output_size = decode_audio->fifo.size;

    if (decode_audio->track_sample_rate) {
        output = fifo_bytes_used(&decode_audio->fifo);
        output = (BYTES_TO_SAMPLES(output) * 1000) / decode_audio->track_sample_rate;
    } else {
        output = 0;
    }
    status.output_time = output;

    elapsed_jiffies = beep_millis();

    if (decode_audio->track_sample_rate
            && decode_audio->sync_played_samples) {
        elapsed = (decode_audio->sync_played_samples * 1000)
            / decode_audio->track_sample_rate;

        if ((decode_audio->state & DECODE_STATE_RUNNING) &&
            decode_audio->sync_timestamp &&
            elapsed_jiffies > decode_audio->sync_timestamp)
        {
            elapsed += (elapsed_jiffies - decode_audio->sync_timestamp);
        }
    } else {
        elapsed = 0;
    }

    status.written_track_time = 0;
    if (decode_audio->track_sample_rate) {
        status.written_track_time =
            ((uint64_t) decode_audio->written_track_samples * 1000)
            / decode_audio->track_sample_rate;
    }

    status.sync_played_time = elapsed;
    status.sync_timestamp = elapsed_jiffies;
    //LOG_TEST(log_audio_decode, "elapsed: %u elapsed_jiffies: %u diff: %u",
    //    status.elapsed, status.elapsed_jiffies,
    //    status.elapsed_jiffies - status.elapsed);
    status.num_tracks_started = decode_audio->num_tracks_started;

    status.decoder_id = -1;
    if (decoder) {
        status.decoder_id = decoder->id;
    }

    status.audio_state = decode_audio->state;
    status.play_cookie = play_cookie;

    decode_audio_unlock();

    streambuf_get_status(&size, &usedbytes, &bytesL, &bytesH);

    status.streambuf_size = size;
    status.streambuf_used = usedbytes;
    status.streambuf_bytes_received_l = bytesL;
    status.streambuf_bytes_received_h = bytesH;

    status.decode_state = current_decoder_state;

    status.gain = decode_audio->lgain;

    decode_metadata_lock();
    struct decode_metadata* metadata = decode_metadata_read();
    status.bitrate = metadata->bitrate;
    decode_metadata_unlock();

    return status;
}

int audio_gain(int32_t gain) {
    if (gain < 0 || gain > (1 << 16)) {
        return 0;
    }

    if (decode_audio) {
        decode_audio_lock();
        decode_audio->lgain = gain;
        decode_audio->rgain = gain;
        decode_audio_unlock();
    }

    return 0;
}

int audio_adjust_gain(int32_t gain_delta) {
    if (gain_delta < -(1 << 16) || gain_delta > (1 << 16)) {
        return 0;
    }

    if (decode_audio) {
        decode_audio_lock();

        int32_t new_gain = decode_audio->lgain + gain_delta;
        if (new_gain > (1 << 16)) {
            new_gain = (1 << 16);
        }
        if (new_gain < 0) {
            new_gain = 0;
        }

        decode_audio->lgain = new_gain;
        decode_audio->rgain = new_gain;
        decode_audio_unlock();
    }

    return 1;
}
