/*
** Copyright 2007-2008 Logitech. All Rights Reserved.
**
** This file is licensed under BSD. Please see the LICENSE file for details.
*/

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "audio/streambuf.h"
#include "audio/decode/decode.h"
#include "audio/decode/decode_priv.h"

#include <mad.h>


/* The input buffer size is at the theoretical maximum frame
 * size - MPEG 2.5 Layer II 8KHz @ 160kbps with padding slot
 */
#define INPUT_BUFFER_SIZE 2890

#define OUTPUT_BUFFER_FRAMES 2304 /* always 1,152 samples per frame */
#define OUTPUT_BUFFER_BYTES (OUTPUT_BUFFER_FRAMES * sizeof(sample_t))

#define ID3_TAG_FLAG_FOOTERPRESENT 0x10


struct decode_mad {
    struct mad_stream stream;
    struct mad_frame frame;
    struct mad_synth synth;

    uint8_t *input_buffer;
    sample_t *output_buffer;
    uint8_t *guard_pointer;

    uint32_t packets;
    uint32_t encoder_delay;
    uint32_t encoder_padding;
    uint64_t lame_samples;
    uint64_t lame_samples_remain;
    uint64_t decoded_samples;

    // used for syncing and verification.
    uint64_t discarded_samples;
    uint64_t bytes_read;
    uint64_t bytes_consumed_total;
    uint64_t bytes_consumed;

    enum {
        MAD_STATE_OK = 0,
        MAD_STATE_PCM_READY,
        MAD_STATE_END_OF_FILE,
        MAD_STATE_ERROR,
    } state;

    uint32_t sample_rate;
};

size_t discarded_samples_offset = offsetof(struct decode_mad, discarded_samples);

#define XING_MAGIC      ( ('X' << 24) | ('i' << 16) | ('n' << 8) | 'g' )
#define INFO_MAGIC      ( ('I' << 24) | ('n' << 16) | ('f' << 8) | 'o' )
#define LAME_MAGIC      ( ('L' << 24) | ('A' << 16) | ('M' << 8) | 'E' )

#define XING_FRAMES     0x01
#define XING_BYTES      0x02
#define XING_TOC        0x04
#define XING_SCALE      0x08

/* Not much documentation exists about the MAD decoder delay, but
   we apparently need to skip the first 529 samples
   http://www.hydrogenaudio.org/forums/lofiversion/index.php/t20083.html

   See also
   http://lame.sourceforge.net/tech-FAQ.txt
*/
#define MAD_DECODER_DELAY 529


static void xing_parse(struct decode_mad *self) {
    struct mad_bitptr ptr = self->stream.anc_ptr;
    unsigned int bitlen = self->stream.anc_bitlen;
    uint32_t magic, flags, frames;

    if (bitlen < 64) {
        LOG_DEBUG(log_audio_codec, "no xing header");
        return;
    }

    magic = mad_bit_read(&ptr, 32);
    LOG_DEBUG(log_audio_codec, "xing magic %x", magic);
    if (magic != XING_MAGIC && magic != INFO_MAGIC) {
        return;
    }

    flags = mad_bit_read(&ptr, 32);
    bitlen -= 64;

    // skip traditional xing vbr tag data
    if (flags & XING_FRAMES) {
        if (bitlen < 32) {
            return;
        }
        frames = mad_bit_read(&ptr, 32);
        bitlen -= 32;
        LOG_DEBUG(log_audio_codec, "xing frames: %d", frames);
    } else {
        LOG_DEBUG(log_audio_codec, "no xing frames available");
        return;
    }

    if (flags & XING_BYTES) {
        if (bitlen < 32) {
            return;
        }
        mad_bit_skip(&ptr, 32);
        bitlen -= 32;
    }
    if (flags & XING_TOC) {
        if (bitlen < 800) {
            return;
        }
        mad_bit_skip(&ptr, 800);
        bitlen -= 800;
    }
    if (flags & XING_SCALE) {
        if (bitlen < 32) {
            return;
        }
        mad_bit_skip(&ptr, 32);
        bitlen -= 32;
    }

    if (bitlen < 72) {
        LOG_DEBUG(log_audio_codec, "no lame header");
        return;
    }

    magic = mad_bit_read(&ptr, 32);
    mad_bit_skip(&ptr, 40);
    bitlen -= 72;

    LOG_DEBUG(log_audio_codec, "lame magic %x bitlen %d", magic, bitlen);
    if (magic != LAME_MAGIC) {
        return;
    }

    if (bitlen < 120) {
        return;
    }

    mad_bit_skip(&ptr, 96);

    self->encoder_delay += mad_bit_read(&ptr, 12);
    self->encoder_padding = mad_bit_read(&ptr, 12);

    /* Remove MAD decoder delay of 529 samples from the end too */
    if (self->encoder_padding > MAD_DECODER_DELAY) {
        self->encoder_padding -= MAD_DECODER_DELAY;
    } else {
        self->encoder_padding = 0;
    }

    self->lame_samples        = frames * 1152ULL;
    self->lame_samples_remain = self->lame_samples - self->encoder_delay - self->encoder_padding;

    LOG_DEBUG(log_audio_codec, "encoder delay=%d padding=%d", self->encoder_delay, self->encoder_padding);
    LOG_DEBUG(log_audio_codec, "total LAME samples %" PRIu64, self->lame_samples);
}


