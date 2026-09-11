#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <libubox/uloop.h>
#include <libubox/list.h>

#include "debug.h"
#include "beep_curl.h"
#include "http_codes.h"

// #define BEEP_CURL_TRACE

typedef struct beep_curl_callback {
    beep_curl_done_cb cb;
    void *priv;
} beep_curl_callback;

// Private library variables
static CURLM *multi_handle = NULL;
static struct uloop_timeout beep_curl_sock_timeout;

// Callback forward declarations
// CURL
static int beep_curl_timeout_callback(CURLM *multi, long timeout_ms,
        void *userp);
static int beep_curl_socket_callback(CURL *easy, curl_socket_t s, int action,
        void *userp, void *socketp);

// ULOOP
static void beep_curl_sock_timeout_cb(struct uloop_timeout *t);
static void beep_curl_fd_handler(struct uloop_fd *u, unsigned int events);

// Initialize the multi_handle and install socket event callbacks
int beep_curl_init(void) {
    int ret;

    if(multi_handle != NULL) {
        LOG_ERROR(log_beep_main, "Programming error: beep_curl_init called " \
                "more than once");
        abort();
    }

    multi_handle = curl_multi_init();
    if(multi_handle == NULL) {
        ret = -1;
        goto out_error;
    }

    ret = curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION,
            &beep_curl_socket_callback);
    if(ret) {
        LOG_ERROR(log_beep_main, "Failed to set multi SOCKETFUNCTION: %d", ret);
        goto out_error;
    }

    ret = curl_multi_setopt(multi_handle, CURLMOPT_TIMERFUNCTION,
            &beep_curl_timeout_callback);
    if(ret) {
        LOG_ERROR(log_beep_main, "Failed to set multi TIMERFUNCTION: %d", ret);
        goto out_error;
    }

    memset(&beep_curl_sock_timeout, 0, sizeof(struct uloop_timeout));
    beep_curl_sock_timeout.cb = &beep_curl_sock_timeout_cb;

#ifdef BEEP_CURL_TRACE
    LOG_INFO(log_beep_main, "beep_curl_init OK");
#endif

    return 0;

out_error:
    if(multi_handle) {
        curl_multi_cleanup(multi_handle);
        multi_handle = NULL;
    }

    return ret;
}

/*
 * To properly clean up curl, MUST call curl_multi_remove_handle on all easy
 * handles, then clean those easy handles up individually, THEN call
 * curl_multi_cleanup
 *
 * It's up to the user to ensure that all easy handles have been removed
 */
int beep_curl_destroy(void) {
    if(multi_handle == NULL) {
        LOG_ERROR(log_beep_main, "Programming error: beep_curl_destroy " \
                "called but beep_curl was not initialized");
        abort();
    }

    uloop_timeout_cancel(&beep_curl_sock_timeout);

    if(curl_multi_cleanup(multi_handle) != CURLM_OK) {
        return -1;
    }

    multi_handle = NULL;
    return 0;
}

int beep_curl_perform(CURL *curl_handle, beep_curl_done_cb cb, void *priv) {
    int ret;
    void *handle_priv;
    beep_curl_callback *callback;

    // multi_handle is initialized by beep_curl_init()
    assert(multi_handle != NULL);

    // users of this library may not use CURLOPT_PRIVATE
    // We clear this out when we call the callback
    if((ret = curl_easy_getinfo(curl_handle, CURLINFO_PRIVATE,
            (char **)&handle_priv)) != CURLE_OK) {
        LOG_ERROR(log_beep_main, "Failed to get CURLINFO_PRIVATE for handle: " \
                "%p (%d)", curl_handle, ret);
        return -1;
    }
    assert(!handle_priv);

    if(!(callback = malloc(sizeof(beep_curl_callback)))) {
        LOG_ERROR(log_beep_main, "Failed to allocate beep_curl_callback: " \
                "%s (%d)", strerror(errno), errno);
        abort();
    }

    callback->cb = cb;
    callback->priv = priv;

    curl_easy_setopt(curl_handle, CURLOPT_PRIVATE, callback);

    if((ret = curl_multi_add_handle(multi_handle, curl_handle)) != CURLM_OK) {
        LOG_ERROR(log_beep_main, "Error adding handle %p to multi_handle: %d",
                curl_handle, ret);
        return -1;
    }

#ifdef BEEP_CURL_TRACE
    LOG_INFO(log_beep_main, "Added curl_handle %p", curl_handle);
#endif
    return 0;
}

