#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>
#include <uci.h>

#include "beep/log.h"

// TODO: uci/version code derived from beepupdate.  Move it somewhere
// so it can be shared.
#define MAX_VERSION_STR_SIZE                        (1024)
#define MAX_UCI_PATH_SIZE                           (64)

#define SERVER_UPLOAD_PATH                          "/1/core/upload"
#define SERVER_UPLOAD_CHUNK_SIZE                    (1024*1024)
//#define SERVER_UPLOAD_CHUNK_SIZE                    (96*1024)
#define UPLOAD_END_MARKER                           0xdeadbeef

#define DEFAULT_CRASH_TIMEOUT                       (180)
#define TIMEOUT_AT_INIT_VAL                         ((time_t)(-1))

#ifdef BEEP_DEVICE
#define DEFAULT_CONFDIR                             "/etc/config"
#define DEFAULT_SYS_VERSION_PATH                    "/beep/SYSVER"
#define DEFAULT_SW_VERSION_PATH                     "/beep/platform/VERSION"
#else
#define DEFAULT_CONFDIR                             "."
#define DEFAULT_SYS_VERSION_PATH                    "./SYSVER"
#define DEFAULT_SW_VERSION_PATH                     "./VERSION"
#endif

static int stderr_valid;

static const char hex_char[] = "0123456789abcdef";

struct device_config {
    char *disable_cores;
    char *device_id;
    char *device_auth;
    char *crash_serv;
    char *crash_port;
    char *crash_timeout;
    char *sys_ver;
    char *sw_ver;
    time_t timeout_val;
};

struct uciop {
    size_t offset;
    const char *path;
};

static struct uciop device_uciops[] = {
    {offsetof(struct device_config, disable_cores),
        "beep_devel.main.disable_cores"},
    {offsetof(struct device_config, device_id), "beep_device.main.device_id"},
    {offsetof(struct device_config, device_auth),
        "beep_device.main.device_auth"},
    {offsetof(struct device_config, crash_serv), "beep_static.main.crash_serv"},
    {offsetof(struct device_config, crash_port), "beep_static.main.crash_port"},
    {offsetof(struct device_config, crash_timeout),
        "beep_static.main.crash_timeout"}
};
#define DEVICE_UCIOPS_SIZE (sizeof(device_uciops)/sizeof(struct uciop))


static int load_uciops(const char *confdir, void *dest,
        struct uciop *uciops, size_t ops_count) {
    struct uci_context *ctx;
    struct uci_ptr ptr;
    char **strdest;
    char path[MAX_UCI_PATH_SIZE];
    int i;
    int ret;

    if (!confdir)
        confdir = DEFAULT_CONFDIR;

    ctx = uci_alloc_context();
    if (!ctx)
        return 1;

    if (uci_set_confdir(ctx, confdir) != UCI_OK) {
        uci_free_context(ctx);
        return 1;
    }

    for (i = 0; i < ops_count; i++) {
        strncpy(path, uciops[i].path, MAX_UCI_PATH_SIZE);
        ret = uci_lookup_ptr(ctx, &ptr, path, true);
        // Do nothing for missing uci entries.  Let caller determine if it's
        // fatal or not.
        if (ret == UCI_OK && ptr.o && ptr.o->v.string) {
            // Get address to the char * in dest to set for this path.
            strdest = (char **)(((uint8_t *)dest) + uciops[i].offset);
            *strdest = strdup(ptr.o->v.string);
        }
    }

    uci_free_context(ctx);

    return 0;
}

static char *version_from_file(const char *path) {
    FILE *stream = NULL;
    char *verstr = (char *)malloc(MAX_VERSION_STR_SIZE);
    size_t verstr_size;

    if (!verstr)
        return NULL;

    memset(verstr, '\0', MAX_VERSION_STR_SIZE);

    stream = fopen(path, "r");
    if (!stream) {
        free(verstr);
        return NULL;
    }

    fread(verstr, 1, MAX_VERSION_STR_SIZE - 1, stream);
    fclose(stream);

    verstr_size = strlen(verstr);

    if (!verstr_size) {
        strcpy(verstr, "unknown");
    } else if (verstr[verstr_size - 1] == '\n') {
        verstr[verstr_size - 1] = '\0';
    }

    return verstr;
}

static void free_dev_cfg(struct device_config *dev_cfg) {
    if (dev_cfg) {
        if (dev_cfg->disable_cores)
            free(dev_cfg->disable_cores);
        if (dev_cfg->device_id)
            free(dev_cfg->device_id);
        if (dev_cfg->device_auth)
            free(dev_cfg->device_auth);
        if (dev_cfg->crash_serv)
            free(dev_cfg->crash_serv);
        if (dev_cfg->crash_port)
            free(dev_cfg->crash_port);
        if (dev_cfg->crash_timeout)
            free(dev_cfg->crash_timeout);
        if (dev_cfg->sys_ver)
            free(dev_cfg->sys_ver);
        if (dev_cfg->sw_ver)
            free(dev_cfg->sw_ver);
        free(dev_cfg);
    }
}

