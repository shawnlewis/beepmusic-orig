// Copyright (c) 2013 Beep Inc.  All rights reserved.

#ifndef __BEEP_STREAM_CH_H__
#define __BEEP_STREAM_CH_H__

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include <libubox/ustream.h>

typedef struct {
    uint32_t cmd;
} BeepStreamChHdr;

// TODO: Remove this later, start doesn't need data.  Use StBegin instead.
typedef struct {
    uint32_t id;
} BeepStreamChStart;

typedef BeepStreamChStart BeepStreamChStEnd;

typedef struct {
    uint32_t id;
    uint32_t seq;
} BeepStreamChSyncPt;

typedef struct {
    uint32_t size;
    uint8_t data;
} BeepStreamChBuffer;

typedef struct {
    uint32_t size;
    uint8_t data;
} BeepStreamChSyncData;

typedef struct {
    uint32_t sync_mtu;
    uint32_t size;
    uint8_t data;  //host string.
} BeepStreamChSyncTo;

typedef struct {
    uint32_t type;
    uint32_t transition_type;
    uint32_t transition_period;
    uint32_t replay_gain;
    uint32_t output_threshold;
    uint32_t polarity_inversion;
    uint32_t output_channels;
} BeepStreamChStBegin;

typedef struct {
    uint32_t set_cookie;
} BeepStreamChFlush;

typedef struct {
    uint32_t cmd;
    union {
        BeepStreamChStart start;
        BeepStreamChStEnd st_end;
        BeepStreamChSyncPt sync_pt;
        BeepStreamChBuffer buffer;
        BeepStreamChSyncData sync_data;
        BeepStreamChSyncTo sync_to;
        BeepStreamChStBegin st_begin;
        BeepStreamChFlush flush;
    } data;
} BeepStreamChPacket;

typedef struct _BeepStreamChCtx BeepStreamChCtx;

typedef void (*stream_ch_connected_cb)(bool success, void* priv);

// General return codes 1 == ack/ok, 0 == nack/error.

// Connect/disconnect to sink.  Currently for ip4 should probably abstract it
// so we can send ip4, ip6, unix sock, our own transport/network layer.
int stream_ch_connect(BeepStreamChCtx *ctx, const char *ip_addr, int port,
        stream_ch_connected_cb on_done, void* priv);
void stream_ch_disconnect(BeepStreamChCtx *ctx);

// Send start of song/track.
int stream_ch_start(BeepStreamChCtx *ctx, uint32_t *id);
// Send end of song/track.
int stream_ch_st_end(BeepStreamChCtx *ctx);
// Send a sync point.  It is up to the sender how often to send the sync points.
int stream_ch_sync_pt(BeepStreamChCtx *ctx);
// Send some encoded data.
int stream_ch_buffer(BeepStreamChCtx *ctx, uint8_t *payload, int size);
// Send beginning of song/track with decoder information.
int stream_ch_st_begin(
        BeepStreamChCtx *ctx,
        uint32_t audio_type,
        uint32_t transition_type,
        uint32_t transition_period,
        uint32_t replay_gain,
        uint32_t output_threshold,
        uint32_t polarity_inversion,
        uint32_t output_channels);
// Flush all buffers but leave decoder state.
int stream_ch_flush(BeepStreamChCtx *ctx, int set_cookie);
// Send sync data from one sink to another.  Content of data is unknown to
// communication libraries.
int stream_ch_sync_data(BeepStreamChCtx *ctx, uint8_t *payload, int size);
// The next two commands should be moved to ctrl channel when implemented.
// Instruct sink to sync it's data with another sink.
int stream_ch_sync_to(BeepStreamChCtx *ctx, const char *host);
// Instruct sink to clear it's state and wait to receive sync data.
int stream_ch_sync_wait(BeepStreamChCtx *ctx);

typedef struct {
    int (*on_start)(BeepStreamChStart *data);
    int (*on_st_end)(BeepStreamChStEnd *data);
    int (*on_sync_pt)(BeepStreamChSyncPt *data);
    int (*on_buffer)(BeepStreamChBuffer *data, uint8_t *payload, int size);
    int (*on_st_begin)(BeepStreamChStBegin *data);
    int (*on_flush)(BeepStreamChFlush *data);
    int (*on_sync_data)(BeepStreamChSyncData *data, uint8_t *payload, int size);
    int (*on_sync_to)(BeepStreamChSyncTo *data, uint8_t *payload, int size);
    int (*on_sync_wait)(void);
} BeepStreamChEvents;

pthread_t *stream_ch_listen(BeepStreamChCtx *ctx, int port,
    const BeepStreamChEvents *events);

typedef enum {
    STREAM_CH_CLOSED,  // This must be 0.
    STREAM_CH_CONN_CONNECTING,
    STREAM_CH_CONN_CONNECTED,
    STREAM_CH_CONN_LISTEN,
    STREAM_CH_WAIT_CLOSE
} BeepStreamChState;

// Consider privatizing this
struct _BeepStreamChCtx {
    BeepStreamChState state;
    int sockfd;
    int errors;
    uint32_t id;
    uint32_t seq;
    BeepStreamChEvents events;

    // only used while in state STREAM_CH_CONN_CONNECTING
    stream_ch_connected_cb on_connect_done;
    void *connect_priv;

    struct ustream_fd stream_fd;
};

#endif

