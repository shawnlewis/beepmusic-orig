// Copyright (c) 2013 Beep Inc.  All rights reserved.

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>

#include <libubox/usock.h>
#include <libubox/ustream.h>

#include "beep/debug.h"
#include "beep/beep_stream_ch.h"
// TODO: These should be abstracted a bit better for opening/listening
// on sockets.
#include "beep/client.h"
#include "beep/net.h"
#include "beep/uhelpers.h"


#define BEEP_COMM_PACKET_FLAGS_NONE                                 0x00000000
#define BEEP_COMM_PACKET_FLAGS_RESPONSE                             0x00000001
#define BEEP_COMM_PACKET_FLAGS_DBG_PRINT                            0x00000002
#define BEEP_COMM_PACKET_FLAGS_VAR_SIZE                             0x00000004

#define BEEP_COMM_CMD_OFFSET                                        0x00000000
#define BEEP_COMM_CMD_SIZE                                          0x00000004
// The channel type is encoded in the ctrl field.
#define BEEP_COMM_CMD_CH_TYPE_MASK                                  0xf0000000
#define BEEP_COMM_CMD_CH_TYPE_RESERVED                              0x00000000
#define BEEP_COMM_CMD_CH_TYPE_CMD                                   0x10000000
#define BEEP_COMM_CMD_CH_TYPE_STREAM                                0x20000000
#define BEEP_COMM_CMD_CMD_MASK                                      0x0fffffff
#define BEEP_COMM_CMD_NACK                                          0x00000000
#define BEEP_COMM_CMD_ACK                                           0x00000001

// Stream channel protocol.
#define BEEP_STREAM_CH_CMD_OFFSET                                   0x00000000
#define BEEP_STREAM_CH_CMD_SIZE                                     0x00000004
#define BEEP_STREAM_CH_CMD_NACK                                     0x00000000
#define BEEP_STREAM_CH_CMD_ACK                                      0x00000001
#define BEEP_STREAM_CH_CMD_START                                    0x00000002
#define BEEP_STREAM_CH_CMD_ST_END                                   0x00000003
#define BEEP_STREAM_CH_CMD_SYNC_PT                                  0x00000004
#define BEEP_STREAM_CH_CMD_BUFFER                                   0x00000005
#define BEEP_STREAM_CH_CMD_ST_BEGIN                                 0x00000006
#define BEEP_STREAM_CH_CMD_SYNC_DATA                                0x00000007
// Stashing these here but should probably get moved to ubus.
#define BEEP_STREAM_CH_CMD_SYNC_TO                                  0x00000008
#define BEEP_STREAM_CH_CMD_SYNC_WAIT                                0x00000009
#define BEEP_STREAM_CH_CMD_FLUSH                                    0x0000000a

// We don't need to wait for the ack response on the standard stream
// commands.
#define BEEP_STREAM_CH_START_FLAGS         BEEP_COMM_PACKET_FLAGS_DBG_PRINT
#define BEEP_STREAM_CH_ST_BEGIN_FLAGS      BEEP_COMM_PACKET_FLAGS_DBG_PRINT
#define BEEP_STREAM_CH_BUFFER_FLAGS        BEEP_COMM_PACKET_FLAGS_VAR_SIZE
#define BEEP_STREAM_CH_ST_END_FLAGS        BEEP_COMM_PACKET_FLAGS_DBG_PRINT
#define BEEP_STREAM_CH_FLUSH_FLAGS         BEEP_COMM_PACKET_FLAGS_DBG_PRINT


// Is this used?
#define BEEP_STREAM_CH_SYNC_PT_FLAGS       (BEEP_COMM_PACKET_FLAGS_RESPONSE | \
                                            BEEP_COMM_PACKET_FLAGS_DBG_PRINT)

#define BEEP_STREAM_CH_SYNC_DATA_FLAGS     (BEEP_COMM_PACKET_FLAGS_RESPONSE | \
                                            BEEP_COMM_PACKET_FLAGS_VAR_SIZE)
