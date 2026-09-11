#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

#include <openssl/crypto.h>
#include <pthread.h>

#include "beepcloud.h"

// #define TRACE_BEEP_DATA

struct request {
    CURL *handle;

    char *key;
    void *value;
    size_t len;

    union {
        get_callback_t get;
        set_callback_t set;
        delete_callback_t delete;
    } callback;

    long error_code;
    void *userdata;
};

/*
 * Get
 */
static void data_get_done(void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif

    struct request *req = (struct request *)userdata;

    if(req->callback.get) {
        (*req->callback.get)(req->key, req->value, req->len, req->userdata);
    }

    curl_easy_cleanup(req->handle);
    free(req->key);
    free(req->value);
    free(req);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}

struct output_buffer {
    uint8_t *data;
    size_t size;
};

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif
    struct output_buffer *output = (struct output_buffer *)userp;
    size_t available_size = size * nmemb;

    output->data = realloc(output->data, output->size + available_size + 1);
    if(output->data == NULL) {
        LOG_DEBUG(log_beep_main, "Out of memory, needed %zu bytes",
                output->size + available_size + 1);
        abort();
    }
    memcpy(&output->data[output->size], contents, available_size);
    output->size += available_size;
    output->data[output->size] = 0;

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
    return available_size;
}

static void data_get_task(void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif
    struct request *req = (struct request *)userdata;

    // Set data, etc.
    req->handle = curl_easy_init();

    char url[URL_MAX_LEN];
    snprintf(url, URL_MAX_LEN, "%sget", url_prefix);
#ifdef TRACE_BEEP_DATA
    LOG_INFO(log_beep_main, "url = %s", url);
#endif

    char *esc_key = curl_easy_escape(req->handle, req->key, 0);

    char data[DATA_MAX_LEN];
    snprintf(data, DATA_MAX_LEN, "cluster_id=%s&key=%s",
            esc_cluster_id, esc_key);
#ifdef TRACE_BEEP_DATA
    LOG_INFO(log_beep_main, "data = %s", data);
#endif

    curl_free(esc_key);

    curl_easy_setopt(req->handle, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(req->handle, CURLOPT_TIMEOUT_MS, TIMEOUT_MS);
    curl_easy_setopt(req->handle, CURLOPT_URL, url);
    curl_easy_setopt(req->handle, CURLOPT_POSTFIELDS, data);
    if(curl_share) {
        curl_easy_setopt(req->handle, CURLOPT_SHARE, curl_share);
    }

    struct output_buffer *output = calloc(1, sizeof(struct output_buffer));
    curl_easy_setopt(req->handle, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(req->handle, CURLOPT_WRITEDATA, output);
    curl_easy_setopt(req->handle, CURLOPT_USE_SSL, CURLUSESSL_ALL);

    CURLcode res = curl_easy_perform(req->handle);

    if(CURLE_OK != res) {
        LOG_ERROR(log_beep_main, "curl failed: %s (%d)", curl_easy_strerror(res), res);
        req->value = NULL;
        req->len = 0;
        free(output->data);
        free(output);
        return;
    }

    req->value = output->data;
    req->len = output->size;
    free(output);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}

void data_get(const char *key, get_callback_t done, void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif

    // Make our own internal copies to work with
    struct request *req = calloc(1, sizeof(struct request));
    req->key = strdup(key);
    req->callback.get = done;
    req->userdata = userdata;

    beep_async_task(&data_get_task, &data_get_done, req);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}

/*
 * Set
 */
static void data_set_done(void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif
    struct request *req = (struct request *)userdata;

    if(req->callback.set) {
        (*req->callback.set)(req->key,
                req->error_code == 200 ? true : false,
                req->userdata);
    }

    curl_easy_cleanup(req->handle);
    free(req->key);
    free(req->value);
    free(req);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}

static size_t null_write_cb(void *contents, size_t size,
        size_t nmemb, void *userp) {
    return size * nmemb;
}

static void data_set_task(void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif

    struct request *req = (struct request *)userdata;

    // Set data, etc.
    req->handle = curl_easy_init();

    char url[URL_MAX_LEN];
    snprintf(url, URL_MAX_LEN, "%sset", url_prefix);
#ifdef TRACE_BEEP_DATA
    LOG_INFO(log_beep_main, "url = %s", url);
#endif

    char *esc_key = curl_easy_escape(req->handle, req->key, 0);
    char *esc_value = curl_easy_escape(req->handle, req->value, req->len);

    // Size of "extra" characters in the POST data... FIXME?
    static const size_t data_static_size = 21;
    size_t data_total_size = data_static_size + strlen(esc_cluster_id) +
        strlen(esc_key) + strlen(esc_value) + 1;

    char *data = malloc(data_total_size);;

    snprintf(data, data_total_size, "cluster_id=%s&key=%s&val=%s",
            esc_cluster_id, esc_key, esc_value);
#ifdef TRACE_BEEP_DATA
    LOG_INFO(log_beep_main, "data = %s", data);
#endif
    curl_free(esc_key);
    curl_free(esc_value);

    curl_easy_setopt(req->handle, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(req->handle, CURLOPT_TIMEOUT_MS, TIMEOUT_MS);
    curl_easy_setopt(req->handle, CURLOPT_URL, url);
    curl_easy_setopt(req->handle, CURLOPT_POSTFIELDS, data);
    if(curl_share) {
        curl_easy_setopt(req->handle, CURLOPT_SHARE, curl_share);
    }

    /* This suppresses curl's default behavior of writing result to STDOUT */
    curl_easy_setopt(req->handle, CURLOPT_WRITEFUNCTION, &null_write_cb);
    curl_easy_setopt(req->handle, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(req->handle, CURLOPT_USE_SSL, CURLUSESSL_ALL);

    CURLcode res = curl_easy_perform(req->handle);
    free(data);

    if(CURLE_OK != res) {
        LOG_ERROR(log_beep_main, "curl failed: %s (%d)",
                curl_easy_strerror(res), res);
        req->error_code = 0;
        return;
    }

    curl_easy_getinfo(req->handle, CURLINFO_RESPONSE_CODE, &req->error_code);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}

void data_set(const char *key, const char *value, size_t len,
        set_callback_t done, void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif

    // Make our own internal copies to work with
    struct request *req = calloc(1, sizeof(struct request));
    req->key = strdup(key);
    req->value = malloc(len);
    memcpy(req->value, value, len);
    req->len = len;
    req->callback.set = done;
    req->userdata = userdata;

    beep_async_task(&data_set_task, &data_set_done, req);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}

/*
 * Delete
 */
static void data_delete_done(void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif
    struct request *req = (struct request *)userdata;

    if(req->callback.delete) {
        (*req->callback.delete)(req->key,
                req->error_code == 200 ? true : false,
                req->userdata);
    }

    curl_easy_cleanup(req->handle);
    free(req->key);
    free(req);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}

static void data_delete_task(void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif

    struct request *req = (struct request *)userdata;

    // Set data, etc.
    req->handle = curl_easy_init();

    char url[URL_MAX_LEN];
    snprintf(url, URL_MAX_LEN, "%sdelete", url_prefix);
#ifdef TRACE_BEEP_DATA
    LOG_INFO(log_beep_main, "url = %s", url);
#endif

    char *esc_key = curl_easy_escape(req->handle, req->key, 0);

    char data[DATA_MAX_LEN];
    snprintf(data, DATA_MAX_LEN, "cluster_id=%s&key=%s",
            esc_cluster_id, esc_key);
#ifdef TRACE_BEEP_DATA
    LOG_INFO(log_beep_main, "data = %s", data);
#endif
    curl_free(esc_key);

    curl_easy_setopt(req->handle, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(req->handle, CURLOPT_TIMEOUT_MS, TIMEOUT_MS);
    curl_easy_setopt(req->handle, CURLOPT_URL, url);
    curl_easy_setopt(req->handle, CURLOPT_POSTFIELDS, data);
    if(curl_share) {
        curl_easy_setopt(req->handle, CURLOPT_SHARE, curl_share);
    }

    curl_easy_setopt(req->handle, CURLOPT_WRITEFUNCTION, &null_write_cb);
    curl_easy_setopt(req->handle, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(req->handle, CURLOPT_USE_SSL, CURLUSESSL_ALL);

    CURLcode res = curl_easy_perform(req->handle);

    if(CURLE_OK != res) {
        LOG_ERROR(log_beep_main, "curl failed: %s (%d)",
                curl_easy_strerror(res), res);
        req->error_code = 0;
        return;
    }

    curl_easy_getinfo(req->handle, CURLINFO_RESPONSE_CODE, &req->error_code);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}

void data_delete(const char *key, delete_callback_t done, void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif

    // Make our own internal copies to work with
    struct request *req = calloc(1, sizeof(struct request));
    req->key = strdup(key);
    req->callback.delete = done;
    req->userdata = userdata;

    beep_async_task(&data_delete_task, &data_delete_done, req);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}