static uint32_t tagtype(const unsigned char *data, uint32_t length) {
        if (length >= 3 && data[0] == 'T' && data[1] == 'A' && data[2] == 'G') {
                LOG_DEBUG(log_audio_codec, "ID3v1 tag detected");
                return 128;
        }

        if (length >= 10 &&
                (data[0] == 'I' && data[1] == 'D' && data[2] == '3') &&
                data[3] < 0xff && data[4] < 0xff &&
                data[6] < 0x80 && data[7] < 0x80 && data[8] < 0x80 && data[9] < 0x80)
        {
                uint32_t size;

        LOG_DEBUG(log_audio_codec, "ID3v2 tag detected");

                size = 10 + (data[6]<<21) + (data[7]<<14) + (data[8]<<7) + data[9];
                if (data[5] & ID3_TAG_FLAG_FOOTERPRESENT) {
                        size += 10;
                }
                for (; size < length && !data[size]; ++size);  /* Consume padding */
                return size;
        }

        return 0;
}


static bool consume_id3_tags(struct decode_mad *self) {
        bool rc = false;
        uint32_t tagsize;
        uint32_t remaining = self->stream.bufend - self->stream.next_frame;

        if ( (tagsize = tagtype(self->stream.this_frame, remaining)) ) {
                LOG_DEBUG(log_audio_codec, "ID3 tag detected, skipping %d bytes before next frame", tagsize);
                mad_stream_skip(&self->stream, tagsize);
                self->bytes_consumed += tagsize;
                rc = true;
        }

        /* We know that a valid frame hasn't been found yet
          * so help libmad out and go back into frame seek mode.
          * This is true whether an ID3 tag was found or not.
          */
        mad_stream_sync(&self->stream);

        return rc;
}


#ifdef HAVE_NULLAUDIO
/*
 * NAME:    synth->frame()
 * DESCRIPTION:    perform PCM synthesis of frame subband samples
 */
void null_synth_frame(struct mad_synth *synth, struct mad_frame const *frame)
{
  unsigned int nch, ns;

  nch = MAD_NCHANNELS(&frame->header);
  ns  = MAD_NSBSAMPLES(&frame->header);

  synth->pcm.samplerate = frame->header.samplerate;
  synth->pcm.channels   = nch;
  synth->pcm.length     = 32 * ns;

  if (frame->options & MAD_OPTION_HALFSAMPLERATE) {
    synth->pcm.samplerate /= 2;
    synth->pcm.length     /= 2;

  }
}
#endif