#define BEEP_STREAM_CH_SYNC_TO_FLAGS       (BEEP_COMM_PACKET_FLAGS_RESPONSE | \
                                            BEEP_COMM_PACKET_FLAGS_DBG_PRINT | \
                                            BEEP_COMM_PACKET_FLAGS_VAR_SIZE)
#define BEEP_STREAM_CH_SYNC_WAIT_FLAGS     (BEEP_COMM_PACKET_FLAGS_RESPONSE | \
                                            BEEP_COMM_PACKET_FLAGS_DBG_PRINT)

// 256k + room for the header
#define MAX_SEND_SIZE 256 * 1024 + 1024

// Use this for recieving the response.
int _beep_recv_amount(int socketfd, uint8_t* buf, int total);

void on_usock_connect_done(bool success, void* priv) {
    BeepStreamChCtx *ctx = priv;
    if (success) {
        ctx->state = STREAM_CH_CONN_CONNECTED;
    }
    ustream_fd_init(&ctx->stream_fd, ctx->sockfd);
    ctx->on_connect_done(success, ctx->connect_priv);
}

int stream_ch_connect(BeepStreamChCtx *ctx, const char *ip_addr, int port,
        stream_ch_connected_cb on_done, void* priv) {
    char port_str[20];

    // This is global for the whole program!
    signal(SIGPIPE, SIG_IGN);

    memset(ctx, 0, sizeof(BeepStreamChCtx));

    snprintf(port_str, 20, "%d", port);
    ctx->sockfd = usock(USOCK_NUMERIC | USOCK_TCP | USOCK_IPV4ONLY | USOCK_NONBLOCK
               | USOCK_NOADDRCONFIG,
            ip_addr, port_str);
    if (ctx->sockfd < 0) {
        ctx->sockfd = 0;
        LOG_ERROR(log_beep_main, "could not open socket");
        return 0;
    }
    ctx->on_connect_done = on_done;
    ctx->connect_priv = priv;
    usock_wait_connect(ctx->sockfd, on_usock_connect_done, ctx);
    ctx->state = STREAM_CH_CONN_CONNECTING;
    return 1;
}

void stream_ch_disconnect(BeepStreamChCtx *ctx) {
    if (ctx != NULL && ctx->state == STREAM_CH_CONN_CONNECTED) {
        ustream_free(&ctx->stream_fd.stream);
        close(ctx->sockfd);
        ctx->state = STREAM_CH_CLOSED;
    }
}

static uint32_t gen_new_id(uint32_t old_id) {
    // Ideally this should do something to minimize id collisions between
    // groups.  Just increment for now.
    return ++old_id;
}

static int stream_ch_packet_size(uint32_t cmd) {
    int size = 0;

    switch (cmd) {

    case BEEP_STREAM_CH_CMD_NACK:
    case BEEP_STREAM_CH_CMD_ACK:
    case BEEP_STREAM_CH_CMD_SYNC_WAIT:
        break;

    case BEEP_STREAM_CH_CMD_FLUSH:
        size = sizeof(BeepStreamChFlush);
        break;

    case BEEP_STREAM_CH_CMD_ST_BEGIN:
        size = sizeof(BeepStreamChStBegin);
        break;
    case BEEP_STREAM_CH_CMD_START:
    case BEEP_STREAM_CH_CMD_ST_END:
        size = sizeof(BeepStreamChStart);
        break;

    case BEEP_STREAM_CH_CMD_SYNC_PT:
        size = sizeof(BeepStreamChSyncPt);
        break;

    case BEEP_STREAM_CH_CMD_BUFFER:
        // Need to fix this since we're not using packed.
        size = sizeof(BeepStreamChBuffer) - 4;
        break;

    case BEEP_STREAM_CH_CMD_SYNC_TO:
        size = sizeof(BeepStreamChSyncTo);
        break;

    case BEEP_STREAM_CH_CMD_SYNC_DATA:
        size = offsetof(BeepStreamChSyncData, data);
        break;

    default:
        return 0;
    }

    return size + sizeof(BeepStreamChHdr);
}

