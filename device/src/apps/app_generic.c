// Standard Beep player. Can play a url, or a list of urls.
//
// Design:
//   - Two threads may be active at any given time. The main thread (the ubus
//     thread) is always active. The main thread creates a player thread,
//     which is responsible for playing media from a single url. Only one
//     player thread may be active at a time, and a new one is created
//     sequentially for each track in a playlist.
//   - The main thread manages the playlist, player threads are not allowed
//     to touch it. Player threads do get a play_ctx, which represents a
//     single track.  play_ctx also contains the list links, but the Player
//     thread must not manipulate those.
//   - Thread communication:
//     - Main thread may create a Player thread.
//     - Any thread may signal that the playlist has advanced, which means
//       that the main thread should delete the previously played track,
//       and create a new player thread for the next track. The signal
//       is an event fd, playlist_advance_fd.
//     - end_current_play_task, used by main thread to tell player_thread to
//       stop playing
//     - last_token a global play threads use to tell the next play thread
//       what audio token to use. Can be set to zero by the main thread
//       to signal that the next play thread should do an acquire for a new
//       token.

#define _GNU_SOURCE

#include <curl/curl.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <stdarg.h>

#include <libubox/blobmsg_json.h>
#include <libubus.h>

#include "beep/app.h"
#include "beep/beeplib.h"
#include "beep/beep_ubus.h"
#include "beep/flags.h"
#include "beep/mdns.h"

#define OBJ_NAME_GENERIC "beep.app.webradio"

static struct list_head playlist_head = LIST_HEAD_INIT(playlist_head);
static pthread_t play_thread;

// Default is MP3
#define DEFAULT_AUDIO_TYPE 'm'

// metadata support functions/constants
enum {
    METADATA_TRACK_TITLE,
    METADATA_TRACK_ARTIST,
    METADATA_TRACK_ALBUM,
    METADATA_ALBUM_ART_URL,
    METADATA_STATION_NAME,
    METADATA_STATION_ART_URL,
    METADATA_PLAYLIST_NAME,
    METADATA_PLAYLIST_AUTHOR,
    METADATA_PROVIDER_NAME,
    METADATA_PROVIDER_ART_URL,

    __METADATA_MAX
};

static char *METADATA_CLEAR[__METADATA_MAX] = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static const struct blobmsg_policy metadata_policy[] = {
    [METADATA_TRACK_TITLE] =
            { .name = "track_title", .type = BLOBMSG_TYPE_STRING },
    [METADATA_TRACK_ARTIST] =
            { .name = "track_artist", .type = BLOBMSG_TYPE_STRING },
    [METADATA_TRACK_ALBUM] =
            { .name = "track_album", .type = BLOBMSG_TYPE_STRING },
    [METADATA_ALBUM_ART_URL] =
            { .name = "album_art_url", .type = BLOBMSG_TYPE_STRING },
    [METADATA_STATION_NAME] =
            { .name = "station_name", .type = BLOBMSG_TYPE_STRING },
    [METADATA_STATION_ART_URL] =
            { .name = "station_art_url", .type = BLOBMSG_TYPE_STRING },
    [METADATA_PLAYLIST_NAME] =
            { .name = "playlist_title", .type = BLOBMSG_TYPE_STRING },
    [METADATA_PLAYLIST_AUTHOR] =
            { .name = "playlist_author", .type = BLOBMSG_TYPE_STRING },
    [METADATA_PROVIDER_NAME] =
            { .name = "provider_name", .type = BLOBMSG_TYPE_STRING },
    [METADATA_PROVIDER_ART_URL] =
            { .name = "provider_art_url", .type = BLOBMSG_TYPE_STRING },
};

