#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "beepupdate.h"
#include "yaml_utils.h"

#define SCHEDULE_INVALID_STR    "invalid"
#define SCHEDULE_NORMAL_STR     "normal"
#define SCHEDULE_IMMEDIATE_STR  "immediate"
#define SCHEDULE_DATE_STR       "date"

#define MAX_SERVER_ERROR_MSG    512

#define GET_FORM_DATA_RELEASE   7
#define GET_FORM_DATA_FILE      9

static const char https_url_str[] = "https://";

// form_data are name/content pairs, form_count is number of pairs
// (i.e. sizeof(form_data)/sizeof(char *) == form_count * 2
static FILE *server_post(const char *url, char **form_data, int form_count,
        UpdateCode *code) {
    FILE *stream = NULL;
    CURL *easy_handle = NULL;
    struct curl_httppost *formpost = NULL;
    struct curl_httppost *formpost_end = NULL;
    const char *curl_what = "setup";
    char *server_msg;
    size_t rsize;
    long http_code;
    int index;
    UpdateCode rcode = UPDATE_OK;
    CURLcode cc = CURLE_OK;
    CURLFORMcode cfc = CURL_FORMADD_OK;

    if (!url) {
        rcode = UPDATE_BAD_ARG;
        goto done;
    }

    // Using CURLOPT_USE_SSL does nothing if the url is http. Enforce that
    // all server posts are going through https.  -1 for the NULL char.
    if (strncmp(https_url_str, url, sizeof(https_url_str) - 1)) {
        LOG_ERROR("invalid url");
        rcode = UPDATE_BAD_ARG;
        goto done;
    }

    stream = tmpfopen("w+");
    if (!stream) {
        rcode = UPDATE_FILE_ERR;
        goto done;
    }

    for (index = 0; index < form_count; index++) {
        if (strcmp(form_data[(index * 2)], "device_auth")) {
            LOG_DEBUG("form_data: %s=%s", form_data[(index * 2)],
                    form_data[(index * 2) + 1]);
        }
        cfc = curl_formadd(&formpost,
                &formpost_end,
                CURLFORM_COPYNAME, form_data[(index * 2)],
                CURLFORM_COPYCONTENTS, form_data[(index * 2) + 1],
                CURLFORM_END);
    }
    if (cfc != CURL_FORMADD_OK) {
        LOG_ERROR("cfc: %d", cfc);
        rcode = UPDATE_CURL_ERR;
        goto done;
    }

    easy_handle = curl_easy_init();
    if (!easy_handle) {
        rcode = UPDATE_CURL_ERR;
        goto done;
    }

    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
            CURLOPT_WRITEDATA, stream);
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
            CURLOPT_URL, url);
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
            CURLOPT_HTTPPOST, formpost);
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
            CURLOPT_USE_SSL, CURLUSESSL_ALL);
    //cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
    //        CURLOPT_VERBOSE, 1L);

    if (cc == CURLE_OK) {
        curl_what = "perform";
        cc = curl_easy_perform(easy_handle);
    }

    if (cc == CURLE_OK) {
        curl_what = "getinfo";
        cc = curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE,
                &http_code);
    }

    if (cc != CURLE_OK) {
        LOG_ERROR("curl %s: %s (%d)", curl_what, curl_easy_strerror(cc), cc);
        rcode = UPDATE_CURL_ERR;
        goto done;
    }

    fseek(stream, 0, SEEK_SET);
    // Update current pos in fd.
    fflush(stream);

    if (http_code == 204) {
        rcode = UPDATE_HTTP_204;
    } else if (http_code != 200) {
        server_msg = (char *)bmalloc(MAX_SERVER_ERROR_MSG);
        if (server_msg) {
            rsize = fread(server_msg, 1, MAX_SERVER_ERROR_MSG, stream);
        }
        LOG_ERROR("code: %ld msg: %.*s", http_code, MAX_SERVER_ERROR_MSG,
                server_msg ? rsize ? server_msg : "none" : "oom");
        if (server_msg)
            bfree(server_msg);
        rcode = UPDATE_HTTP_ERR;
    }