static int stream_ch_packet_var_size(BeepStreamChPacket *packet) {
    switch (packet->cmd) {
    case BEEP_STREAM_CH_CMD_BUFFER:
        return packet->data.buffer.size;
    case BEEP_STREAM_CH_CMD_SYNC_DATA:
        return packet->data.sync_data.size;
    case BEEP_STREAM_CH_CMD_SYNC_TO:
        return packet->data.sync_to.size;
    default:
        LOG_ERROR(log_beep_main, "cmd %d does not have variable size",
            packet->cmd);
        return 0;
    }
}

static unsigned int stream_ch_packet_flags(uint32_t cmd) {
    switch (cmd) {

    case BEEP_STREAM_CH_CMD_START:
        return BEEP_STREAM_CH_START_FLAGS;
    case BEEP_STREAM_CH_CMD_ST_END:
        return BEEP_STREAM_CH_ST_END_FLAGS;
    case BEEP_STREAM_CH_CMD_SYNC_PT:
        return BEEP_STREAM_CH_SYNC_PT_FLAGS;
    case BEEP_STREAM_CH_CMD_BUFFER:
        return BEEP_STREAM_CH_BUFFER_FLAGS;
    case BEEP_STREAM_CH_CMD_ST_BEGIN:
        return BEEP_STREAM_CH_ST_BEGIN_FLAGS;
    case BEEP_STREAM_CH_CMD_SYNC_DATA:
        return BEEP_STREAM_CH_SYNC_DATA_FLAGS;
    case BEEP_STREAM_CH_CMD_SYNC_TO:
        return BEEP_STREAM_CH_SYNC_TO_FLAGS;
    case BEEP_STREAM_CH_CMD_SYNC_WAIT:
        return BEEP_STREAM_CH_SYNC_WAIT_FLAGS;
    case BEEP_STREAM_CH_CMD_FLUSH:
        return BEEP_STREAM_CH_FLUSH_FLAGS;

    default:
        break;
    }
    return BEEP_COMM_PACKET_FLAGS_NONE;
}

static int stream_ch_send_packet(BeepStreamChCtx *ctx,
    BeepStreamChPacket *packet, const uint8_t *var_data, int var_size) {
    static uint8_t send_buf[MAX_SEND_SIZE];
    BeepStreamChPacket t_packet;
    uint32_t *ptr = (uint32_t *) &t_packet;
    unsigned int flags = stream_ch_packet_flags(packet->cmd);
    int size = stream_ch_packet_size(packet->cmd);
    int i = size / sizeof(uint32_t);
    int ret;

    assert(size + var_size <= MAX_SEND_SIZE);

    // Note: This will mangle the packet on little endian machines so we'll
    // make a backup of just the parts we're going to switch.  This could be
    // useful it we want to resend the same packets without reconstruction.
    memcpy(&t_packet, packet, size);

    // Mark the packet command with the correct type.
    t_packet.cmd |= BEEP_COMM_CMD_CH_TYPE_STREAM;

    // This also only works if all fields in the packet are 32 bits wide except
    // for the buffer data which we don't byte swap.
    while (i--) {
        *ptr = htonl(*ptr);
        ptr++;
    }

    if (flags & BEEP_COMM_PACKET_FLAGS_DBG_PRINT) {
        LOG_DEBUG(log_beep_main, "send packet cmd: %d, size: %d", packet->cmd, size);
    }

    // THere is a performance fix here. Previously we making two separate
    // send() calls if we had var_data to send. This triggered a tcp
    // issue due to the interaction between Nagle's algorithm and tcp
    // delayed acknoledgement which occurs when a w-w-r pattern is used.
    // We could also fix the issue by disabling stream_ch response (in
    // which case we'd have w-w-w).
    // See // http://en.wikipedia.org/wiki/Nagle's_algorithm for a
    // description of the issue.
    memcpy(send_buf, &t_packet, size);
    if (var_data != NULL) {
        memcpy(send_buf + size, var_data, var_size);
        size += var_size;
    }
    ret = ustream_write(&ctx->stream_fd.stream, (const char *) send_buf, size, false);

    if (ret != size) {
        LOG_ERROR(log_beep_main, "send packet failed");
        return 0;
    }

    ret = 1;

    // NOTE: no caller currently uses the response check code below. We should
    // probably convert it to use ustream if we're going to use it.
    // Wait for the response if flagged and beep_send was successful.
    if ((flags & BEEP_COMM_PACKET_FLAGS_RESPONSE)) {
        // TODO: Cleanup...
        i = _beep_recv_amount(ctx->sockfd, (uint8_t *) &t_packet,
            sizeof(BeepStreamChHdr));
        ret &= (i == sizeof(BeepStreamChHdr));
        ret &= ((BEEP_STREAM_CH_CMD_ACK | BEEP_COMM_CMD_CH_TYPE_STREAM) ==
            ntohl(t_packet.cmd));
        if ((flags & BEEP_COMM_PACKET_FLAGS_DBG_PRINT) && ret) {
            LOG_DEBUG(log_beep_main, "send packet cmd: %d - response ack",
                packet->cmd);
        }
    }
    if (!ret) {
        LOG_ERROR(log_beep_main, "send packet cmd: %d - response nack/error",
            packet->cmd);
    }

    return ret;
}