// Allocates and returns an array of strings on the heap, where each item is a
// value corresponding to the metadata field defined by METADATA_*.  value
// can be NULL, which indicates that there was no data for this field
char **parse_metadata_table(struct blob_attr *tbl, int len) {
    char **metadata;

    if (!tbl) {
        //TODO: Return minimal metadata table?
        return NULL;
    }

    struct blob_attr *tb[__METADATA_MAX];
    blobmsg_parse(metadata_policy, __METADATA_MAX,
            tb, tbl, len);

    metadata = malloc(sizeof(char *) * __METADATA_MAX);
    for (int i=0; i < __METADATA_MAX; i++) {
        metadata[i] = tb[i] ? strdup(blobmsg_get_string(tb[i])) : NULL;
    }

    return metadata;
}

// Helper function to convert .local-TLD hostnames to IP addresses
// Caller must free dest_url
static bool convert_url(char **dest_url, const char *src_url) {
    char *new_url;
    char scheme[URLPARSE_SCHEME_LEN];
    char host[URLPARSE_HOST_LEN];
    int port;
    char path[URLPARSE_PATH_LEN];

    if (beep_urlparse(src_url, scheme, host, &port, path)) {
        int host_len = strlen(host);
        if (host_len > 6 && !strcmp(&host[host_len - 6], ".local")) {
            char* ip = mdns_getaddrinfo(host);
            if (ip) {
                if(asprintf(&new_url, "%s://%s:%d/%s", scheme,
                            ip, port, path) < 0) {
                    return false;
                }
                *dest_url = new_url;
            } else {
                LOG_WARN(log_beep_main, "mdns lookup failed");
                return false;
            }
        } else {
            *dest_url = strdup(src_url);
        }
    }

    return true;
}

// play_station parameters
enum {
    PLAY_STATION_URL,
    PLAY_STATION_HEADERS,
    PLAY_STATION_POST_DATA,
    PLAY_STATION_NAME,
    PLAY_STATION_IMAGE,
    PLAY_STATION_TYPE,

    PLAY_STATION_ARTIST,
    PLAY_STATION_ALBUM,
    PLAY_STATION_TITLE,
    PLAY_STATION_ALBUM_ART_URI,

    PLAY_STATION_APP_OBJ,

    __PLAY_STATION_MAX
};

static const struct blobmsg_policy play_station_policy[] = {
    [PLAY_STATION_URL] = { .name = "url", .type = BLOBMSG_TYPE_STRING },
    [PLAY_STATION_HEADERS] = { .name = "headers", .type = BLOBMSG_TYPE_STRING },
    [PLAY_STATION_POST_DATA] = { .name = "post_data", .type = BLOBMSG_TYPE_STRING },
    [PLAY_STATION_NAME] = { .name = "name", .type = BLOBMSG_TYPE_STRING },
    [PLAY_STATION_IMAGE] = { .name = "image_url", .type = BLOBMSG_TYPE_STRING },
    [PLAY_STATION_TYPE] = { .name = "type", .type = BLOBMSG_TYPE_STRING},

    [PLAY_STATION_ARTIST] = { .name = "artist", .type = BLOBMSG_TYPE_STRING },
    [PLAY_STATION_ALBUM] = { .name = "album", .type = BLOBMSG_TYPE_STRING },
    [PLAY_STATION_TITLE] = { .name = "title", .type = BLOBMSG_TYPE_STRING },
    [PLAY_STATION_ALBUM_ART_URI] = { .name = "album_art_uri",
        .type = BLOBMSG_TYPE_STRING },

    // Apps (DLNA) may override this to set the current app name. This also
    // tells distributor who to route device UI commands (play/pause) to.
    [PLAY_STATION_APP_OBJ] = { .name = "app_obj", .type = BLOBMSG_TYPE_STRING },
};

// play and enqueue parameters
enum {
    PLAY_URL,
    PLAY_HEADERS,
    PLAY_POST_DATA,
    PLAY_METADATA,
    PLAY_APP_OBJ,
    PLAY_TYPE,

    __PLAY_MAX
};