static void decode_mad_frame(struct decode_mad *self) {
    size_t read_max, read_num, remaining;
    uint8_t *read_start;
    unsigned char const *frame_start;
    bool streaming, will_skip;

    do {
        /* The input stream must be filled if it's the first
         * execution of the loop or it becomes empty
         */
        if (self->stream.buffer == NULL ||
            self->stream.error == MAD_ERROR_BUFLEN) {

            /* If there's data left from the last time,
             * copy it to the beginning of the input buffer
             */
            if (self->stream.next_frame) {
                remaining = self->stream.bufend - self->stream.next_frame;
                memmove(self->input_buffer, self->stream.next_frame, remaining);
            }
            /* Otherwise fill the input buffer */
            else {
                remaining = 0;
            }

            read_start = self->input_buffer + remaining;
            read_max = INPUT_BUFFER_SIZE - remaining;

            read_num = streambuf_read(read_start, 0, read_max, &streaming);
            self->bytes_read += read_num;
            LOG_TRACE(log_audio_codec, "read_num: %zu bytes_read: %" PRIu64,
                read_num, self->bytes_read);

            if (!read_num) {
                current_decoder_state |= DECODE_STATE_UNDERRUN;
                if (streaming) {
                    return;
                }

                /* Mark that we are at the end of the file,
                 * for some reason this is not required on
                 * ip3k, but it is needed in SqueezePlay as
                 * this_frame never reaches the guard_pionter?
                 */
                if (self->guard_pointer) {
                    self->stream.this_frame = self->guard_pointer;
                    self->state = MAD_STATE_PCM_READY;
                    return;
                }

                /* If we're at the end of the input file, write
                 * out the buffer guard.
                 */
                self->guard_pointer = read_start;
                memset(self->guard_pointer, 0, MAD_BUFFER_GUARD);
                read_num = MAD_BUFFER_GUARD;
            } else {
                current_decoder_state &= ~DECODE_STATE_UNDERRUN;
            }

            /* Send the new content to libmad's stream decoder
             */
            mad_stream_buffer(&self->stream,
                      self->input_buffer,
                      read_num + remaining);

            self->stream.error = MAD_ERROR_NONE;
        }

        frame_start = self->stream.this_frame;
        will_skip = (self->stream.skiplen != 0);
        LOG_TRACE(log_audio_codec,
            "this_frame: %p next_frame: %p frame_start: %p skiplen: %lu",
            self->stream.this_frame, self->stream.next_frame, frame_start,
            self->stream.skiplen);
        LOG_TRACE(log_audio_decode,
            "frame_start: %02x%02x%02x%02x%02x%02x%02x%02x "
            "%02x%02x%02x%02x%02x%02x%02x%02x",
            frame_start[0], frame_start[1], frame_start[2], frame_start[3],
            frame_start[4], frame_start[5], frame_start[6], frame_start[7],
            frame_start[8], frame_start[9], frame_start[10], frame_start[11],
            frame_start[12], frame_start[13], frame_start[14], frame_start[15]);
        if (mad_frame_decode(&self->frame, &self->stream)) {
            LOG_TRACE(log_audio_codec,
                "this_frame: %p next_frame: %p frame_start: %p skiplen: %lu",
                self->stream.this_frame, self->stream.next_frame, frame_start,
                self->stream.skiplen);
            if (((self->stream.error == MAD_ERROR_BUFLEN) && (will_skip == 0)) ||
                (self->stream.error == MAD_ERROR_LOSTSYNC)) {
                // After the call to mad_frame_decode need to track other
                // bytes consumed.  consume_id3_tags will set
                // self->stream.skiplen.  LOSTSYNC will for the first frame
                // after i3d meta data.
                // TODO: Investigate how corrupted frames are tracked.
                // mad_stream_sync may be helpful in incrementing the pointers
                // ourselves.
                LOG_TRACE(log_audio_codec,
                    "bytes_consumed += %zd mad_error: %04x will_skip: %d"
                    " decoded_samples: %" PRIu64,
                    self->stream.this_frame - frame_start, self->stream.error,
                    will_skip, self->decoded_samples);
                self->bytes_consumed += self->stream.this_frame - frame_start;
            } else {
                // Limit logging level if we are actually dropping bytes
                // rather than if mad_frame_decode without dropping bytes
                // since they occur naturally at song transitions.
                if ((self->stream.this_frame - frame_start) != 0) {
                    LOG_ERROR(log_audio_codec,
                        "ignore bytes: %zd mad_error: %04x will_skip: %d"
                        " decoded_samples: %" PRIu64,
                        self->stream.this_frame - frame_start,
                        self->stream.error,
                        will_skip,
                        self->decoded_samples);
                } else {
                    LOG_TRACE(log_audio_codec,
                        "ignore bytes: %zd mad_error: %04x will_skip: %d"
                        " decoded_samples: %" PRIu64,
                        self->stream.this_frame - frame_start, self->stream.error,
                        will_skip, self->decoded_samples);
                }
            }

            if (MAD_RECOVERABLE(self->stream.error)) {
                if (consume_id3_tags(self)) {
                    continue;
                }

                if (self->stream.error != MAD_ERROR_LOSTSYNC ||
                    self->stream.this_frame != self->guard_pointer) {
                    continue;
                }
            } else {
                // Need more data, try again.
                if (self->stream.error == MAD_ERROR_BUFLEN) {
                    continue;
                }

                // XXXX unrecoverable error
                LOG_ERROR(log_audio_codec, "Unrecoverable frame error %d",
                    self->stream.error);
                self->state = MAD_STATE_ERROR;
                current_decoder_state |= DECODE_STATE_ERROR;
                return;
            }
        } else {
            LOG_TRACE(log_audio_codec,
                    "this_frame: %p next_frame: %p frame_start: %p skiplen: %lu",
                    self->stream.this_frame, self->stream.next_frame, frame_start,
                    self->stream.skiplen);
            if (will_skip == 0) {
                LOG_TRACE(log_audio_codec,
                        "bytes_consumed += %zd decoded_samples: %" PRIu64,
                        self->stream.this_frame - frame_start,
                        self->decoded_samples);
                self->bytes_consumed += self->stream.this_frame - frame_start;
            }
        }

#ifdef HAVE_NULLAUDIO
        null_synth_frame(&self->synth, &self->frame);
#else
        mad_synth_frame(&self->synth, &self->frame);
#endif

        /* pcm is now ready  */
        self->state = MAD_STATE_PCM_READY;
    } while (0);
}