/*
 * Socket management -- this is mostly isolated from the public interface
 * and the rest of this library
 */

typedef struct socket_context {
    struct uloop_fd ufd;
    curl_socket_t sockfd;
} socket_context;

static socket_context *create_socket_context(curl_socket_t s) {
    socket_context *context;

    context = calloc(1, sizeof(socket_context));
    if(!context) {
        LOG_ERROR(log_beep_main, "Failed to allocate socket_context: %s (%d)",
                strerror(errno), errno);
        abort();
    }

    context->sockfd = s;
    context->ufd.fd = s;
    context->ufd.cb = &beep_curl_fd_handler;

    return context;
}

static void destroy_socket_context(socket_context *context) {
    free(context);
}

/*
 * This is called from the uloop socket and timeout callbacks
 * to consume messages from curl_multi_info_read.  If a callback was passed
 * to beep_curl_perform, call it to determine if we should clean up.  Otherwise,
 * always clean up.
 */
static void check_multi_info(void) {
    int ret;
    CURLMsg *message;
    beep_curl_callback *callback;

#ifdef BEEP_CURL_TRACE
    char *url;
    long resp_code;
#endif

    int pending;

    while((message = curl_multi_info_read(multi_handle, &pending))) {
        switch(message->msg) {
        case CURLMSG_DONE:
#ifdef BEEP_CURL_TRACE
            curl_easy_getinfo(message->easy_handle,
                    CURLINFO_EFFECTIVE_URL, &url);
            curl_easy_getinfo(message->easy_handle,
                    CURLINFO_RESPONSE_CODE, &resp_code);
            LOG_INFO(log_beep_main, "Done: %s (%ld)", url, resp_code);
#endif

            // Remove easy_handle from multi_handle
            if((ret = curl_multi_remove_handle(multi_handle,
                    message->easy_handle)) != CURLM_OK) {
                LOG_ERROR(log_beep_main, "Error removing handle %p from " \
                        "multi_handle: %d", message->easy_handle, ret);
                abort();
            }

            // Retrieve callback struct from easy_handle private
            if((ret = curl_easy_getinfo(message->easy_handle,
                    CURLINFO_PRIVATE, (char **)&callback)) != CURLE_OK) {
                LOG_ERROR(log_beep_main, "Failed to get CURLINFO_PRIVATE " \
                        "for handle: %p (%d)", message->easy_handle, ret);
                abort();
            }

            // Clear easy_handle private prior to calling cb in case we
            // want to add it back again from the done cb
            if((ret = curl_easy_setopt(message->easy_handle, CURLOPT_PRIVATE,
                    NULL)) != CURLE_OK) {
                LOG_ERROR(log_beep_main, "Failed to clear " \
                        "CURLOPT_PRIVATE for handle: %p (%d)",
                        message->easy_handle, ret);
            }

            // Call callback if it exists and don't clean up if it returns
            // BEEP_CURL_KEEP.  Otherwise, clean up!
            ret = BEEP_CURL_CLEANUP;
            if(callback->cb) {
                ret = callback->cb(message->easy_handle, callback->priv);
            }

            if(ret == BEEP_CURL_CLEANUP) {
                curl_easy_cleanup(message->easy_handle);
            }

            free(callback);
            break;

        default:
            LOG_WARN(log_beep_main, "Unhandled CURLMsg %d for handle %p",
                    message->msg, message->easy_handle);
            break;
        }
    }
}

// ULOOP Socket callback
static void beep_curl_fd_handler(struct uloop_fd *u, unsigned int events) {
    int ret;
    int running_handles;
    int flags = 0;
    socket_context *context = container_of(u, socket_context, ufd);

    uloop_timeout_cancel(&beep_curl_sock_timeout);

    if(events & ULOOP_READ) {
        flags |= CURL_CSELECT_IN;
    }

    if(events & ULOOP_WRITE) {
        flags |= CURL_CSELECT_OUT;
    }

    if((ret = curl_multi_socket_action(multi_handle, context->sockfd, flags,
            &running_handles)) != CURLM_OK) {
        LOG_ERROR(log_beep_main, "Error calling curl_multi_socket_action: %d",
                ret);
        abort();
    }

    check_multi_info();
}