done:
    if (rcode != UPDATE_OK && stream) {
        tmpfunclose(stream);
        stream = NULL;
    }

    if (easy_handle)
        curl_easy_cleanup(easy_handle);

    if (formpost)
        curl_formfree(formpost);

    if (code)
        *code = rcode;

    return stream;
}

void dump_config(UpdateConfig *uc) {
    if (!uc) {
        LOG_ERROR("uc is NULL");
        return;
    }

    LOG_INFO("UpdateConfig: %p", uc);
    LOG_INFO("  uc->preinst: %s", uc->preinst);
    LOG_INFO("  uc->install: %s", uc->install);
    LOG_INFO("  uc->postinst: %s", uc->postinst);
    LOG_INFO("  uc->release: %s", uc->release);
    LOG_INFO("  uc->schedule: %d", uc->schedule);
    LOG_INFO("  uc->force: %d", uc->force);
}

void cleanup_config(UpdateConfig *uc) {
    if (uc) {
        if (uc->server)
            bfree(uc->server);
        if (uc->preinst)
            bfree(uc->preinst);
        if (uc->install)
            bfree(uc->install);
        if (uc->postinst)
            bfree(uc->postinst);
        if (uc->release)
            bfree(uc->release);
        bfree(uc);
    }
}

static UpdateConfigSchedule str_to_sched(const char *str) {
    if (!strcmp(str, SCHEDULE_NORMAL_STR)) {
        return SCHEDULE_NORMAL;
    } else if (!strcmp(str, SCHEDULE_IMMEDIATE_STR)) {
        return SCHEDULE_IMMEDIATE;
    } else if (!strcmp(str, SCHEDULE_DATE_STR)) {
        return SCHEDULE_DATE;
    } else {
        return SCHEDULE_INVALID;
    }
}