static const struct blobmsg_policy play_policy[] = {
    [PLAY_URL] = { .name = "url", .type = BLOBMSG_TYPE_STRING },
    [PLAY_HEADERS] = { .name = "headers", .type = BLOBMSG_TYPE_STRING },
    [PLAY_POST_DATA] = { .name = "post_data", .type = BLOBMSG_TYPE_STRING },
    [PLAY_METADATA] = { .name = "metadata", .type = BLOBMSG_TYPE_TABLE },
    [PLAY_APP_OBJ] = { .name = "app_obj", .type = BLOBMSG_TYPE_STRING },
    [PLAY_TYPE] = { .name = "type", .type = BLOBMSG_TYPE_STRING },
};

// play_list and enqueue_list parameters
// PLAY_LIST_ITEMS is just an array of tables that conform to play_policy
enum {
    PLAY_LIST_ITEMS,

    __PLAY_LIST_MAX
};

static const struct blobmsg_policy play_list_policy[] = {
    [PLAY_LIST_ITEMS] = { .name = "items", .type = BLOBMSG_TYPE_ARRAY },
};

typedef struct play_ctx {
    struct list_head head;

    char *method;
    struct blob_attr *method_params;

    char *url;
    char *headers;
    char *post_data;
    char **metadata;
    char *app_obj;
    char type;

    CURL *curl_handle;

    int token;
    bool did_at_least_one_buffer;

    // This denotes if the stream should be reopened automatically when
    // curl exits with success status code.
    bool perpetual;

    int n_retries;
} PlayCtx;

// Set by main thread, read by play thread
static volatile bool end_current_play_task = false;

// Set by play thread, read by main thread
static volatile bool clear_playlist_signal = false;

// Set by play thread, read and cleared by main thread
static volatile int last_token = 0;

static void populate_play_context(PlayCtx *ctx, struct blob_attr **tbl) {
    // Process optional HTTP request info parameters
    if (tbl[PLAY_HEADERS]) {
        ctx->headers =
                strdup(blobmsg_get_string(tbl[PLAY_HEADERS]));
    }

    if (tbl[PLAY_POST_DATA]) {
        ctx->post_data =
                strdup(blobmsg_get_string(tbl[PLAY_POST_DATA]));
    }

    if (tbl[PLAY_APP_OBJ]) {
        ctx->app_obj =
                strdup(blobmsg_get_string(tbl[PLAY_APP_OBJ]));
    } else {
        ctx->app_obj =
                strdup(OBJ_NAME_GENERIC);
    }

    // Default audio type is 'm' for mp3
    if (tbl[PLAY_TYPE]) {
        ctx->type = *blobmsg_get_string(tbl[PLAY_TYPE]);
    } else {
        ctx->type = DEFAULT_AUDIO_TYPE;
    }

    if (tbl[PLAY_METADATA]) {
        ctx->metadata = parse_metadata_table(blobmsg_data(tbl[PLAY_METADATA]),
                blobmsg_data_len(tbl[PLAY_METADATA]));
    } else {
        ctx->metadata = METADATA_CLEAR;
    }
}
static void free_play_context(PlayCtx *ctx) {
    free(ctx->method);
    free(ctx->method_params);

    free(ctx->url);
    free(ctx->headers);
    free(ctx->post_data);
    free(ctx->app_obj);

    if (ctx->metadata && ctx->metadata != METADATA_CLEAR) {
        for (int i=0;i < __METADATA_MAX; i++)
            free(ctx->metadata[i]);
        free(ctx->metadata);
    }

    free(ctx);
}

// playlist manipulation
// ONLY CALL FROM MAIN THREAD
static void create_play_thread(PlayCtx *msg);
static void signal_playlist_advance(void);

static void add_to_playlist(PlayCtx *ctx) {
    list_add_tail(&ctx->head, &playlist_head);
    LOG_INFO(log_beep_main, "%s added to queue!", ctx->url);
}

static void clear_playlist(void) {
    PlayCtx *p, *n;

    list_for_each_entry_safe(p, n, &playlist_head, head) {
        free(p);
    }
    INIT_LIST_HEAD(&playlist_head);
    clear_playlist_signal = false;

    LOG_INFO(log_beep_main, "Playlist cleared");
}