/*  Unused right now.
int stream_ch_nack(BeepStreamChCtx *ctx) {
    BeepStreamChPacket packet;
    // Not sure what the required state for this is.
    if (ctx == NULL || ctx->state == STREAM_CH_CLOSED) {
        return 0;
    }

    packet.cmd = BEEP_STREAM_CH_CMD_NACK;
    return stream_ch_send_packet(ctx, &packet, NULL, 0);
}

int stream_ch_ack(BeepStreamChCtx *ctx) {
    BeepStreamChPacket packet;
    // Not sure what the required state for this is.
    if (ctx == NULL || ctx->state == STREAM_CH_CLOSED) {
        return 0;
    }

    packet.cmd = BEEP_STREAM_CH_CMD_ACK;
    return stream_ch_send_packet(ctx, &packet, NULL, 0);
}
*/

int stream_ch_start(BeepStreamChCtx *ctx, uint32_t *id) {
    BeepStreamChPacket packet;
    uint32_t new_id;
    if (ctx == NULL || ctx->state != STREAM_CH_CONN_CONNECTED) {
        LOG_ERROR(log_beep_main, "invalid %s", ctx ? "state" : "params");
        return 0;
    }

    // If not specified generate a new id.  This will allow us to use the same
    // id for a group or set an id when syncing a new device mid stream.
    if (id == NULL || *id == 0) {
        new_id = gen_new_id(ctx->id);
    } else {
        new_id = *id;
    }
    memset(&packet, 0, sizeof(BeepStreamChPacket));
    packet.cmd = BEEP_STREAM_CH_CMD_START;
    packet.data.start.id = new_id;
    if (!stream_ch_send_packet(ctx, &packet, NULL, 0)) {
        return 0;
    }

    ctx->id = new_id;
    ctx->seq = 0;
    if (id != NULL) *id = new_id;
    return 1;
}

int stream_ch_st_end(BeepStreamChCtx *ctx) {
    BeepStreamChPacket packet;
    if (ctx == NULL || ctx->state != STREAM_CH_CONN_CONNECTED) {
        LOG_ERROR(log_beep_main, "invalid %s", ctx ? "state" : "params");
        return 0;
    }

    memset(&packet, 0, sizeof(BeepStreamChPacket));
    packet.cmd = BEEP_STREAM_CH_CMD_ST_END;
    packet.data.st_end.id = ctx->id;
    if (!stream_ch_send_packet(ctx, &packet, NULL, 0)) {
        return 0;
    }

    return 1;
}

// If one device will not ack a sync point it should be dropped and
// reconnected to the group.  At the very least it should be marked as
// having corrupted state and can not sync a new device.
int stream_ch_sync_pt(BeepStreamChCtx *ctx) {
    BeepStreamChPacket packet;
    if (ctx == NULL || ctx->state != STREAM_CH_CONN_CONNECTED) {
        LOG_ERROR(log_beep_main, "invalid %s", ctx ? "state" : "params");
        return 0;
    }

    memset(&packet, 0, sizeof(BeepStreamChPacket));
    packet.cmd = BEEP_STREAM_CH_CMD_SYNC_PT;
    packet.data.sync_pt.id = ctx->id;
    packet.data.sync_pt.seq = ++(ctx->seq);
    // TODO: This should be more robust, we'll see how well it works out.
    return stream_ch_send_packet(ctx, &packet, NULL, 0);
}