static UpdateConfig *parse_update_config(yaml_parser_t *parser,
        UpdateCode *code) {
    enum {
        TL_MAP_START,
        TL_MAP_KEYS,
        TL_MAP_VALUE_STR,  // install, post, pre, release
        TL_MAP_VALUE_SCHEDULE,
        TL_MAP_VALUE_FORCE,
        __IGNORE_BLOCK,
        __UNKNOWN_STATE
    };

    UpdateConfig *uc;
    UpdateCode rcode = UPDATE_OK;
    char **dest = NULL;

    yaml_event_t event;
    ParseControl *ctrl = NULL;
    PARSE_CONTROL_INIT(ctrl, TL_MAP_START);

    uc = (UpdateConfig *)bmalloc(sizeof(UpdateConfig));
    if (!ctrl || !uc) {
        PARSE_CONTROL_CLEANUP(ctrl);
        if (uc)
            bfree(uc);
        *code = UPDATE_OOM;
        return NULL;
    }

    while (ctrl->p_err == POK_READY) {
        if (!yaml_parser_parse(parser, &event)) {
            PARSE_SET_ERROR(ctrl, PERR_YAML);
            break;
        }

        PARSE_DUMP_STATE(ctrl, event);

        if (event.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&event);
            ctrl->count++;
            PARSE_SET_ERROR(ctrl, PERR_STREAM_END);
            break;
        }

        switch (PARSE_CURRENT_STATE(ctrl)) {
        case TL_MAP_START: {
            if (event.type == YAML_MAPPING_START_EVENT) {
                PARSE_CHANGE_STATE(ctrl, TL_MAP_KEYS);
            } else if (event.type == YAML_STREAM_START_EVENT ||
                    event.type == YAML_DOCUMENT_START_EVENT) {
                // Ignore the stream/document start events.
                continue;
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_MAPPING_START_EVENT);
            }
            break;
        }

        case TL_MAP_KEYS: {
            if (event.type == YAML_SCALAR_EVENT) {
                if (PARSE_SCALAR_EQ("install", event)) {
                    dest = &uc->install;
                    PARSE_PUSH_STATE(ctrl, TL_MAP_VALUE_STR);
                } else if (PARSE_SCALAR_EQ("post", event)) {
                    dest = &uc->postinst;
                    PARSE_PUSH_STATE(ctrl, TL_MAP_VALUE_STR);
                } else if (PARSE_SCALAR_EQ("pre", event)) {
                    dest = &uc->preinst;
                    PARSE_PUSH_STATE(ctrl, TL_MAP_VALUE_STR);
                } else if (PARSE_SCALAR_EQ("release", event)) {
                    dest = &uc->release;
                    PARSE_PUSH_STATE(ctrl, TL_MAP_VALUE_STR);
                } else if (PARSE_SCALAR_EQ("schedule", event)) {
                    PARSE_PUSH_STATE(ctrl, TL_MAP_VALUE_SCHEDULE);
                } else if (PARSE_SCALAR_EQ("force", event)) {
                    PARSE_PUSH_STATE(ctrl, TL_MAP_VALUE_FORCE);
                } else {
                    PARSE_IGNORE_BLOCK(ctrl);
                }
            } else if (event.type == YAML_MAPPING_END_EVENT) {
                PARSE_DONE(ctrl);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_SCALAR_EVENT);
            }
            break;
        }

        case TL_MAP_VALUE_STR: {
            if (dest == NULL) {
                PARSE_SET_ERROR(ctrl, PERR_STATE);
            } else {
                if (event.type == YAML_SCALAR_EVENT) {
                    *dest = bstrdup((const char *)event.data.scalar.value);
                    PARSE_POP_STATE(ctrl);
                } else {
                    PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_SCALAR_EVENT);
                }
                dest = NULL;
            }
            break;
        }

        case TL_MAP_VALUE_SCHEDULE: {
            if (event.type == YAML_SCALAR_EVENT) {
                uc->schedule = str_to_sched(
                        (const char *)event.data.scalar.value);
                PARSE_POP_STATE(ctrl);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_SCALAR_EVENT);
            }
            break;
        }

        case TL_MAP_VALUE_FORCE: {
            if (event.type == YAML_SCALAR_EVENT) {
                if (((const char *)event.data.scalar.value)[0] == '1'
                    && ((const char *)event.data.scalar.value)[1] == '\0') {
                    uc->force = true;
                }
                PARSE_POP_STATE(ctrl);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_SCALAR_EVENT);
            }
            break;
        }

        case __IGNORE_BLOCK: {
            PARSE_IGNORE_CASE(event, ctrl, ignore_depth);
            break;
        }

        default:
            PARSE_SET_ERROR(ctrl, PERR_PARSE);
            PARSE_CHANGE_STATE(ctrl, __UNKNOWN_STATE);
            break;
        }

        yaml_event_delete(&event);
        ctrl->count++;
    }

    if (ctrl->p_err != POK_READY && ctrl->p_err != POK_DONE) {
        log_parse_error(ctrl);
        if (rcode == UPDATE_OK)
            rcode = UPDATE_YAML_ERR;
    }

    PARSE_CONTROL_CLEANUP(ctrl);

    *code = rcode;

    return uc;
}