static void play_if_needed(bool enqueue) {
    bool play_thread_exists =
            play_thread && pthread_kill(play_thread, 0) != ESRCH;
    // Note that play_thread could stop immediately after the previous
    // statement, so our value play_thread_exists could be incorrect, we'd
    // have true, but the play thread actually doesn't exist. However, the
    // logic is still correct, because the only time we care that it was
    // true we're just signaling play_thread to stop, so the same effect
    // is achieved by it stopping on it's own.
    //
    // The opposite case is not true: if play_thread_exists is false, there
    // is no way it can become true during the execution of this function,
    // which runs on the ubus thread, since we only start a new play thread
    // from the ubus thread.

    audio_resume();

    if (enqueue) {
       if (!play_thread_exists) {
            // If enqueuing and no play thread exists, signal playlist advance
            signal_playlist_advance();
       }
    } else {
        // Playing...
        if (play_thread_exists) {
            end_current_play_task = true;
        } else {
            signal_playlist_advance();
        }
    }
}

static void on_playlist_advanced(struct uloop_fd *u, unsigned int events);

static struct uloop_fd playlist_advance_fd = {
    .cb = on_playlist_advanced
};

// This does not conform to the rest of this component's set of methods
// and remains for compatibility reasons until all users of the old
// API have been phased out
static int generic_play_station(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    char **metadata;
    struct blob_attr *tb[__PLAY_STATION_MAX];

    blobmsg_parse(play_station_policy, __PLAY_STATION_MAX,
            tb, blob_data(msg), blob_len(msg));

    if (!tb[PLAY_STATION_URL] || !tb[PLAY_STATION_NAME]) {
        beep_reply_error(ctx, req, "Missing required arg", 0);
        return 0;
    }

    // Create play context
    // TODO: Refactor this into its own function?
    PlayCtx *p_ctx = calloc(1, sizeof(PlayCtx));

    p_ctx->method = strdup(method);
    p_ctx->method_params = blob_memdup(msg);

    if (!convert_url(&p_ctx->url,
            blobmsg_get_string(tb[PLAY_STATION_URL]))) {
        beep_reply_error(ctx, req,
                "Failed to translate .local URL", 0);
        free(p_ctx);
        return 0;
    }

    // Process optional HTTP request info parameters
    if (tb[PLAY_STATION_HEADERS]) {
        p_ctx->headers =
                strdup(blobmsg_get_string(tb[PLAY_STATION_HEADERS]));
    }

    if (tb[PLAY_STATION_POST_DATA]) {
        p_ctx->post_data =
                strdup(blobmsg_get_string(tb[PLAY_STATION_POST_DATA]));
    }

    if (tb[PLAY_STATION_APP_OBJ]) {
        p_ctx->app_obj =
                strdup(blobmsg_get_string(tb[PLAY_STATION_APP_OBJ]));
    } else {
        p_ctx->app_obj =
                strdup(OBJ_NAME_GENERIC);
    }

    // Default audio type is 'm' for mp3
    if (tb[PLAY_STATION_TYPE]) {
        p_ctx->type = *blobmsg_get_string(tb[PLAY_STATION_TYPE]);
    } else {
        p_ctx->type = DEFAULT_AUDIO_TYPE;
    }

    // Manually construct metadata array
    metadata = calloc(1, sizeof(char *) * __METADATA_MAX);
    p_ctx->metadata = metadata;

    if (tb[PLAY_STATION_TITLE]) {
        metadata[METADATA_TRACK_TITLE] =
                strdup(blobmsg_get_string(tb[PLAY_STATION_TITLE]));

    }
    if (tb[PLAY_STATION_ARTIST]) {
        metadata[METADATA_TRACK_ARTIST] =
                strdup(blobmsg_get_string(tb[PLAY_STATION_ARTIST]));
    }

    if (tb[PLAY_STATION_ALBUM]) {
        metadata[METADATA_TRACK_ALBUM] =
                strdup(blobmsg_get_string(tb[PLAY_STATION_ALBUM]));
    }

    if (tb[PLAY_STATION_NAME]) {
        metadata[METADATA_STATION_NAME] =
                strdup(blobmsg_get_string(tb[PLAY_STATION_NAME]));
    }

    if (tb[PLAY_STATION_IMAGE]) {
        metadata[METADATA_STATION_ART_URL] =
                strdup(blobmsg_get_string(tb[PLAY_STATION_IMAGE]));
    }

    p_ctx->perpetual = true;

    last_token = 0;
    clear_playlist();
    add_to_playlist(p_ctx);

    play_if_needed(false);
    beep_reply_success(ctx, req, NULL);

    return 0;
}

