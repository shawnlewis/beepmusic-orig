#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <libubox/uloop.h>

#include "beep/beep_curl.h"
#include "beep/debug.h"

static int stream_count = 3;
static int event_stream_data_count = 2;

int event_data_cb(BeepEventStream *stream, const void *ptr, size_t size,
        void *priv) {
    int psize = (size > 20) ? 20 : (int)size;
    LOG_INFO(log_beep_main, "size: %zu data: %.*s", size, psize,
            (const char *)ptr);

    if (priv == &event_stream_data_count
            && !--event_stream_data_count) {
        beep_curl_event_stream_close(stream);
    }

    return 0;
}

int event_done_cb(BeepEventStream *stream, long code, void *priv) {
    LOG_DEBUG(log_beep_main, "stream: %p priv: %p", stream, priv);
    LOG_INFO(log_beep_main, "stream closing with code: %ld", code);

    if (!--stream_count) {
        LOG_DEBUG(log_beep_main, "no more streams, stopping uloop");
        uloop_end();
    }

    return 0;
}

int event_redirect_cb(BeepEventStream *stream, const char *url, long code,
        void *priv) {
    LOG_DEBUG(log_beep_main, "stream: %p priv: %p", stream, priv);
    LOG_INFO(log_beep_main, "redirecting to %s from code: %ld", url, code);
    return 0;
}

int main(int argc, char **argv) {
    BeepEventStream *stream;

    log_beep_main = LOG_CATEGORY_GET("curl_stream_test");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    uloop_init();
    curl_global_init(CURL_GLOBAL_ALL);
    beep_curl_init();

    // Test redirect callback functionality.
    stream = beep_curl_event_stream(
            "http://jigsaw.w3.org/HTTP/300/307.html",
            event_data_cb,
            event_done_cb,
            event_redirect_cb,
            NULL);
    LOG_DEBUG(log_beep_main, "created stream with redirect callback: %p",
            stream);

    // Test curl redirect functionality.
    stream = beep_curl_event_stream(
            "http://jigsaw.w3.org/HTTP/300/307.html",
            event_data_cb,
            event_done_cb,
            NULL,
            &stream_count);
    LOG_DEBUG(log_beep_main, "created stream curl redirects: %p and priv: %p",
            stream, &stream_count);

    // Test an actual event stream.
    stream = beep_curl_event_stream(
            "https://samplechat.firebaseio-demo.com/users/jack/name.json",
            event_data_cb,
            event_done_cb,
            event_redirect_cb,
            &event_stream_data_count);
    LOG_DEBUG(log_beep_main, "created stream using for real stream: %p",
            stream);

    uloop_run();
    beep_curl_destroy();
    curl_global_cleanup();

    LOG_DEBUG(log_beep_main, "exiting...");
}