UpdateConfig *server_get_config(const char *server, const char *port,
        UpdateCode *code) {
    UpdateConfig *uc = NULL;
    FILE *stream = NULL;
    char *url = NULL;
    yaml_parser_t parser;
    size_t server_size;
    UpdateCode rcode = UPDATE_OK;

    char *form_data[] = {
        "device_id", sysconfig->device_id,
        "device_auth", sysconfig->device_auth,
        "system", sysconfig->sys_ver,
        "current", sysconfig->beep_ver,
        "requested", sysconfig->requested,
        "recovery", (sysconfig->state & STATE_RECOVERY_PART) ? "true" : "false"
    };

    if (!server) {
        rcode = UPDATE_BAD_ARG;
        goto done;
    }

    server_size = strlen(server);
    if (server[server_size - 1] == '/')
        server_size--;

    if (asprintf(&url, "https://%.*s:%s/1/update/config", (int)server_size,
            server, port) == -1) {
        rcode = UPDATE_OOM;
        goto done;
    }

    if (!yaml_parser_initialize(&parser)) {
        rcode = UPDATE_YAML_ERR;
        goto done;
    }

    LOG_DEBUG("fetching update config");
    stream = server_post(url, form_data, sizeof(form_data)/sizeof(char *)/2, &rcode);
    if (stream) {
        yaml_parser_set_input_file(&parser, stream);
        uc = parse_update_config(&parser, &rcode);
        tmpfunclose(stream);
    }

    yaml_parser_delete(&parser);

    if (rcode == UPDATE_HTTP_204) {
        LOG_INFO("device up to date");
        rcode = UPDATE_UP_TO_DATE;
        goto done;
    }

    if (uc &&
            (!uc->preinst
            || !uc->install
            || !uc->postinst
            || !uc->release
            || uc->schedule == SCHEDULE_INVALID)) {
        LOG_ERROR("incomplete config");
        rcode = UPDATE_ERR;
    } else if (uc) {
        // Copy server from the url since it already has https:// appended to
        // it.  Note: If the update server wants to set an alternative server
        // in the update config the override would happen here.
        // - 1: Remove null char from sizeof(https_url_str).
        // + 1: Add ':' for port define.
        uc->server = bstrndup(url, server_size + sizeof(https_url_str) - 1
                + strlen(port) + 1);
        if (!uc->server)
            rcode = UPDATE_OOM;
    }

done:
    if (rcode != UPDATE_OK && uc) {
        cleanup_config(uc);
        uc = NULL;
    }

    if (url)
        free(url);

    if (code)
        *code = rcode;

    return uc;
}

char *server_get_file(UpdateConfig *uc, UpdateGetSelection selection,
        UpdateCode *code) {
    FILE *stream = NULL;
    char *url = NULL;
    char *dlfile = NULL;
    UpdateCode rcode = UPDATE_OK;

    char *form_data[] = {
        "device_id", sysconfig->device_id,
        "device_auth", sysconfig->device_auth,
        "system", sysconfig->sys_ver,
        "release", NULL,
        "file", NULL
    };

    if (!uc || !uc->server || !uc->release) {
        rcode = UPDATE_BAD_ARG;
        goto done;
    }

    switch (selection) {
    case UPDATE_GET_PREINST:
        form_data[GET_FORM_DATA_FILE] = uc->preinst;
        break;
    case UPDATE_GET_INSTALL:
        form_data[GET_FORM_DATA_FILE] = uc->install;
        break;
    case UPDATE_GET_POSTINST:
        form_data[GET_FORM_DATA_FILE] = uc->postinst;
        break;
    default:
        LOG_ERROR("invalid selection");
        rcode = UPDATE_BAD_ARG;
        goto done;
        break;
    }

    if (!form_data[GET_FORM_DATA_FILE]
            || !strlen(form_data[GET_FORM_DATA_FILE])) {
        // Valid selection but nothing to do.
        goto done;
    }

    form_data[GET_FORM_DATA_RELEASE] = uc->release;

    if (asprintf(&url, "%s/1/update/file", uc->server) == -1) {
        rcode = UPDATE_OOM;
        goto done;
    }

    stream = server_post(url, form_data, sizeof(form_data)/sizeof(char *)/2, &rcode);
    free(url);
    if (stream) {
        dlfile = tmpfpath(stream);
        if (!dlfile) {
            // Could be oom as well.
            rcode = UPDATE_FILE_ERR;
        }
    }

done:
    if (stream) {
        // Close the stream if no error since this returns just the filename.
        // On error close the stream and unlink the file.
        if (rcode == UPDATE_OK) {
            fclose(stream);
        } else {
            tmpfunclose(stream);
        }
    }

    return dlfile;
}