int stream_ch_buffer(BeepStreamChCtx *ctx, uint8_t *payload, int size) {
    BeepStreamChPacket packet;
    if (ctx == NULL || ctx->state != STREAM_CH_CONN_CONNECTED) {
        LOG_ERROR(log_beep_main, "invalid %s", ctx ? "state" : "params");
        return 0;
    }

    memset(&packet, 0, sizeof(BeepStreamChPacket));
    packet.cmd = BEEP_STREAM_CH_CMD_BUFFER;
    packet.data.buffer.size = size;
    return stream_ch_send_packet(ctx, &packet, payload, size);
}

int stream_ch_st_begin(
        BeepStreamChCtx *ctx,
        uint32_t audio_type,
        uint32_t transition_type,
        uint32_t transition_period,
        uint32_t replay_gain,
        uint32_t output_threshold,
        uint32_t polarity_inversion,
        uint32_t output_channels) {
    BeepStreamChPacket packet;
    if (ctx == NULL || ctx->state != STREAM_CH_CONN_CONNECTED) {
        LOG_ERROR(log_beep_main, "invalid %s", ctx ? "state" : "params");
        return 0;
    }

    memset(&packet, 0, sizeof(BeepStreamChPacket));
    packet.cmd = BEEP_STREAM_CH_CMD_ST_BEGIN;
    packet.data.st_begin.type = audio_type;
    packet.data.st_begin.transition_type = transition_type;
    packet.data.st_begin.transition_period = transition_period;
    packet.data.st_begin.replay_gain = replay_gain;
    packet.data.st_begin.output_threshold = output_threshold;
    packet.data.st_begin.polarity_inversion = polarity_inversion;
    packet.data.st_begin.output_channels = output_channels;
    return stream_ch_send_packet(ctx, &packet, NULL, 0);
}

int stream_ch_flush(BeepStreamChCtx *ctx, int set_cookie) {
    BeepStreamChPacket packet;
    if (ctx == NULL || ctx->state != STREAM_CH_CONN_CONNECTED) {
        LOG_ERROR(log_beep_main, "invalid %s", ctx ? "state" : "params");
        return 0;
    }

    memset(&packet, 0, sizeof(BeepStreamChPacket));
    packet.cmd = BEEP_STREAM_CH_CMD_FLUSH;
    packet.data.flush.set_cookie = set_cookie;
    return stream_ch_send_packet(ctx, &packet, NULL, 0);
}

int stream_ch_sync_data(BeepStreamChCtx *ctx, uint8_t *payload, int size) {
    BeepStreamChPacket packet;
    if (ctx == NULL || ctx->state != STREAM_CH_CONN_CONNECTED) {
        LOG_ERROR(log_beep_main, "invalid %s", ctx ? "state" : "params");
        return 0;
    }

    memset(&packet, 0, sizeof(BeepStreamChPacket));
    packet.cmd = BEEP_STREAM_CH_CMD_SYNC_DATA;
    packet.data.sync_data.size = size;
    return stream_ch_send_packet(ctx, &packet, payload, size);
}

int stream_ch_sync_to(BeepStreamChCtx *ctx, const char *host) {
    BeepStreamChPacket packet;
    int size;

    if (ctx == NULL || ctx->state != STREAM_CH_CONN_CONNECTED ||
        host == NULL) {
        LOG_ERROR(log_beep_main, "invalid %s", ctx ? "state" : "params");
        return 0;
    }

    size = strlen(host) + 1;
    memset(&packet, 0, sizeof(BeepStreamChPacket));
    packet.cmd = BEEP_STREAM_CH_CMD_SYNC_TO;
    packet.data.sync_to.size = size;
    return stream_ch_send_packet(ctx, &packet, (const uint8_t *)host, size);
}

