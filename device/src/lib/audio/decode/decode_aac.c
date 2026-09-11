#include <string.h>

#include "audio/audio.h"
#include "audio/streambuf.h"
#include "audio/decode/decode.h"
#include "audio/decode/decode_priv.h"

#include <fdk-aac/aacdecoder_lib.h>

/*
 * Max frame size is 2048 samples for HE-AAC; We set fdk-aac to always mix
 * to 2 channels
 */
#define DEC_MAX_SAMPLES (2048)
#define DEC_MAX_CHANNELS (2)
#define DEC_OUTPUT_BYTES \
    (sizeof(INT_PCM) * DEC_MAX_SAMPLES * DEC_MAX_CHANNELS)
#define OUTPUT_BUFFER_BYTES \
    (sizeof(sample_t) * DEC_MAX_SAMPLES * DEC_MAX_CHANNELS)
#define _CONCEAL_METHOD (1)

/*
 * Largest incoming chunks should be 16k, so 24k gives us 50% cushion
 */
#define INPUT_BUFFER_BYTES (1024 * 24)

// #define TRACE_AAC

struct decode_aac {
    /*
     * Ancillary buffer and mutable pointer
     */
    UCHAR *in_buf, *_in_ptr;
    UINT in_buf_length;
    UINT bytes_valid;

    HANDLE_AACDECODER decoder_handle;
    CStreamInfo *stream_info;
    sample_t *out_buf;
    INT_PCM *dec_buf;
};

static bool decode_aac_callback(void *data) {
    struct decode_aac *self = (struct decode_aac *) data;

    if(!self->bytes_valid) { // Buffer is empty consume stuff
#ifdef TRACE_AAC
        LOG_DEBUG(log_audio_codec, "Ancillary input buffer empty; refilling.");
#endif  // TRACE_AAC

        self->in_buf_length = streambuf_read(self->in_buf, 0,
                INPUT_BUFFER_BYTES, NULL);

#ifdef TRACE_AAC
        LOG_DEBUG(log_audio_codec, "Read %d bytes", self->in_buf_length);
#endif  // TRACE_AAC

        streambuf_discard(self->in_buf_length, true);  // true = discard data
        self->bytes_valid = self->in_buf_length;
        self->_in_ptr = self->in_buf;
    }

    /*
     * aacDecoder_Fill fills fdk-aac's internal input buffer and returns
     * the number of bytes remaining in the ancillary input buffer in
     * bytes valid.  This loop repeats while there are still bytes left to
     * consume in the ancillary buffer and aacDecoder_DecodeFrame returns
     * AAC_DEC_NOT_ENOUGH_BITS.  Once aacDecoder_DecodeFrame finds enough
     * data in the internal buffer to produce a frame, it returns with
     * any other error code.
     */
    AAC_DECODER_ERROR err = AAC_DEC_NOT_ENOUGH_BITS;
    while(self->bytes_valid > 0 && err == AAC_DEC_NOT_ENOUGH_BITS) {
        aacDecoder_Fill(self->decoder_handle, &self->_in_ptr,
                &self->in_buf_length, &self->bytes_valid);
        err = aacDecoder_DecodeFrame(self->decoder_handle,
                (INT_PCM *)self->dec_buf, DEC_OUTPUT_BYTES, 0);
    }

    if(err != AAC_DEC_OK) {
        if(err > aac_dec_init_error_start && err < aac_dec_init_error_end) {
            LOG_ERROR(log_audio_codec, "Fatal AAC decode error: %04x", err);
            return false;
        } else {
            LOG_WARN(log_audio_codec, "AAC decode error: %04x", err);
            goto out;
        }
    }

    /*
     * Get stream info but perform sanity check
     */
    self->stream_info = aacDecoder_GetStreamInfo(self->decoder_handle);
    if(self->stream_info->frameSize == 0 || self->stream_info->sampleRate == 0
            || self->stream_info->numChannels == 0) {
        LOG_WARN(log_audio_codec, "Invalid stream info: %d", err);
        goto out;
    }

    /*
     * fdk-aac outputs signed interleaved 16-bit; audio backend expects
     * signed interleaved 16-bit padded to 32-bit per sample:
     * L1R1L2R2L3R3 ... LnRn <-> L100R100L200R200L300R3 ... Ln00Rn00
     */
    int n_samples =
        self->stream_info->numChannels * self->stream_info->frameSize;

    for(int i = 0; i < n_samples; i ++) {
        self->out_buf[i] = self->dec_buf[i] << 16;
    }

    /*
     * Safety check on sample rate
     */
    if(self->stream_info->sampleRate != 44100) {
        LOG_ERROR(log_audio_codec, "Unsupported sample rate (beep backend): %d",
                self->stream_info->sampleRate);
        return false;
    }

    decode_output_samples(self->out_buf, self->stream_info->frameSize,
            self->stream_info->sampleRate);

out:
    return true;
}

static size_t decode_aac_samples(void *data) {
    return DEC_MAX_SAMPLES * DEC_MAX_CHANNELS;
}

static void *decode_aac_start(uint8_t *params, uint32_t num_params) {
    struct decode_aac *self;

    self = calloc(1, sizeof(struct decode_aac));

    self->dec_buf = malloc(DEC_OUTPUT_BYTES);
    self->out_buf = malloc(OUTPUT_BUFFER_BYTES);
    self->in_buf = (UCHAR *)malloc(INPUT_BUFFER_BYTES);

    LOG_DEBUG(log_audio_codec, "Opening AAC decoder");
    self->decoder_handle = aacDecoder_Open(TT_MP4_ADTS, 1);
    if(self->decoder_handle != NULL) {
        self->stream_info = aacDecoder_GetStreamInfo(self->decoder_handle);

        if(self->stream_info == NULL) {
            LOG_WARN(log_audio_codec, "Stream info is not available");
        }
    } else {
        LOG_ERROR(log_audio_codec, "Failed to open decoder");
        exit(-1);
    }

    /*
     * These are defaults but guard against future changes by explicitly
     * setting them.
     */
    aacDecoder_SetParam(self->decoder_handle, AAC_PCM_OUTPUT_INTERLEAVED, 1);
    aacDecoder_SetParam(self->decoder_handle, AAC_PCM_OUTPUT_CHANNELS, 2);
    aacDecoder_SetParam(self->decoder_handle, AAC_PCM_OUTPUT_CHANNEL_MAPPING, 1);
    aacDecoder_SetParam(self->decoder_handle, AAC_CONCEAL_METHOD, _CONCEAL_METHOD);

    return self;
}

static void decode_aac_stop(void *data) {
    struct decode_aac *self = (struct decode_aac *) data;

#ifdef TRACE_AAC
    LOG_DEBUG(log_audio_codec, "decode_aac_stop()");
#endif // TRACE_AAC

    free(self->in_buf);
    free(self->out_buf);
    free(self->dec_buf);
    aacDecoder_Close(self->decoder_handle);
    free(self);
}

struct decode_module decode_aac = {
    'a',
    "aac",
    decode_aac_start,
    decode_aac_stop,
    decode_aac_samples,
    decode_aac_callback,
    NULL  // no flush operation
};