// ULOOP Timeout callback
static void beep_curl_sock_timeout_cb(struct uloop_timeout *t) {
    int ret;
    int running_handles;

    if((ret = curl_multi_socket_action(multi_handle, CURL_SOCKET_TIMEOUT, 0,
            &running_handles)) != CURLM_OK) {
        LOG_ERROR(log_beep_main, "Error calling curl_multi_socket_action: %d",
                ret);
        abort();
    }

    check_multi_info();
}

// CURL Socket callback
static int beep_curl_socket_callback(CURL *easy, curl_socket_t s, int action,
         void *userp, void *socketp) {
    int ret;
    socket_context *context;

    if(action != CURL_POLL_REMOVE) {
        if(socketp) {
            context = (socket_context *)socketp;
        } else {
            context = create_socket_context(s);
        }
        if((ret = curl_multi_assign(multi_handle, s, context)) != CURLM_OK) {
            LOG_ERROR(log_beep_main, "Error assigning private data to socket " \
                    "%d: %d", s, ret);
            return -1;
        }
    }

    switch(action) {
    case CURL_POLL_IN:
        uloop_fd_add(&context->ufd, ULOOP_READ);
        break;

    case CURL_POLL_OUT:
        uloop_fd_add(&context->ufd, ULOOP_WRITE);
        break;

    case CURL_POLL_INOUT:
        uloop_fd_add(&context->ufd, ULOOP_READ|ULOOP_WRITE);
        break;

    case CURL_POLL_REMOVE:
        if(socketp) {
            uloop_fd_delete(&((socket_context *)socketp)->ufd);
            destroy_socket_context((socket_context *)socketp);
            if((ret = curl_multi_assign(multi_handle, s, NULL)) != CURLM_OK) {
                LOG_ERROR(log_beep_main, "Error clearing private data for " \
                        "socket %d: %d", s, ret);
                return -1;
            }
        } else {
            LOG_WARN(log_beep_main, "CURL_POLL_REMOVE for inactive socket %d",
                    s);
        }
        break;

    default:
        LOG_ERROR(log_beep_main, "Unknown action %d", action);
        return -1;
    }

    return 0;
}

// CURL Timeout callback
static int beep_curl_timeout_callback(CURLM *multi, long timeout_ms,
        void *userp) {
    if(timeout_ms <= 0) {
        timeout_ms = 1;
    }

    return uloop_timeout_set(&beep_curl_sock_timeout, timeout_ms);
}

struct BeepEventStream {
    CURL *easy_handle;
    beep_curl_event_data_cb data_cb;
    beep_curl_event_done_cb done_cb;
    beep_curl_event_redirect_cb redirect_cb;
    void *priv;
    struct curl_slist *header_slist;
    int redirects;
    bool abort;
};

static const char https_url_str[] = "https://";

#define BEEP_CURL_MAX_REDIRECTS                     (10)
#define HTTP_CODE_IS_REDIRECT(__CODE__) \
    (__CODE__ == HTTP_CODE_MOVED_PERMANENTLY \
    || __CODE__ == HTTP_CODE_TEMPORARY_REDIRECT)

static void event_stream_free(BeepEventStream *stream) {
    if (stream) {
        if (stream->header_slist) {
            curl_slist_free_all(stream->header_slist);
        }
        free(stream);
    }
}

static size_t event_stream_write_cb(void *ptr, size_t size, size_t nmemb,
        void *userdata) {
    BeepEventStream *stream = userdata;
    long resp_code;
    int ret;

    // Need to wait for another write callback until we can tell
    // libcurl we want to close the connection.
    if (stream->abort) {
        return 0;
    }

    // Server can return data with the redirect code.  Do not
    // send this up to the caller.
    curl_easy_getinfo(stream->easy_handle, CURLINFO_RESPONSE_CODE,
            &resp_code);
    if (HTTP_CODE_IS_REDIRECT(resp_code)) {
        return size * nmemb;
    }

    ret = stream->data_cb(stream, ptr, size * nmemb, stream->priv);

    // If callback was non-zero abort this connection in libcurl.
    return (ret) ? 0 : size * nmemb;
}

