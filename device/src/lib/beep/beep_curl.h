#ifndef LIBBEEP_BEEP_CURL_H
#define LIBBEEP_BEEP_CURL_H

#include <curl/curl.h>

/* USAGE
 *
 * Note: This library depends on uloop.
 *
 * 1. Call beep_curl_init()
 * 2. Create curl handles (CURL *) using curl_easy_*, including callbacks, etc.
 * 3. Add curl handles with beep_curl_perform(...).  Handles are automatically
 *    removed when they are "done."
 * 4. Users of this library are not allowed to use CURLOPT_PRIVATE.
 */

#define BEEP_CURL_KEEP (1)
#define BEEP_CURL_CLEANUP (0)

typedef int (*beep_curl_done_cb)(CURL *easy, void *priv);

int beep_curl_init(void);
int beep_curl_destroy(void);
int beep_curl_perform(CURL *curl_handle, beep_curl_done_cb cb, void *priv);

typedef struct BeepEventStream BeepEventStream;

// Returning non-zero values from the callbacks will cause the
// stream to be closed.
typedef int (*beep_curl_event_data_cb)(BeepEventStream *stream,
        const void *ptr, size_t size, void *priv);
typedef int (*beep_curl_event_done_cb)(BeepEventStream *stream,
        long code, void *priv);
typedef int (*beep_curl_event_redirect_cb)(BeepEventStream *stream,
        const char *url, long code, void *priv);

// Opens an event stream.
// If url starts with https:// SSL will be enabled.
// redirect_cb and priv may be NULL if not needed.
// If redirect_cb is not set curl will automatically follow redirects.
// Returns a pointer to the event stream data or NULL on error.
// After returning from done_cb the BeepEventStream pointer is no longer
// valid.
BeepEventStream *beep_curl_event_stream(const char *url,
        beep_curl_event_data_cb data_cb,
        beep_curl_event_done_cb done_cb,
        beep_curl_event_redirect_cb redirect_cb,
        void *priv);

// Immediately closes and cleans up an event stream.
// This probably is not thread safe and should be called from the
// same thread curl is running in.
int beep_curl_event_stream_close(BeepEventStream *stream);

#endif  // LIBBEEP_BEEP_CURL_H