int stream_ch_sync_wait(BeepStreamChCtx *ctx) {
    BeepStreamChPacket packet;
    if (ctx == NULL || ctx->state != STREAM_CH_CONN_CONNECTED) {
        LOG_ERROR(log_beep_main, "invalid %s", ctx ? "state" : "params");
        return 0;
    }

    memset(&packet, 0, sizeof(BeepStreamChPacket));
    packet.cmd = BEEP_STREAM_CH_CMD_SYNC_WAIT;
    return stream_ch_send_packet(ctx, &packet, NULL, 0);
}

static int stream_ch_on_packet(BeepStreamChCtx *ctx,
    BeepStreamChPacket *packet, uint8_t *var_data, int var_size) {
    int ret = 0;
    unsigned int flags = stream_ch_packet_flags(packet->cmd);

    if (flags & BEEP_COMM_PACKET_FLAGS_DBG_PRINT) {
        LOG_DEBUG(log_beep_main, "recv packet cmd: %d", packet->cmd);
    }

    // This is setup so if not all callbacks are needed they won't
    // get dropped on the floor or accumulate errors.
    switch (packet->cmd) {

    case BEEP_STREAM_CH_CMD_START:
        if (ctx->events.on_start == NULL) goto on_packet_unregistered;
        ret = ctx->events.on_start(&packet->data.start);
        break;

    case BEEP_STREAM_CH_CMD_ST_END:
        if (ctx->events.on_st_end == NULL) goto on_packet_unregistered;
        ret = ctx->events.on_st_end(&packet->data.st_end);
        break;

    case BEEP_STREAM_CH_CMD_SYNC_PT:
        if (ctx->events.on_sync_pt == NULL) goto on_packet_unregistered;
        ret = ctx->events.on_sync_pt(&packet->data.sync_pt);
        break;

    case BEEP_STREAM_CH_CMD_BUFFER:
        if (ctx->events.on_buffer == NULL) goto on_packet_unregistered;
        ret = ctx->events.on_buffer(&packet->data.buffer, var_data,
            var_size);
        break;

    case BEEP_STREAM_CH_CMD_ST_BEGIN:
        if (ctx->events.on_st_begin == NULL) goto on_packet_unregistered;
        LOG_INFO(log_beep_main, "st_begin.type = %c (%d)",
                packet->data.st_begin.type, packet->data.st_begin.type);
        ret = ctx->events.on_st_begin(&packet->data.st_begin);
        break;

    case BEEP_STREAM_CH_CMD_FLUSH:
        if (ctx->events.on_flush == NULL) goto on_packet_unregistered;
        ret = ctx->events.on_flush(&packet->data.flush);
        break;

    case BEEP_STREAM_CH_CMD_SYNC_DATA:
        if (ctx->events.on_sync_data == NULL) goto on_packet_unregistered;
        ret = ctx->events.on_sync_data(&packet->data.sync_data, var_data,
            var_size);
        break;

    case BEEP_STREAM_CH_CMD_SYNC_TO:
        if (ctx->events.on_sync_to == NULL) goto on_packet_unregistered;
        ret = ctx->events.on_sync_to(&packet->data.sync_to, var_data,
            var_size);
        break;

    case BEEP_STREAM_CH_CMD_SYNC_WAIT:
        if (ctx->events.on_sync_wait == NULL) goto on_packet_unregistered;
        ret = ctx->events.on_sync_wait();
        break;

    default:
        LOG_ERROR(log_beep_main, "unknown cmd: %d", packet->cmd);
        ctx->errors++;
        return ret;
    }

    return ret;

on_packet_unregistered:
    LOG_WARN(log_beep_main, "event for cmd: %d is not registered", packet->cmd);
    return ret;
}

