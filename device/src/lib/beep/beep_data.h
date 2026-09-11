#ifndef BEEP_DATA_H
#define BEEP_DATA_H

#define BEEP_DATA_TIMEOUT_MS 4000

typedef void (*beep_data_get_callback_t)(
        const char *key,
        const void *value,
        size_t len,
        void *userdata);

typedef void (*beep_data_set_callback_t)(
        const char *key,
        bool ok,
        void *userdata);

typedef void(*beep_data_delete_callback_t)(
        const char *key,
        bool ok,
        void *userdata);

void beep_data_get(const char *key, bool global,
        beep_data_get_callback_t done, void *userdata);
void beep_data_set(const char *key, const void *value, size_t len, bool global,
        beep_data_set_callback_t done, void *userdata);
void beep_data_delete(const char *key, bool global,
        beep_data_delete_callback_t done, void *userdata);

#endif  // BEEP_DATA_H
