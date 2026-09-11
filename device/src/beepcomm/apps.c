#define _GNU_SOURCE

#include "beepcomm.h"

static char app_resource[] = \
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n" \
    "<service xmlns=\"urn:dial-multiscreen-org:schemas:dial\">\r\n" \
    "    <name>%s</name>\r\n" \
    "    <options allowStop=\"%s\"/>\r\n" \
    "    <appdata xmlns=\"urn:castchat-org:device:app\">\r\n" \
    "        <message-url>ws://%s:%d/castchat/%s</message-url>\r\n" \
    "        <session-id>%d</session-id>\r\n" \
    "%s" /* app_extra */ \
    "    </appdata>\r\n" \
    "    <state>%s</state>\r\n" \
    "%s" /* link_element */ \
    "</service>\r\n";

static char app_extra_f[] =    "        <extra>%s</extra>\r\n";
static char link_element[] = "    <link rel=\"run\" href=\"run\"/>\r\n";

/*
 * TODO: This needs some major review
 */
static void parse_uri(const char *uri, char *app_name, size_t app_name_size,
        char *instance, size_t instance_size) {
    const char *instance_start = uri;
    int i;

    memset(app_name, 0, app_name_size);
    memset(instance, 0, instance_size);

    if(strstr(uri,"/apps/") != uri) {
        return;
    }

    // uri + 6 cuts off trailing /apps/
    strncpy(app_name, uri + 6, app_name_size - 1);
    for(i = 0; app_name[i] != '\0'; i++) {
        if(app_name[i] == '/') {
            app_name[i++] = '\0';
            instance_start = uri + 6 + i;
            break;
        }
    }

    if(instance_start != uri && instance_start[0] != '\0') {
        strncpy(instance, instance_start, instance_size - 1);
    }
}

static int control_app(const char *name, const char *params, bool start) {
    struct blob_attr *response = NULL;
    struct blob_attr *result[__BEEP_RESPONSE_MAX];
    struct blob_buf *args = calloc(1, sizeof(struct blob_buf));
    int ret, _ret = 0;

    blob_buf_init(args, 0);
    blobmsg_add_string(args, "name", name);

    if(start && strlen(params) > 0) {
        LOG_WARN(log_beep_main, "UNIMPLEMENTED: Received params payload: \"%s\"",
                params);
    }

    ret = beep_ubus_invoke("beep.manager",
            start ? "start_app" : "stop_app",
            args->head, &response);

    blob_buf_free(args);
    free(args);

    if(ret) {
        LOG_ERROR(log_beep_main,
                "beep.manager::start/stop_app failed: %s",
                ubus_strerror(ret));
        _ret = -1;
        goto out;
    }

    if(!beep_parse_response(response, result)) {
        LOG_ERROR(log_beep_main,
                "Failed to parse beep.manager::start/stop_app response");
        abort();
    }

    // TODO: Need to expand error reporting here (starting @ beepmanager)
    if(!result[BEEP_RESPONSE_SUCCESS] ||
            !blobmsg_get_bool(result[BEEP_RESPONSE_SUCCESS])) {
        LOG_WARN(log_beep_main, "Failed to %s '%s'",
                (start ? "start" : "stop"),
                name);
        _ret = -1;
    }

out:
    free(response);
    return _ret;
}

static int start_app(const char *name, const char *params) {
    return control_app(name, params, true);
}

static int stop_app(const char *name) {
    return control_app(name, NULL, false);
}