static struct device_config *get_dev_cfg(void) {
    struct device_config *dev_cfg = (struct device_config *)
            malloc(sizeof(struct device_config));

    if (!dev_cfg)
        return NULL;

    memset(dev_cfg, 0, sizeof(struct device_config));
    if (load_uciops(NULL, dev_cfg, device_uciops,
            DEVICE_UCIOPS_SIZE)) {
        free(dev_cfg);
        return NULL;
    }

    dev_cfg->sys_ver = version_from_file(DEFAULT_SYS_VERSION_PATH);
    dev_cfg->sw_ver = version_from_file(DEFAULT_SW_VERSION_PATH);

    if (dev_cfg->crash_timeout) {
        char *ccheck = NULL;
        long int val = strtol(dev_cfg->crash_timeout, &ccheck, 10);
        if (val < 0 || *ccheck != '\0') {
            dev_cfg->timeout_val = DEFAULT_CRASH_TIMEOUT;
        } else {
            dev_cfg->timeout_val = (time_t)val;
        }
    } else {
        dev_cfg->timeout_val = DEFAULT_CRASH_TIMEOUT;
    }

    return dev_cfg;
}

struct post_args {
    char *url;
    FILE *write_stream;
    char *read_cb_name;
    curl_read_callback read_cb;
    void *read_cb_priv;
    char **form_data;
    size_t form_count;
    long read_cb_size;
    bool verbose;
};

// Negative are internal errors, positive is returned http code.
long server_post(struct post_args *args) {
    CURL *easy_handle = NULL;
    struct curl_httppost *formpost = NULL;
    struct curl_httppost *formpost_end = NULL;
    long http_code = -1;
    int i;
    CURLcode cc = CURLE_OK;
    CURLFORMcode cfc = CURL_FORMADD_OK;

    if (!args || !args->url || !args->form_data || !args->form_count) {
        return -1;
    }

    easy_handle = curl_easy_init();
    if (!easy_handle) {
        return -1;
    }

    // Add general form data.
    for (i = 0; i < args->form_count && cfc == CURL_FORMADD_OK; i++) {
        if (strcmp(args->form_data[(i * 2)], "device_auth")) {
            LOG_DEBUG(log_beep_main, "form_data: %s=%s",
                    args->form_data[(i * 2)],
                    args->form_data[(i * 2) + 1]);
        }
        cfc = curl_formadd(&formpost,
                &formpost_end,
                CURLFORM_COPYNAME, args->form_data[(i * 2)],
                CURLFORM_COPYCONTENTS, args->form_data[(i * 2) + 1],
                CURLFORM_END);
    }

    // Setup the read (data to server) callback if specified.
    // Don't check read_cb_priv since it may be NULL.
    if (cfc == CURL_FORMADD_OK && args->read_cb_name && args->read_cb
            && args->read_cb_size) {
        cfc = curl_formadd(&formpost,
                &formpost_end,
                CURLFORM_COPYNAME, args->read_cb_name,
                CURLFORM_STREAM, args->read_cb_priv,
                CURLFORM_CONTENTSLENGTH, args->read_cb_size,
                CURLFORM_FILENAME, "-",
                CURLFORM_END);
        cc = curl_easy_setopt(easy_handle, CURLOPT_READFUNCTION,
                args->read_cb);
    }

    if (cfc != CURL_FORMADD_OK) {
        LOG_ERROR(log_beep_main, "cfc: %d", cfc);
        goto done;
    }

    // Optional write (data from server) response to file.
    if (args->write_stream && cc == CURLE_OK) {
        cc = curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA,
                args->write_stream);
    }

    if (args->verbose && stderr_valid && cc == CURLE_OK) {
        cc = curl_easy_setopt(easy_handle, CURLOPT_VERBOSE, 1L);
    }

    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
            CURLOPT_URL, args->url);
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
            CURLOPT_HTTPPOST, formpost);
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(easy_handle,
            CURLOPT_USE_SSL, CURLUSESSL_ALL);

    if (cc == CURLE_OK) {
        cc = curl_easy_perform(easy_handle);
    }

    if (cc == CURLE_OK) {
        cc = curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE,
                &http_code);
    }

    // Ignore error if the callback bails.
    if (cc != CURLE_OK && cc != CURLE_ABORTED_BY_CALLBACK) {
        LOG_ERROR(log_beep_main, "curl error: %s (%d)",
                curl_easy_strerror(cc), cc);
    }