static inline sample_t mad_fixed_to_32bit(mad_fixed_t fixed)
{
    fixed += (1L << (MAD_F_FRACBITS - 24));

    /* Clipping */
    if(fixed >= MAD_F_ONE)
        fixed = MAD_F_ONE - 1;
    if(fixed <= -MAD_F_ONE)
        fixed = -MAD_F_ONE;

    /* Conversion */
    fixed = fixed >> (MAD_F_FRACBITS - 23);
    return ((sample_t)fixed) << 8;
}


static void decode_mad_output(struct decode_mad *self) {
    struct mad_pcm *pcm;
    sample_t *buf, *buf_end;
    mad_fixed_t *left, *right;
    uint32_t nsamples;
    int i, offset = 0;

    pcm = &self->synth.pcm;

    /* parse xing header */
    if (self->packets++ == 0) {
        /* Bug 5720, files with CRC will have the ptr in the
         * wrong place
         */
        if (self->frame.header.flags & MAD_FLAG_PROTECTION) {
            if (self->stream.anc_ptr.byte > self->stream.buffer + 2) {
                self->stream.anc_ptr.byte = self->stream.anc_ptr.byte - 2;
            }
        }

        xing_parse(self);
        self->state = MAD_STATE_OK;
        return;
    }

    /* Bug 9046, don't allow sample rate to change mid stream */
    if (self->packets > 2 && self->sample_rate != self->frame.header.samplerate) {
        LOG_DEBUG(log_audio_codec, "Sample rate changed from %d to %d, discarding PCM", self->sample_rate, self->frame.header.samplerate);
        current_decoder_state |= DECODE_STATE_ERROR;
        return;
    }
    self->sample_rate = self->frame.header.samplerate;

    decode_metadata_set_bitrate(self->frame.header.bitrate / 1000);

    buf = self->output_buffer;
    buf_end = self->output_buffer + OUTPUT_BUFFER_FRAMES;

    left = pcm->samples[0];

    if (pcm->channels == 2) {
        /* stereo */
        right = pcm->samples[1];
    } else {
        /* mono */
        right = pcm->samples[0];
    }

    /* skip samples for the encoder delay */
    if (self->encoder_delay) {
        offset = self->encoder_delay;
        if (offset > pcm->length) {
            offset = pcm->length;
        }

        LOG_DEBUG(log_audio_codec, "Skip encoder_delay=%d pcm->length=%d offset=%d", self->encoder_delay, pcm->length, offset);

        self->encoder_delay -= offset;

        left += offset;
        right += offset;
    }

    /* Track the total number of samples we have decoded */
    self->decoded_samples += pcm->length;

    /* Remove encoder padding. Only do this if we are streaming a file,
     * some radio stations seem to incorrectly include a xing header in
     * the stream.
     */
    if (self->encoder_padding && !streambuf_is_icy()) {
        if (pcm->length > self->lame_samples_remain) {
            LOG_DEBUG(log_audio_codec,
                    "Removing encoder padding, lame_samples_remain=%" PRIu64,
                    self->lame_samples_remain);

            pcm->length = (unsigned short)self->lame_samples_remain;
        }

        /* Bug 16233, if total decoded samples gets beyond lame_samples + 1152, assume we were given
         * an invalid Xing header and should stop treating samples as encoder padding
         */
        if (self->decoded_samples > self->lame_samples + 1152) {
            LOG_DEBUG(log_audio_codec,
                    "Decoded more samples (%" PRIu64 ") than expected (%" PRIu64 "), assuming"
                    " invalid LAME header and ignoring padding",
                    self->decoded_samples,
                    self->lame_samples);
            self->encoder_padding = 0;
        }
    }

    for (i=offset; i<pcm->length; i++) {
        *buf++ = mad_fixed_to_32bit(*left++);
        *buf++ = mad_fixed_to_32bit(*right++);

        if (buf == buf_end) {
            nsamples = (buf - self->output_buffer) / 2;
            decode_output_samples(self->output_buffer, nsamples, self->sample_rate);

            if (self->encoder_padding) {
                if (nsamples > self->lame_samples_remain) {
                    self->lame_samples_remain = 0;
                } else {
                    self->lame_samples_remain -= nsamples;
                }
            }

            buf = self->output_buffer;
        }
    }

    nsamples = (buf - self->output_buffer) / 2;
    if (nsamples) {
        decode_output_samples(self->output_buffer, nsamples, self->sample_rate);

        if (self->encoder_padding) {
            if (nsamples > self->lame_samples_remain) {
                self->lame_samples_remain = 0;
            } else {
                self->lame_samples_remain -= nsamples;
            }
        }
    }

    /* If we've come to the guard pointer, we're done */
    if (self->stream.this_frame == self->guard_pointer) {
        LOG_DEBUG(log_audio_codec,
                "Reached end of stream decoded_samples: %" PRIu64,
                self->decoded_samples);
        self->state = MAD_STATE_END_OF_FILE;
    } else {
        self->state = MAD_STATE_OK;
    }
}


