#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <libubox/uloop.h>

#include "beep/beep_curl.h"
#include "beep/debug.h"

static char *userdata = "Hello this is test";
static int handles;

static char *targets[] = {
    "http://www.google.com",
    "http://www.yahoo.com",
    "http://www.bing.com",
    "http://www.twitter.com",
    "http://www.facebook.com",
    "http://www.example.com",
    "http://localhost:1234",
    NULL};

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    CURL *handle = (CURL *)userdata;
    char *url;

    curl_easy_getinfo(handle, CURLINFO_EFFECTIVE_URL, &url);

    LOG_INFO(log_beep_main, "Received %zd bytes from URL %s", size * nmemb, url);
    return size * nmemb;
}

static int done_cb(CURL *easy, void *priv) {
    char *url;
    long resp_code;

    curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &url);
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &resp_code);
    LOG_INFO(log_beep_main, "Done: %s %ld -- %s", url, resp_code, (char *)priv);

    if (!--handles) {
        LOG_DEBUG(log_beep_main, "no more handles, stopping uloop");
        uloop_end();
    }

    return BEEP_CURL_CLEANUP;
}

int main(int argc, char **argv) {
    CURL *handle;

    log_beep_main = LOG_CATEGORY_GET("curl_test");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    uloop_init();
    curl_global_init(CURL_GLOBAL_ALL);
    beep_curl_init();

    int i=0;
    while(targets[i]) {
        handle = curl_easy_init();
        curl_easy_setopt(handle, CURLOPT_URL, targets[i++]);
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &write_cb);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, handle);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 5);
        beep_curl_perform(handle, &done_cb, userdata);
    }

    handles = i;

    uloop_run();
    beep_curl_destroy();
    curl_global_cleanup();

    LOG_INFO(log_beep_main, "Clean exit.");
}