done:
    if (easy_handle)
        curl_easy_cleanup(easy_handle);

    if (formpost)
        curl_formfree(formpost);

    return http_code;
}

struct file_read_cb_priv {
    FILE *stream;
    size_t rem_bytes;  // Reset SERVER_UPLOAD_CHUNK_SIZE each *chunk*.
    size_t real_bytes;  // Reset 0 each file.
    time_t timeout_val;
    time_t timeout_at;  // Reset TIMEOUT_AT_INIT_VAL each file.
    uint32_t marker;  // Reset 0 each file.
    int feof;  // Reset 0 each file.
    int ferror;  // Reset 0 each file.
    int _feof;  // Reset 0 each file.
    int _ferror;  // Reset 0 each file.
    bool timeout;  // Reset 0 each file.
    bool pad;
};

static size_t file_read_cb(char *ptr, size_t size, size_t nmemb,
        void *userdata) {
#ifdef BEEP_VIRTUAL
    LOG_DEBUG(log_beep_main, "%p %zu %zu %p", ptr, size, nmemb, userdata);
#endif
    struct file_read_cb_priv *priv = (struct file_read_cb_priv *)userdata;
    size_t curl_bytes = size * nmemb;
    size_t need_bytes = priv->rem_bytes < curl_bytes ?
            priv->rem_bytes : curl_bytes;
    size_t wbytes = 0;

    if (priv->timeout_at == TIMEOUT_AT_INIT_VAL) {
        if (priv->timeout_val == 0) {
            // Set no timeout.
            priv->timeout_at = 0;
        } else {
            struct timespec now;
            // Setup correct timeout_at value, don't care about timezone.
            clock_gettime(CLOCK_REALTIME, &now);
            priv->timeout_at = now.tv_sec + priv->timeout_val;
        }
    } else if (priv->timeout_at != 0) {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        if (now.tv_sec > priv->timeout_at) {
            priv->timeout = true;
            return CURL_READFUNC_ABORT;
        }
    }

    // Still need to send bytes and have not read everything from stream.
    // Note: There is a worst case scenario where this is the last read
    // of this chunk and it exhausts the stream at the same time.  We will
    // not know eof is hit until the beginning of the next chunk and will
    // send an entire empty chunk.
    if (!priv->_feof && !priv->_ferror && need_bytes) {
        wbytes = fread(ptr, 1, need_bytes, priv->stream);
        priv->real_bytes += wbytes;
        need_bytes -= wbytes;
        // If we still need bytes find out why.
        if (need_bytes) {
            priv->_feof = feof(priv->stream);
            priv->_ferror = ferror(priv->stream);

            // Only set the marker here when the file has been
            // exhausted.  The marker can't have any 0x00 bytes
            // in it.
            priv->marker = UPLOAD_END_MARKER;
        }
    }

    // Go ahead and pad bytes even if ferror occurred so we can finish
    // the chunk and appease the server.
    if (priv->pad && need_bytes) {
        // Check if there is part of the end of data maker to write.
        // Allows marker to span multiple read callbacks.
        while (priv->marker && need_bytes) {
            // Write data in big endian.
            *((uint8_t *)(ptr + wbytes)) = (uint8_t)(priv->marker >> 24);
            wbytes++;
            need_bytes--;
            priv->marker <<= 8;

            // Defer setting the feof and ferror that the upper layers
            // check so the marker can span multiple chunks.
            if (!priv->marker) {
                priv->feof = priv->_feof;
                priv->ferror = priv->_ferror;
            }
        }

        memset(ptr + wbytes, 0, need_bytes);
        wbytes += need_bytes;
        need_bytes = 0;
    }

    // Be careful rem_bytes is unsigned.
    priv->rem_bytes -= wbytes;

    return wbytes;
}