int stream_ch_on_recv(BeepConnection* conn, void* userdata) {
    BeepStreamChPacket packet;
    BeepStreamChCtx *ctx = (BeepStreamChCtx *) userdata;
    uint32_t *ptr = (uint32_t *) &packet;
    uint8_t *buf;
    uint32_t cmd;
    unsigned int flags;
    int req_size = sizeof(BeepStreamChHdr);
    int var_size = 0;
    int read_size, i, response;

    read_size = beep_net_read(conn, &buf, req_size);
    if (read_size != req_size) {
        return 0;
    }

    // Need to use memcpy since beep_net_read may not be properly aligned and
    // we don't want to use __attribute__((packed)) for performance reasons.
    // Even then it could cause unaligned accesses which not every arch
    // supports.
    memcpy(&packet, buf, read_size);

    // TODO: Right now there is no error recovery in the stream.  We'll just
    // move to next 4 bytes and try again.
    cmd = ntohl(packet.cmd);
    if ((cmd & BEEP_COMM_CMD_CH_TYPE_MASK) != BEEP_COMM_CMD_CH_TYPE_STREAM) {
        goto stream_error;
    }
    cmd &= BEEP_COMM_CMD_CMD_MASK;
    req_size = stream_ch_packet_size(cmd);
    if (req_size == 0) {
        goto stream_error;
    }

    read_size = beep_net_read(conn, &buf, req_size);
    if (read_size != req_size) {
        return 0;  // Not error just not enough data.
    }
    memcpy(&packet, buf, read_size);

    i = read_size / sizeof(uint32_t);
    while (i--) {
        *ptr = ntohl(*ptr);
        ptr++;
    }
    packet.cmd = cmd;  // Already has ch type stripped.
    flags = stream_ch_packet_flags(packet.cmd);

    // If this is the buffer command make sure the whole payload has also been
    // recieved.  Maybe change this to
    if (flags & BEEP_COMM_PACKET_FLAGS_VAR_SIZE) {
        var_size = stream_ch_packet_var_size(&packet);
        read_size = beep_net_read(conn, &buf, req_size + var_size);
        if (read_size != (req_size + var_size)) {
            return 0;
        }
    }

    // TODO: cleanup the response code here.  We really should just be able
    // to call stream_ch_ack(...)/stream_ch_nack(...).  This is why the client
    // side code in client.c and host side code in net.c needs to be one
    // inet4 lib or something.
    response = stream_ch_on_packet(ctx, &packet, buf + req_size, var_size);
    if (flags & BEEP_COMM_PACKET_FLAGS_RESPONSE) {
        if (flags & BEEP_COMM_PACKET_FLAGS_DBG_PRINT) {
            LOG_DEBUG(log_beep_main, "recv packet cmd: %d - response: %s", packet.cmd,
                response ? "ack" : "nack");
        }
        if (response) {
            //stream_ch_ack(ctx);
            packet.cmd = htonl(BEEP_STREAM_CH_CMD_ACK |
                BEEP_COMM_CMD_CH_TYPE_STREAM);
        } else {
            //stream_ch_nack(ctx);
            packet.cmd = htonl(BEEP_STREAM_CH_CMD_NACK |
                BEEP_COMM_CMD_CH_TYPE_STREAM);
        }
        beep_respond(conn, (uint8_t *) &packet, sizeof(BeepStreamChHdr));
    }

    return read_size;

stream_error:
    LOG_ERROR(log_beep_main, "req_size: %d cmd: %d read_size: %d",
        req_size, cmd, read_size);
    ctx->errors++;
    return read_size;
}

pthread_t *stream_ch_listen(BeepStreamChCtx *ctx, int port,
    const BeepStreamChEvents *events) {
    if (ctx == NULL || events == NULL) {
        LOG_ERROR(log_beep_main, "invalid params");
        return NULL;
    }

    memset(ctx, 0, sizeof(BeepStreamChCtx));
    memcpy(&(ctx->events), events, sizeof(BeepStreamChEvents));

    beep_register_port(port, MAX_CONNECTIONS, stream_ch_on_recv, ctx);

    // Well right now pthread_t gets thrown away :( so return null I guess.
    // But we do need a way to join the thread back to the parent.  Probably
    // the parent should just change ctx->state to STREAM_CH_WAIT_CLOSE.
    return NULL;
}

