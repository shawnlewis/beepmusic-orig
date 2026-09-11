#include "audio/decode/decode_priv.h"

pthread_mutex_t mutex;

struct decode_metadata data;

void decode_metadata_lock(void) {
    pthread_mutex_lock(&mutex);
}

void decode_metadata_unlock(void) {
    pthread_mutex_unlock(&mutex);
}

void decode_metadata_clear(void) {
    data.bitrate = 0;
}

struct decode_metadata* decode_metadata_read(void) {
    return &data;
}

void decode_metadata_init(void) {
    decode_metadata_clear();
    pthread_mutex_init(&mutex, NULL);
}

// value in kilobits per second
void decode_metadata_set_bitrate(int kbps) {
    decode_metadata_lock();
    data.bitrate = kbps;
    decode_metadata_unlock();
}

void decode_metadata_set_track_info(
        const char* artist, const char* album, const char* track_name) {
}