// Side effects:
// - Clears the playlist
// - Immediately stops any existing play_task
// - Create a new play_task
static int generic_play(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[__PLAY_MAX];
    bool enqueue = !strcmp(method,"enqueue");

    blobmsg_parse(play_policy, __PLAY_MAX,
            tb, blob_data(msg), blob_len(msg));

    if (!tb[PLAY_URL]) {
        beep_reply_error(ctx, req, "Missing required arg: url", 0);
        return 0;
    }

    // Create play context
    // TODO: Refactor this into its own function?
    PlayCtx *p_ctx = calloc(1, sizeof(PlayCtx));

    p_ctx->method = strdup(method);
    p_ctx->method_params = blob_memdup(msg);

    if (!convert_url(&p_ctx->url,
            blobmsg_get_string(tb[PLAY_URL]))) {
        beep_reply_error(ctx, req,
                "Failed to translate .local URL", 0);
        free(p_ctx);
        return 0;
    }

    populate_play_context(p_ctx, tb);

    if (!enqueue) {
        last_token = 0;
        clear_playlist();
    }

    add_to_playlist(p_ctx);

    play_if_needed(enqueue);
    beep_reply_success(ctx, req, NULL);

    return 0;
}

static int generic_play_list(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[__PLAY_LIST_MAX];
    struct blob_attr *attr, *data;
    int rem;
    int i = 0;
    int added = 0;

    bool enqueue = !strcmp(method,"enqueue_list");

    blobmsg_parse(play_list_policy, __PLAY_LIST_MAX,
            tb, blob_data(msg), blob_len(msg));

    if (!tb[PLAY_LIST_ITEMS]) {
        beep_reply_error(ctx, req, "Missing required arg: items", 0);
        return 0;
    }


    if (!enqueue) {
        last_token = 0;
        clear_playlist();
    }

    data = blobmsg_data(tb[PLAY_LIST_ITEMS]);
    rem = blobmsg_data_len(tb[PLAY_LIST_ITEMS]);

    __blob_for_each_attr(attr, data, rem) {
        struct blob_attr *tb2[__PLAY_MAX];

        blobmsg_parse(play_policy, __PLAY_MAX, tb2,
                blobmsg_data(attr), blobmsg_data_len(attr));

        if (!tb2[PLAY_URL]) {
            LOG_WARN(log_beep_main, "Missing required arg for item %d: url",
                    i);
            goto bad_item;
        }

        // Create play context
        // TODO: Refactor this into its own function?
        PlayCtx *p_ctx = calloc(1, sizeof(PlayCtx));

        p_ctx->method = strdup(method);
        p_ctx->method_params = blob_memdup(msg);

        if (!convert_url(&p_ctx->url,
                blobmsg_get_string(tb2[PLAY_URL]))) {
            LOG_WARN(log_beep_main, "Failed to translate .local URL for item %d",
                    i);
            free(p_ctx);
            goto bad_item;
        }

        populate_play_context(p_ctx, tb2);

        add_to_playlist(p_ctx);
        added++;

bad_item:
        i++;
    }

    if (!added) {
        beep_reply_error(ctx, req, "Couldn't enqueue any items in list", 0);
        return 0;
    }

    play_if_needed(enqueue);
    beep_reply_success(ctx, req, NULL);

    return 0;
}