static bool decode_mad_callback(void *data) {
    struct decode_mad *self = (struct decode_mad *) data;
    // can't discard what hasn't been written in, but right now we
    // can consume more in the case of meta data.  This could be
    // simplified.
    static size_t left_to_discard = 0;

    /* End of file? */
    if (self->state == MAD_STATE_END_OF_FILE ||
        self->state == MAD_STATE_ERROR) {
        return false;
    }

    if (self->state == MAD_STATE_OK) {
        decode_mad_frame(self);
    }

    if (self->state == MAD_STATE_PCM_READY) {
        decode_mad_output(self);
    }

    if (self->bytes_consumed != 0) {
        left_to_discard += self->bytes_consumed;
        left_to_discard -= streambuf_discard(left_to_discard, true);
        self->bytes_consumed_total += self->bytes_consumed;
        self->discarded_samples = self->decoded_samples;
        LOG_TRACE(log_audio_codec,
                "discarded_samples: %" PRIu64 " bytes_consumed_total: %" PRIu64
                " bytes_read: %" PRIu64,
                self->discarded_samples,
                self->bytes_consumed_total,
                self->bytes_read);
        LOG_TRACE(log_audio_codec,
                "bytes_consumed: %" PRIu64 " left_to_discard: %zu\n",
                self->bytes_consumed, left_to_discard);
        self->bytes_consumed = 0;
    }

    return true;
}


static size_t decode_mad_samples(void *data) {
    return BYTES_TO_SAMPLES(OUTPUT_BUFFER_BYTES);
}


static void *decode_mad_start(uint8_t *params, uint32_t num_params) {
    struct decode_mad *self;

    LOG_DEBUG(log_audio_codec, "decode_mad_start()");

    self = malloc(sizeof(struct decode_mad));
    memset(self, 0, sizeof(struct decode_mad));

    self->input_buffer = malloc(INPUT_BUFFER_SIZE + MAD_BUFFER_GUARD);
    self->output_buffer = malloc(OUTPUT_BUFFER_BYTES);
    self->guard_pointer = NULL;

    mad_stream_init(&self->stream);
    mad_frame_init(&self->frame);
    mad_synth_init(&self->synth);

    /* Assume we aren't changing sample rates until proven wrong */
    self->sample_rate = decode_output_samplerate();
    self->encoder_delay = MAD_DECODER_DELAY;

    /* Don't check for CRC errors (bug #2527) */
    // XXXX this needs a patch to libmad
    //decoder->stream.options = MAD_OPTION_ZEROCRC;

    return self;
}


static void decode_mad_stop(void *data) {
    struct decode_mad *self = (struct decode_mad *) data;

    LOG_DEBUG(log_audio_codec, "decode_mad_stop()");

    mad_stream_finish(&self->stream);
    mad_frame_finish(&self->frame);
    mad_synth_finish(&self->synth);

    free(self->input_buffer);
    free(self->output_buffer);
    free(self);
}


struct decode_module decode_mad = {
    'm',
    "mp3",
    decode_mad_start,
    decode_mad_stop,
    decode_mad_samples,
    decode_mad_callback,
    NULL  // no flush operation
};

