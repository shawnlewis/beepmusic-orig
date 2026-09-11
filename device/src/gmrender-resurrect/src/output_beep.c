/* output_beep.c - Output module for GStreamer
 *
 * Copyright (C) 2005-2007   Ivo Clarysse
 *
 * Adapted to gstreamer-0.10 2006 David Siorpaes
 *
 * This file is part of GMediaRender.
 *
 * GMediaRender is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GMediaRender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GMediaRender; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>

#include "logging.h"
#include "upnp_connmgr.h"
#include "output_module.h"
#include "upnp_device.h"
#include "upnp_control.h"
#include "upnp_transport.h"

#include <libubus.h>
#include "beep/app.h"
#include "beep/beep_ubus.h"
#include "beep/log.h"

#define OBJ_NAME_DLNA "beep.app.DLNA"

struct track_time_info {
    int64_t duration;
    int64_t position;
};

// Passing state changes from DLNA to Beep needs to happen indirectly
// because audio_pause, audio_play, and set_*_volume happen synchronously
// but allow event handlers to fire.  Event handlers must lock DLNA
// structures to access them, resulting in deadlock.
struct dlna_to_beep_state {
    struct blob_buf *play_args;
    int volume;
    bool play;
    bool pause;
};

static pthread_mutex_t shared_state_mutex;
static struct dlna_to_beep_state shared_state;
static struct uloop_fd state_event_fd;

static inline void signal_state_event(void) {
    uint64_t x = 1;
    write(state_event_fd.fd, &x, sizeof(uint64_t));
}

static inline void shared_state_lock(void) {
    pthread_mutex_lock(&shared_state_mutex);
}

static inline void shared_state_unlock(void) {
    pthread_mutex_unlock(&shared_state_mutex);
}

static int last_volume = -1;
static int beep_volume = 0;
static struct upnp_device *upnp_device = NULL;
static char *beep_group_name = NULL;
static char beep_usn[80];

static int play_uri(const char* uri, struct SongMetaData *metadata) {
    void *metadata_cookie = NULL;

    shared_state_lock();
    if (shared_state.play_args) {
        blob_buf_free(shared_state.play_args);
        free(shared_state.play_args);
    }

    shared_state.play_args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(shared_state.play_args, 0);
    blobmsg_add_string(shared_state.play_args, "url", uri);
    blobmsg_add_string(shared_state.play_args, "name", "dlna");
    blobmsg_add_string(shared_state.play_args, "app_obj", OBJ_NAME_DLNA);
    if (metadata) {
        metadata_cookie = blobmsg_open_table(shared_state.play_args, "metadata");
        if (metadata->artist) {
            blobmsg_add_string(shared_state.play_args, "track_artist", metadata->artist);
        }
        if (metadata->album) {
            blobmsg_add_string(shared_state.play_args, "track_album", metadata->album);
        }
        if (metadata->title) {
            blobmsg_add_string(shared_state.play_args, "track_title", metadata->title);
        }
        if (metadata->album_art_uri) {
            blobmsg_add_string(shared_state.play_args, "album_art_url", metadata->album_art_uri);
        }
        blobmsg_close_table(shared_state.play_args, metadata_cookie);

        LOG_DEBUG(log_beep_main, "Playing mimetype: %s", metadata->mime_type);
        if (metadata->mime_type
                && strstr(metadata->mime_type, "mpeg")) {
            blobmsg_add_string(shared_state.play_args, "type", "m");
        } else if (metadata->mime_type
                && strstr(metadata->mime_type, "ogg")) {
            blobmsg_add_string(shared_state.play_args, "type", "o");
        } else if (metadata->mime_type
                && strstr(metadata->mime_type, "aac")) {
            blobmsg_add_string(shared_state.play_args, "type", "a");
        } else if (metadata->mime_type
                && strstr(metadata->mime_type, "flac")) {
            blobmsg_add_string(shared_state.play_args, "type", "f");
        } else {
            // Don't know it, hope for the best with mp3!
            LOG_WARN(log_beep_main, "Unknown mime_type! Trying mp3.");
            blobmsg_add_string(shared_state.play_args, "type", "m");
        }
    }
    shared_state_unlock();
    signal_state_event();

    return 1;
}

static void output_beep_set_next_uri(const char *uri) {
    LOG_DEBUG(log_beep_main, "set_next_uri to :%s", uri);
}

static void output_beep_set_uri(const char *uri,
                     const char *meta,
                     output_update_meta_cb_t meta_cb) {
    LOG_DEBUG(log_beep_main, "set_uri to :%s", uri);
    struct SongMetaData metadata;
    SongMetaData_init(&metadata);
    int rc = SongMetaData_parse_DIDL(&metadata, meta);

    if (rc) {
        // This is private user info, don't log in production
        // LOG_DEBUG(log_beep_main, "PARSED META: %s %s %s",
        //         metadata.title,
        //         metadata.artist,
        //         metadata.album);
        play_uri(uri, &metadata);
        SongMetaData_clear(&metadata);
    } else {
        play_uri(uri, NULL);
    }
}

static int output_beep_play(output_transition_cb_t callback) {
    LOG_DEBUG(log_beep_main, "play");
    shared_state_lock();
    shared_state.play = true;
    shared_state.pause = false;
    shared_state_unlock();

    signal_state_event();
    return 0;
}

static int output_beep_stop(void) {
    LOG_DEBUG(log_beep_main, "stop");
    if (upnp_device) {
        inform_play_transition_from_output(PLAY_STOPPED, false);
        shared_state_lock();
        shared_state.pause = true;
        shared_state.play = false;
        shared_state_unlock();

        signal_state_event();
    }
    return 0;
}

static int output_beep_pause(void) {
    LOG_DEBUG(log_beep_main, "pause");
    shared_state_lock();
    shared_state.pause = true;
    shared_state.play = false;
    shared_state_unlock();

    signal_state_event();
    return 0;
}

static int output_beep_seek(int64_t position_nanos) {
    LOG_DEBUG(log_beep_main, "seek");
    return 0;
}

static int output_beep_get_position(int64_t *track_duration,
                     int64_t *track_pos) {
    //LOG_DEBUG(log_beep_main, "get position");
    *track_duration = get_track_duration() * 1000000000LL;    // duration is secs
    *track_pos = get_written_track_time() * 1000000LL;  // msecs

    return 0;
}

static void output_beep_get_name_and_usn(char **name, char **usn) {
    *name = beep_group_name;
    *usn = beep_usn;
}

static int output_beep_get_volume(float *v) {
    LOG_DEBUG(log_beep_main, "get volume");
    *v = beep_volume / 1000.0f;
    return 0;
}

static int output_beep_set_volume(float value) {
    LOG_DEBUG(log_beep_main, "set volume");

    last_volume = value * 1000;
    // Don't directly call set_master_volume -- this is called in dlna thread
    shared_state_lock();
    shared_state.volume = last_volume;
    shared_state_unlock();

    signal_state_event();
    return 0;
}

static int output_beep_get_mute(int *m) {
    LOG_DEBUG(log_beep_main, "get mute");
    *m = false;
    return 0;
}

static int output_beep_set_mute(int m) {
    LOG_DEBUG(log_beep_main, "set mute");
    return 0;
}

static void beep_on_volume_handler(int volume, void *userdata) {
    LOG_DEBUG(log_beep_main, "Got beep volume: %d", volume);

    // TODO: both of these may not be thread safe, figure out gmrender's
    // threading model.
    beep_volume = volume;

    if (upnp_device && abs(last_volume - volume) > 1) {
        control_service_lock();
        LOG_INFO(log_beep_main, "Locked volume change");
        change_volume_level(volume / 10);
        control_service_unlock();
    }
}

static void beep_on_group_handler(const char *group_name, const char *uuid,
        void *userdata) {
    // This is private user info, don't log in production
    // LOG_DEBUG(log_beep_main, "SETTING group name: %s uuid: %s", group_name, uuid);
    if (beep_group_name) {
        free(beep_group_name);
    }
    beep_group_name = strdup(group_name);

    snprintf(beep_usn, 80, "uuid:%s", uuid);

    if (upnp_device) {
        upnp_device_set_name(beep_group_name, beep_usn, upnp_device);
    }
}

static void beep_on_playpause_handler(bool playing, void *userdata) {
    if (upnp_device) {
        transport_service_lock();
        if (playing) {
            change_transport_state(TRANSPORT_PLAYING);
        } else {
            change_transport_state(TRANSPORT_PAUSED_PLAYBACK);
        }
        transport_service_unlock();
    }
}

static void beep_on_stopped_handler(void *userdata) {
    LOG_DEBUG(log_beep_main, "on_stopped_handler");
    if (upnp_device) {
        inform_play_transition_from_output(PLAY_STOPPED, true);
    }
}

struct beep_event_callbacks beep_event_handler = {
    .on_volume_event = &beep_on_volume_handler,
    .on_buffer_event = NULL,
    .on_group_event = &beep_on_group_handler,
    .on_playpause_event = &beep_on_playpause_handler,
    .on_stopped_event = &beep_on_stopped_handler,

    .userdata = NULL,
};

static int get_state(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {

    struct blob_buf bbuf;
    memset(&bbuf, 0, sizeof(struct blob_buf));
    blob_buf_init(&bbuf, 0);
    beep_reply_success(ctx, req, bbuf.head);
    blob_buf_free(&bbuf);

    return 0;
}

static const struct ubus_method dlna_methods[] = {
    UBUS_METHOD_NOARG("get_state", get_state),
};

static struct ubus_object_type dlna_object_type =
    UBUS_OBJECT_TYPE(OBJ_NAME_DLNA, dlna_methods);

static struct ubus_object dlna_object = {
    .name = OBJ_NAME_DLNA,
    .type = &dlna_object_type,
    .methods = dlna_methods,
    .n_methods = ARRAY_SIZE(dlna_methods),
};

static void state_event_fd_callback(
        struct uloop_fd *u, unsigned int events) {
    // ON ULOOP THREAD
    uint64_t x;
    read(state_event_fd.fd, &x, sizeof(uint64_t));

    // Lock shared state because we must reset values
    shared_state_lock();
    if (shared_state.volume > 0) {
        set_master_volume(shared_state.volume);
        shared_state.volume = -1;
    }

    // Handle pause first in case play is also set so play
    // has higher priority than pause
    if (shared_state.pause) {
        audio_pause();
        shared_state.pause = false;
    }

    if (shared_state.play) {
        audio_resume();
        shared_state.play = false;
    }

    if (shared_state.play_args) {
        struct blob_attr* response = NULL;
        struct blob_attr *parsed[__BEEP_RESPONSE_MAX];

        int ret = beep_ubus_invoke(
                "beep.app.webradio", "play", shared_state.play_args->head,
                &response);
        if (ret) {
            LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        }

        if (!beep_parse_response(response, parsed)) {
            LOG_ERROR(log_beep_main, "webradio::play invalid response");
        }

        if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
            LOG_ERROR(log_beep_main, "webradio::play failed");
        }

        blob_buf_free(shared_state.play_args);
        free(shared_state.play_args);
        free(response);
        shared_state.play_args = NULL;
    }

    shared_state_unlock();
}

void* uloop_thread(void* arg) {
    app_init("app_dlna", &dlna_object);
    set_beep_event_handler(&beep_event_handler);

    state_event_fd.cb = &state_event_fd_callback;
    state_event_fd.fd = eventfd(0, 0);
    uloop_fd_add(&state_event_fd, ULOOP_READ);
    app_start();

    return NULL;
}

static int output_beep_init(void) {
    LOG_DEBUG(log_beep_main, "init");

    log_beep_main = LOG_CATEGORY_GET("dlna");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    // Set up shared state and mutex
    memset(&shared_state, 0, sizeof(struct dlna_to_beep_state));
    pthread_mutex_init(&shared_state_mutex, 0);

    // TODO never freed, does output ever get shutdown?
    pthread_t* thread = malloc(sizeof(pthread_t));
    pthread_create(thread, NULL, uloop_thread, NULL);

    register_mime_type("audio/mpeg");
    register_mime_type("audio/x-ogg");
    register_mime_type("audio/x-aac");
    register_mime_type("audio/x-flac");

    // Wait til we've received the initial state callbacks.
    int iters = 0;
    while (1) {
        if (beep_group_name) {
            return 0;
        }
        usleep(100000);
        if (iters >= 100) {
            LOG_DEBUG(log_beep_main, "Never got group name, Exiting...");
            exit(1);
        }
        iters++;
    }

    return 0;
}

static void output_beep_set_device(struct upnp_device *device) {
    upnp_device = device;
}

struct output_module beep_output = {
        .shortname = "beep",
    .description = "Beep Player",
    .init        = output_beep_init,
    .set_device        = output_beep_set_device,

    //.add_options = output_beep_add_options,
    .set_uri     = output_beep_set_uri,

    // disabled in upnp_transport.c
    .set_next_uri= output_beep_set_next_uri,
    .play        = output_beep_play,
    .stop        = output_beep_stop,
    .pause       = output_beep_pause,
    .seek        = output_beep_seek,
    .get_position = output_beep_get_position,
    .get_name_and_usn = output_beep_get_name_and_usn,
    .get_volume  = output_beep_get_volume,
    .set_volume  = output_beep_set_volume,
    .get_mute  = output_beep_get_mute,
    .set_mute  = output_beep_set_mute,
};