static int request_handler(struct mg_connection *conn) {
    char app_name[APP_NAME_MAXLEN];
    char instance[INSTANCE_MAXLEN];

    const struct mg_request_info *ri = mg_get_request_info(conn);

    const char *host = mg_get_header(conn, "Host");
    if(!host) {
        mgutil_resp_error(conn, 400, "Bad Request");
        return 1;
    }

    if(comm_flags.debug) {
        LOG_DEBUG(log_beep_main, "Host: %s", host);
        LOG_DEBUG(log_beep_main, "URI: %s", ri->uri);
        LOG_DEBUG(log_beep_main, "Method: %s", ri->request_method);
    }

    if(strstr(ri->uri, "/apps") != ri->uri) {
        mgutil_resp_error(conn, 400, "Bad Request");
        return 1;
    }

    parse_uri(ri->uri, app_name, APP_NAME_MAXLEN,
            instance, INSTANCE_MAXLEN);

    if (strlen(app_name) == 0) {
        mgutil_resp_error(conn, 404, "Not Found");
        return 1;
    }

    /* START
     * Must be a POST to /apps/<app_name>
     * Optional body of up to 4096 characters to pass as a parameter
     * 201 Created on success
     * 503 Service Unavailable on failure
     * 413 Request Entity Too Large if body > 4096 characters
     * 400 Bad Request for all other cases
     */

    if(!strcmp(ri->request_method, "POST") &&
            strlen(instance) == 0) {
        char readbuf[4096] = {0,};
        int len = 0;

        const char *content_length = mg_get_header(conn, "Content-Length");
        if(content_length && atoi(content_length) > 0) {
            len = mg_read(conn, readbuf, 4096);
        }

        if(len == 4096) {
            mgutil_resp_error(conn, 413, "Request Entity Too Large");
        } else if(!mgutil_is_valid(readbuf, len)) {
            mgutil_resp_error(conn, 400, "Bad Request");
        }

        int ret = start_app(app_name, readbuf);

        if(ret) { // Error condition
            mgutil_resp_error(conn, 503, "Service Unavailable");
        } else {
            // Can't use mgutil_resp_printf because of extra header info
            mg_printf(conn,
                    "HTTP/1.1 201 Created\r\n"
                    "Content-Type: text/plain; charset=utf-8\r\n"
                    "Content-Length: 0\r\n"
                    "Location: http://%s/apps/%s/run\r\n"
                    "\r\n", host, app_name);
        }
    }

    /* STOP
     * Must be a DELETE to /apps/<app_name>/run
     * 501 Not Implemented if this isn't supported
     * 200 OK if app was running and will be stopped
     * 404 Not found if app was not running
     */
    else if(!strcmp(ri->request_method, "DELETE") &&
            !strcmp(instance, "run")) {
        bool running = get_session_id(app_name) > 0;
        int ret = stop_app(app_name);

        if(!running || ret) {
            mgutil_resp_error(conn, 404, "Not Found");
        } else {
            mg_printf(conn,
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain; charset=utf-8\r\n"
                    "Content-Length: 0\r\n"
                    "\r\n");
        }
    }

    /* INFO REQUEST
     * Must be a GET to /apps/<app_name>
     * 200 OK with app_resource in body if found
     * 404 Not Found if app not found
     */
    else if(!strcmp(ri->request_method, "GET") &&
            strlen(instance) == 0) {
        int session_id = get_session_id(app_name);
        bool running = (session_id > 0);

        if(session_id < 0) { // Not found
            mgutil_resp_error(conn, 404, "Not Found");
        } else {
            char *app_extra = NULL;
            // TODO: This might be slow?
            AppConfig app = beep_config_app_load(app_name);
            if(!app) {
                LOG_INFO(log_beep_main,
                        "Failed to load config for %s (this is normal "
                        "for non-js apps", app_name);
            } else {
                const char *app_extra_data =
                    beep_config_app_get_dial_extra(app);

                if(app_extra_data &&
                        asprintf(&app_extra, app_extra_f,
                        app_extra_data) == -1) {
                    LOG_WARN(log_beep_main,
                            "Failed to allocate dial_extra string");
                }
            }
            mgutil_resp_printf(conn, 200, "OK",
                    "text/xml; charset=utf-8",
                    app_resource, app_name, "true",
                    get_address(host), comm_flags.msg_port,
                    app_name, session_id,
                    app_extra != NULL ? app_extra : "",
                    running ? "running" : "stopped",
                    running ? link_element : "");
            free(app_extra);
            beep_config_app_free(app);
        }
    }

    /* INVALID REQUEST */
    else {
        mgutil_resp_error(conn, 400, "Bad Request");
    }
    return 1;
}

struct apps_context *start_apps_api(void) {
    struct apps_context *apps_ctx = calloc(1, sizeof(struct apps_context));
    struct mg_callbacks callbacks;
    char port_str[10];
    const char *options[] = {
        "listening_ports", port_str,
        "num_threads", "3",
        NULL
    };

    snprintf(port_str, 10, "%d", comm_flags.dial_port);

    memset(&callbacks, 0, sizeof(struct mg_callbacks));
    callbacks.begin_request = request_handler;


    apps_ctx->mg_ctx = mg_start(&callbacks, apps_ctx, options);
    if(!apps_ctx->mg_ctx) {
        LOG_ERROR(log_beep_main, "Couldn't start mongoose");
        abort();
    }
    LOG_INFO(log_beep_main, "apps REST api @ http://localhost:%d/apps/",
            comm_flags.dial_port);
    return apps_ctx;
}

void stop_apps_api(struct apps_context *apps_ctx) {
    mg_stop(apps_ctx->mg_ctx);
    LOG_INFO(log_beep_main, "apps REST api server stopped");
    free(apps_ctx);
}
