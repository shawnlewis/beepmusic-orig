#ifndef BEEP_APP_H
#define BEEP_APP_H

#include <libubox/blobmsg_json.h>
#include <libubus.h>

#include "beep/debug.h"

struct ubus_context* app_init(const char* app_name, struct ubus_object* obj);
void app_end(struct ubus_object* obj);
void app_start(void);

// Returns a token
int audio_acquire(const char* app_ubus_obj);
int audio_can_track_begin(int token);
bool audio_track_begin(
        int token, const char* track_id,
        const char* title0, const char* title1, const char* title2,
        const char* image_url, const char audio_type,
        int content_length);
bool audio_track_end(int token);
bool set_track_info(
        int token,
        const char* title0, const char* title1, const char* title2,
        const char* image_url);
int audio_can_buffer(int token, size_t size);
bool audio_buffer(int token, uint8_t* ptr, size_t size);
bool audio_flush(int token);
bool set_station(
        int token, const char* station_id, const char* station_name,
        const char* station_image_url,
        const char* play_station_method, struct blob_attr* play_station_args);
bool audio_resume(void);
bool audio_pause(void);

bool msg_socket_send_message(const char* app_id, const char* sender_id,
        const char* msg_namespace, const char* message);
bool msg_socket_close(const char* app_id, const char* sender_id);

uint32_t get_output_used(void);
uint32_t get_streambuf_used(void);
uint32_t get_written_track_time(void);
uint32_t get_duration(void);
uint32_t get_track_duration(void);
uint32_t get_buffered_bytes(void);
int get_master_volume(void);
void set_master_volume(int volume);
void set_track_volume_scalar(int volume);

typedef void (*on_volume_event_t)(int volume, void *userdata);
typedef void (*on_buffer_event_t)(int buffered_bytes, void *userdata);
typedef void (*on_group_event_t)(const char *group_name, const char *uuid,
        void *userdata);
typedef void (*on_playpause_event_t)(bool playing, void *userdata);
typedef void (*on_stopped_event_t)(void *userdata);

struct beep_event_callbacks {
    on_volume_event_t on_volume_event;
    on_buffer_event_t on_buffer_event;
    on_group_event_t on_group_event;
    on_playpause_event_t on_playpause_event;
    on_stopped_event_t on_stopped_event;

    void *userdata;
};

void set_beep_event_handler(struct beep_event_callbacks *callbacks);

#endif  // BEEP_APP_H