static int generic_get_state(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_buf b;

    memset(&b, 0, sizeof(struct blob_buf));
    blob_buf_init(&b, 0);
    blobmsg_add_u8(&b, "alive", true);
    beep_reply_success(ctx, req, b.head);

    blob_buf_free(&b);
    return 0;
}

// Immediately skip track if there is something to skip to.  Otherwise, noop
static int generic_skip(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    LOG_INFO(log_beep_main, "SKIP");
    if (!list_empty(&playlist_head)) {
        if (play_thread) {
            last_token = 0;
            end_current_play_task = true;
        } else {
            signal_playlist_advance();
        }
    }

    beep_reply_success(ctx, req, NULL);
    return 0;
}

static int generic_shutdown(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    beep_reply_success(ctx, req, NULL);
    app_end(obj);
    return 0;
}

static const struct ubus_method generic_methods[] = {
    UBUS_METHOD("play_station", generic_play_station, play_station_policy),
    UBUS_METHOD("play", generic_play, play_policy),
    UBUS_METHOD("play_list", generic_play_list, play_list_policy),
    UBUS_METHOD("enqueue", generic_play, play_policy),
    UBUS_METHOD("enqueue_list", generic_play_list, play_list_policy),
    UBUS_METHOD_NOARG("skip", generic_skip),
    UBUS_METHOD_NOARG("get_state", generic_get_state),
    UBUS_METHOD_NOARG("shutdown", generic_shutdown),
};

static struct ubus_object_type generic_object_type =
    UBUS_OBJECT_TYPE(OBJ_NAME_GENERIC, generic_methods);

static struct ubus_object generic_object = {
    .name = OBJ_NAME_GENERIC,
    .type = &generic_object_type,
    .methods = generic_methods,
    .n_methods = ARRAY_SIZE(generic_methods),
};

// Play thread task and curl callbacks
// In play thread, can't call clear_playlist directly, so we signal the main
// thread to clear the playlist by setting clear_playlist_signal to true
static int beep_generic_curl_progress_cb(void *clientp, double dltotal, double dlnow,
        double ultotal, double ulnow) {
    if (end_current_play_task) {
        LOG_INFO(log_beep_main, "Play task cancelled");

        return -1; // Abort
    }

    CURL *curl_handle = (CURL *)clientp;
    curl_easy_pause(curl_handle, CURLPAUSE_CONT);

    // We sleep here to prevent cpu-burning, this seems to work.
    usleep(25000);

    return CURLE_OK;
}

static size_t beep_generic_curl_write_cb(
        void *ptr, size_t size, size_t nmemb, void *userdata) {
    PlayCtx *ctx = userdata;
    long cc;
    int ret;

    curl_easy_getinfo(ctx->curl_handle, CURLINFO_RESPONSE_CODE, &cc);

    // Some streams respond with something other than the status code,
    // causing the curlcode to be zero. (e.g., "ICY 200 OK")  Presumably,
    // if we are getting this far, the stream is forthcoming, so let's just
    // accept that this is OK and let the decoder handle the extra data
    if (cc && cc != 200) {
        LOG_INFO(log_beep_main, "Error code in write cb: %ld", cc);
        last_token = 0;
        return 0;
    }

    if (end_current_play_task) {
        LOG_INFO(log_beep_main, "Play task cancelled");
        return 0;
    }

    ret = audio_can_buffer(ctx->token, size * nmemb); // Don't block

    if (ret < 0) { // Can't buffer ever
        LOG_INFO(log_beep_main, "Can't buffer ever (token lost?)");
        goto out_token_lost;
    } else if (ret == 0) { // Can't buffer right now
        return CURL_WRITEFUNC_PAUSE;
    }

    if (!audio_buffer(ctx->token, ptr, size * nmemb)) {
        LOG_INFO(log_beep_main, "audio_buffer returned false (token lost?)");
        goto out_token_lost;
    }

    ctx->did_at_least_one_buffer = true;
    return size * nmemb;

out_token_lost:
    clear_playlist_signal = true;
    last_token = 0;
    return 0; // Something bad happened, abort xfer.
}