static int bcore_send(struct device_config *dev_cfg, size_t *bytes_sent,
        int argc, char **argv) {
    struct post_args args = {};
    struct file_read_cb_priv cb_priv ={};
    char *form_data[] = {
        "device_id", dev_cfg->device_id,
        "device_auth", dev_cfg->device_auth,
        "name", NULL
    };
    char *tail;
    long http_code = -1;
    uint8_t chunk = 0;

    // Want to know if no or incomplete upload.
    *bytes_sent = 0;

    //core_pattern args: %t %p %s %h %e
    //args: time pid signal hostname executable
    if (argc < 5) {
        LOG_ERROR(log_beep_main, "too few args for sendcore");
        return 1;
    }

    // Send notice that a core dump happened before doing anything
    // else in case the system is about to go down.
    // Note: This may not catch everything since we've already accessed
    // the filesystem and started some libraries, but it should be good
    // enough.
    LOG_INFO(log_beep_main, "program crash: %s %s %s %s %s",
            argv[0], argv[1], argv[2], argv[3], argv[4]);

    // Make sure there is enough data from uci to be able to upload to the
    // server.
    if (!dev_cfg->device_id || !dev_cfg->device_auth || !dev_cfg->crash_serv
            || !dev_cfg->crash_port) {
        LOG_ERROR(log_beep_main, "uci missing required data");
        return 1;
    }

    // name = time_device-id_pid_signal_executable.core.lzo.XX
    if (asprintf(&form_data[5], "%s_%s_%s_%s_%s.core.lzo.00",
            argv[0],
            dev_cfg->device_id,
            argv[1],
            argv[2],
            argv[4]) == -1) {
        return 1;
    }

    if (asprintf(&args.url, "https://%s:%s%s", dev_cfg->crash_serv,
            dev_cfg->crash_port, SERVER_UPLOAD_PATH) == -1) {
        goto done;
    }

    args.read_cb_name = strdup("file");
    if (!args.read_cb_name) {
        goto done;
    }

    args.read_cb = file_read_cb;
    args.read_cb_priv = &cb_priv;
    args.form_data = form_data;
    args.form_count = sizeof(form_data)/sizeof(char *)/2;  // Count is form k,v.
    args.read_cb_size = SERVER_UPLOAD_CHUNK_SIZE;
    //args.verbose = true;

    cb_priv.stream = stdin;
    cb_priv.rem_bytes = SERVER_UPLOAD_CHUNK_SIZE;
    cb_priv.timeout_val = dev_cfg->timeout_val;
    cb_priv.timeout_at = TIMEOUT_AT_INIT_VAL;
    cb_priv.pad = true;

    // Points to the first chunk number char.
    tail = form_data[5] + strlen(form_data[5]) - 2;

    http_code = 200;

    while (!cb_priv.feof && !cb_priv.timeout && !cb_priv.ferror
            && http_code == 200) {
        cb_priv.rem_bytes = SERVER_UPLOAD_CHUNK_SIZE;
        http_code = server_post(&args);

        chunk++;
        *tail = hex_char[chunk >> 4];
        *(tail + 1) = hex_char[chunk & 0xf];
    }

    if (cb_priv.timeout || cb_priv.ferror || http_code != 200) {
        LOG_ERROR(log_beep_main,
                "core upload: failure - http_code: %ld timeout: %d ferror: %d"
                " bytes: %zu",
                http_code, cb_priv.timeout, cb_priv.ferror,
                cb_priv.real_bytes);
        http_code = -1;  // For return value.
    } else {
        *(tail - 1) = '\0';  // Get rid of chunk numbers.
        // Search for this in Logstash to get all sucessfully uploaded cores.
        LOG_INFO(log_beep_main,
                "core upload: success - path: %s chunks: %u bytes: %zu",
                form_data[5],
                chunk,
                cb_priv.real_bytes);
    }

    // Due to Logstash-1317 we need to send another message with this
    // stream_identity to flush out the previous message which needs to
    // be reported to Beep clinic in a timely fashion.
    LOG_INFO(log_beep_main, "Logstash flush");

    *bytes_sent = cb_priv.real_bytes;

done:
    if (form_data[5])
        free(form_data[5]);

    if (args.url)
        free(args.url);

    if (args.read_cb_name)
        free(args.read_cb_name);

    return http_code == 200 ? 0 : 1;
}

int main(int argc, char **argv) {
    struct device_config *dev_cfg;
    int ret = 1;

    log_beep_main = LOG_CATEGORY_GET("bcore");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

#ifdef BEEP_DEVICE
    LOG_SET_SYSLOG_IDENT("bcore");
    LOG_SET_SYSLOG_PRIORITY(LOG_PRIORITY_INFO);
#endif

    // When this is invoked from the kernel/core_pattern stdout and stderr
    // are not valid.  Trying to use them causes very strange behavior.
    if (fcntl(STDERR_FILENO, F_GETFD) == -1) {
        stderr_valid = 0;
        LOG_SET_STDERR_PRIORITY(LOG_PRIORITY_OFF);
    } else {
        stderr_valid = 1;
    }

    signal(SIGPIPE, SIG_IGN);

    if (curl_global_init(CURL_GLOBAL_DEFAULT))
        return 1;

    dev_cfg = get_dev_cfg();
    if (!dev_cfg)
        return ret;

    if (argc <= 1) {
        LOG_ERROR(log_beep_main, "no cmd");
    } else if (!strcmp(argv[1], "sendcore")) {
        size_t bytes_sent;
        ret = bcore_send(dev_cfg, &bytes_sent, argc - 2, argv + 2);
    } else {
        LOG_ERROR(log_beep_main, "unknown cmd: %s", argv[1]);
    }

    curl_global_cleanup();

    free_dev_cfg(dev_cfg);

    LOG_CLEANUP();

    return ret;
}