static int event_stream_done_cb(CURL *easy, void *priv) {
    BeepEventStream *stream = priv;
    char *redirect_url;
    long resp_code;
    int ret = 1;
    CURLcode cc;

    curl_easy_getinfo(stream->easy_handle, CURLINFO_RESPONSE_CODE,
            &resp_code);

    // libcurl does not have a redirect callback instead it will
    // just close the connection and say done.
    // If CURLOPT_FOLLOWLOCATION was never set it is up to the
    // user to decide what to do.
    // If this stream has been closed by the user do not try
    // to do a redirect.
    if (HTTP_CODE_IS_REDIRECT(resp_code)
            && stream->redirect_cb
            && !stream->abort) {
        if (++(stream->redirects) <= BEEP_CURL_MAX_REDIRECTS) {
            curl_easy_getinfo(stream->easy_handle, CURLINFO_REDIRECT_URL,
                    &redirect_url);

            LOG_DEBUG(log_beep_main, "redirecting to: %s", redirect_url);

            // By the time this is called check_multi_info will have
            // already removed the easy handle from the multi handle.
            // Update the url and call beep_curl_perform again.  Then
            // return BEEP_CURL_KEEP so check_multi_info will not
            // cleanup the easy handle.
            cc = curl_easy_setopt(stream->easy_handle, CURLOPT_URL,
                    redirect_url);
            // If this has an error we will call the parent done_cb below.
            if (cc == CURLE_OK) {
                ret = stream->redirect_cb(stream, redirect_url, resp_code,
                        stream->priv);
            }
            // If callback returned non-zero they do not want this stream
            // anymore.
            if (ret == 0) {
                beep_curl_perform(stream->easy_handle, &event_stream_done_cb,
                        stream);
                return BEEP_CURL_KEEP;
            }
        } else {
            LOG_WARN(log_beep_main, "too many redirects");
        }
    }

    stream->done_cb(stream, resp_code, stream->priv);

    event_stream_free(stream);

    return BEEP_CURL_CLEANUP;
}

BeepEventStream *beep_curl_event_stream(const char *url,
        beep_curl_event_data_cb data_cb,
        beep_curl_event_done_cb done_cb,
        beep_curl_event_redirect_cb redirect_cb,
        void *priv) {
    CURL *easy_handle;
    BeepEventStream *stream;
    struct curl_slist *header_slist = NULL;
    CURLcode cc = CURLE_OK;

    if (!url || !data_cb || !done_cb) {
        return NULL;
    }

    easy_handle = curl_easy_init();
    if (!easy_handle) {
        return NULL;
    }

    stream = malloc(sizeof(BeepEventStream));
    if (!stream) {
        return NULL;
    }
    memset(stream, 0, sizeof(BeepEventStream));

    header_slist = curl_slist_append(header_slist, "Accept: text/event-stream");
    if (header_slist) {
        cc = curl_easy_setopt(easy_handle, CURLOPT_HTTPHEADER, header_slist);
    }

    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
            CURLOPT_URL, url);
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
            CURLOPT_WRITEFUNCTION, event_stream_write_cb);
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
            CURLOPT_WRITEDATA, stream);
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
            CURLOPT_SSL_VERIFYPEER, 0L);
    //cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
    //        CURLOPT_VERBOSE, 1L);

    // Enable SSL if the url starts with https.
    if (!strncmp(https_url_str, url, sizeof(https_url_str) - 1)
            && cc == CURLE_OK) {
        cc = curl_easy_setopt(easy_handle, CURLOPT_USE_SSL, CURLUSESSL_ALL);
    }

    // If the caller does use the redirect callback use curl to
    // follow the redirects.
    if (!redirect_cb && cc == CURLE_OK) {
        cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
                CURLOPT_FOLLOWLOCATION, 1L);
        cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
                CURLOPT_MAXREDIRS, (long)BEEP_CURL_MAX_REDIRECTS);
    }

    if (cc != CURLE_OK) {
        if (easy_handle) {
            curl_easy_cleanup(easy_handle);
        }
        if (stream) {
            free(stream);
        }
        return NULL;
    }

    stream->easy_handle = easy_handle;
    stream->data_cb = data_cb;
    stream->done_cb = done_cb;
    stream->redirect_cb = redirect_cb;
    stream->priv = priv;
    stream->header_slist = header_slist;

    beep_curl_perform(stream->easy_handle, &event_stream_done_cb, stream);

    return stream;
}

int beep_curl_event_stream_close(BeepEventStream *stream) {
    if (!stream) {
        return 1;
    }
    stream->abort = true;
    return 0;
}