// Plays until stopped or an error occurs.
// If the perpetual flag is set, we repeat the current item until a skip or
// error is encountered.
static void play_task(void* arg) {
    PlayCtx* ctx = (PlayCtx*)arg;
    int ret;
    int sleep_time;
    struct curl_slist *headers = NULL;
    bool finalize = false;
    CURLcode cc;

    while (true) {
        LOG_INFO(log_beep_main, "Playing url: %s", ctx->url);

        if (!ctx->token) {
            ctx->token = audio_acquire(ctx->app_obj);

            if (ctx->token == -1) {
                LOG_ERROR(log_beep_main, "acquire failed");
                finalize = true;
                goto out;
            }

            last_token = ctx->token;

            if (!set_station(ctx->token,
                        ctx->metadata[METADATA_STATION_NAME] ?: "Unknown Station",
                        ctx->metadata[METADATA_STATION_NAME] ?: "Unknown Station",
                        ctx->metadata[METADATA_STATION_ART_URL],
                        ctx->method, ctx->method_params)) {
                LOG_WARN(log_beep_main, "set_station returned false");
                goto out;
            }

        }

        while (!(ret = audio_can_track_begin(ctx->token)) &&
                !end_current_play_task) {
            if (ret == -1) { // Invalid token
                last_token = 0;
                finalize = true;
                goto out;
            }

            usleep(250000);
        }

        if (!audio_track_begin(ctx->token,
                    NULL, // TODO: What is this?
                    ctx->metadata[METADATA_TRACK_TITLE] ?: "Untitled",
                    ctx->metadata[METADATA_TRACK_ARTIST] ?: "Unknown Artist",
                    ctx->metadata[METADATA_TRACK_ALBUM] ?: "Unknown Album",
                    ctx->metadata[METADATA_ALBUM_ART_URL],
                    ctx->type, 0)) {
            LOG_INFO(log_beep_main, "audio_track_begin returned false");
            goto out;
        }

        ctx->curl_handle = curl_easy_init();

        // Process optional headers parameter
        if (ctx->headers) {
            char *_headers = strdup(ctx->headers);
            for (char *header_ptr = strtok(_headers, "\n"); header_ptr;
                    header_ptr = strtok(NULL, "\n")) {
                headers = curl_slist_append(headers, header_ptr);
            }
            free(_headers);
        }

        // If PLAY_STATION_POST_DATA exists, this request should be performed as a
        // POST with the given POST_DATA
        if (ctx->post_data) {
            curl_easy_setopt(ctx->curl_handle, CURLOPT_POST, 1);
            curl_easy_setopt(ctx->curl_handle, CURLOPT_COPYPOSTFIELDS, ctx->post_data);
        }

        //headers = curl_slist_append(headers, "Icy-MetaData:0");

        // TODO: Play with setting initial-burst header or ?burst=xxxx to request
        //     more data to prebuffer.
        curl_easy_setopt(ctx->curl_handle, CURLOPT_URL, ctx->url);
        // TODO: We should be verifying ssl
        curl_easy_setopt(ctx->curl_handle, CURLOPT_SSL_VERIFYPEER, false);
        curl_easy_setopt(ctx->curl_handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(ctx->curl_handle, CURLOPT_WRITEFUNCTION,
                beep_generic_curl_write_cb);
        curl_easy_setopt(ctx->curl_handle, CURLOPT_PROGRESSFUNCTION,
                beep_generic_curl_progress_cb);
        curl_easy_setopt(ctx->curl_handle, CURLOPT_PROGRESSDATA,
                ctx->curl_handle);
        curl_easy_setopt(ctx->curl_handle, CURLOPT_FOLLOWLOCATION, 1);
        curl_easy_setopt(ctx->curl_handle, CURLOPT_NOPROGRESS, 0);
        curl_easy_setopt(ctx->curl_handle, CURLOPT_WRITEDATA, ctx);
        ctx->did_at_least_one_buffer = false;

        cc = curl_easy_perform(ctx->curl_handle);

        if (cc == CURLE_OK && !ctx->perpetual) {
            LOG_INFO(log_beep_main, "Curl reached end of stream");
            break;
        } else if (ctx->did_at_least_one_buffer && cc != CURLE_WRITE_ERROR &&
                cc != CURLE_ABORTED_BY_CALLBACK) {
            // NOTE: This branch calls play_task recursively so do not set
            // n_retries arbitrarily high
            if (ctx->perpetual || ctx->n_retries < 2) {
                // Cleanup and end this thread but don't free context

                curl_slist_free_all(headers);
                curl_easy_cleanup(ctx->curl_handle);

                ctx->n_retries++;

                sleep_time = ctx->perpetual ? 1 : ctx->n_retries;

                LOG_INFO(log_beep_main,
                        "Restarting station after %d seconds (%d)",
                        sleep_time, cc);

                sleep(sleep_time);

                audio_flush(ctx->token);
                ctx->token = 0;
            } else {
                audio_pause();
                break;
            }
        } else if (cc != 23 && cc != 42) {
            LOG_ERROR(log_beep_main,
                    "Curl error. code: %d  message: %s\n", cc,
                    curl_easy_strerror(cc));
            break;
        } else {
            LOG_WARN(log_beep_main, "Unhandled curl error: %d", cc);
            break;
        }
    }

    audio_track_end(ctx->token);

    curl_slist_free_all(headers);
    curl_easy_cleanup(ctx->curl_handle);

out:
    if (finalize) {
        clear_playlist_signal = true;
    }

    free_play_context(ctx);

    return;
}

static void signal_playlist_advance(void) {
    uint64_t val = 1;
    write(playlist_advance_fd.fd, &val, 8);
}

// (Runs in a new thread)
// Run the play_task function, then write to playlist_advance eventfd
// after it returns
static void* run_play_thread(void* arg) {
    PlayCtx *ctx = (PlayCtx *)arg;
    ctx->token = last_token;
    play_task(ctx);
    signal_playlist_advance();

    return NULL;
}

static void create_play_thread(PlayCtx* ctx) {
    int ret;
    if (play_thread) {
        pthread_join(play_thread, NULL);
        play_thread = 0;
    }
    end_current_play_task = false;

    if ((ret = pthread_create(&play_thread, NULL, run_play_thread, ctx))) {
        LOG_ERROR(log_beep_main, "Failed to create play thread with " \
               "error code => %d", ret);
        abort();
    }
}

static void on_playlist_advanced(struct uloop_fd *fd, unsigned int events) {
    uint64_t val;
    read(playlist_advance_fd.fd, &val, 8);

    if (clear_playlist_signal) {
        clear_playlist();
    }

    if (list_empty(&playlist_head)) {
        LOG_INFO(log_beep_main, "No pending audio items.  Nothing to do.");
        return;
    }

    PlayCtx *p_ctx = list_first_entry(&playlist_head, PlayCtx, head);
    list_del(&p_ctx->head);
    create_play_thread(p_ctx);
}

int main(int argc, char **argv)
{
    log_beep_main = LOG_CATEGORY_GET("app_generic");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    beep_flags_init(argc, argv);
    curl_global_init(CURL_GLOBAL_ALL);
    app_init("app_generic", &generic_object);

    playlist_advance_fd.fd = eventfd(0, 0);
    uloop_fd_add(&playlist_advance_fd, ULOOP_READ);

    app_start();

    if (play_thread && pthread_kill(play_thread, 0) != ESRCH) {
        end_current_play_task = true;
        pthread_join(play_thread, NULL);
    }

    curl_global_cleanup();
    uloop_fd_delete(&playlist_advance_fd);
    close(playlist_advance_fd.fd);

}
