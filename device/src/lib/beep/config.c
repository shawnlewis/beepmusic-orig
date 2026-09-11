#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <uci.h>

#include "beep/beeplib.h"
#include "beep/debug.h"
#include "beep/flags.h"

#include "config.h"

///// Flags

struct flag_vals {
    char* uciconfig;
};

struct flag_vals config_flags = {
    .uciconfig = NULL
};

static const BeepFlag flags[] = {
    BEEP_FLAG("uciconfig", BEEP_FLAG_STRING, &config_flags.uciconfig, NULL, NULL)
};

BEEP_INIT_FLAGS(flags);

///// End Flags

bool beep_config_main_write(const char *config,
        const char *attr, const char *value) {
    struct uci_ptr ptr;
    struct uci_context *ctx;
    char *path;
    int ret;


    if(!uci_validate_text(value)) {
        LOG_ERROR(log_beep_main, "Invalid UCI value: %s", value);
        goto out_early_error;
    }

    if((asprintf(&path, "%s.main.%s=%s", config, attr, value)) == -1) {
        LOG_ERROR(log_beep_main, "Couldn't allocate path string");
        goto out_early_error;
    }

    ctx = uci_alloc_context();

    if(config_flags.uciconfig) {
        uci_set_confdir(ctx, config_flags.uciconfig);
    }

    ret = uci_lookup_ptr(ctx, &ptr, path, true);
    if(ret != UCI_OK) {
        LOG_ERROR(log_beep_main, "Failed to lookup ptr: %d", ret);
        goto out_error;
    }

    ret = uci_set(ctx, &ptr);
    if(ret != UCI_OK) {
        LOG_ERROR(log_beep_main, "Failed to set %s", path);
        goto out_error;
    }

    ret = uci_save(ctx, ptr.p);
    if(ret != UCI_OK) {
        LOG_ERROR(log_beep_main, "Failed to save package");
        goto out_error;
    }

    ret = uci_commit(ctx, &ptr.p, true);
    if(ret != UCI_OK) {
        LOG_ERROR(log_beep_main, "Failed to commit package");
        goto out_error;
    }

    free(path);
    uci_free_context(ctx);
    return true;

out_error:
    free(path);
    uci_free_context(ctx);

out_early_error:
    return false;
}

static ssize_t beep_config_read_safe(char *path, char *buf, size_t buf_sz) {
    struct uci_ptr p;

    struct uci_context *ctx = uci_alloc_context();

    if (config_flags.uciconfig) {
        uci_set_confdir(ctx, config_flags.uciconfig);
    }

    if (uci_lookup_ptr(ctx, &p, path, true) != UCI_OK) {
        //uci_perror(ctx, "UCI Error");
        char *error_str = NULL;
        uci_get_errorstr(ctx, &error_str, NULL);
        LOG_WARN(log_beep_main, "UCI Error: %s", error_str);
        free(error_str);
        goto out_error;
    }

    if (!p.o) {
        goto out_error;
    }

    strncpy(buf, p.o->v.string, buf_sz);
    buf[buf_sz - 1] = '\0';
    buf_sz = strlen(buf);

    uci_free_context(ctx);
    return buf_sz;

out_error:
    uci_free_context(ctx);
    return -1;
}

ssize_t beep_config_main_read_safe(
        const char *config, const char *attr, char *buf, size_t buf_sz) {
    char* path;
    if(asprintf(&path, "%s.main.%s", config, attr) == -1) {
        return -1;
    }

    ssize_t ret = beep_config_read_safe(path, buf, buf_sz);
    free(path);
    return ret;
}

int beep_config_main_get_int(
        const char *config, const char *attr, int default_value) {
    char config_buf[32];
    if (beep_config_main_read_safe(config, attr, config_buf, 32) < 0) {
        LOG_INFO(log_beep_main, "'%s' not found.  Using default %d",
                attr, default_value);
        return default_value;
    }

    int r = atoi(config_buf);
    return r;
}

size_t beep_config_data_read(const char *attr, char *buf, size_t buf_sz) {
    return beep_config_main_read_safe("beep_data", attr, buf, buf_sz);
}

size_t beep_config_data_get_int(const char *attr, int default_value) {
    return beep_config_main_get_int("beep_data", attr, default_value);
}

bool beep_config_data_write(const char *attr, const char* value) {
    return beep_config_main_write("beep_data", attr, value);
}

size_t beep_config_device_read(const char *attr, char *buf, size_t buf_sz) {
    return beep_config_main_read_safe("beep_device", attr, buf, buf_sz);
}

size_t beep_config_static_read(const char *attr, char *buf, size_t buf_sz) {
    return beep_config_main_read_safe("beep_static", attr, buf, buf_sz);
}

size_t beep_config_devel_read(const char *attr, char *buf, size_t buf_sz) {
    return beep_config_main_read_safe("beep_devel", attr, buf, buf_sz);
}

size_t beep_config_devel_get_int(const char *attr, int default_value) {
    return beep_config_main_get_int("beep_devel", attr, default_value);
}

struct _app_config {
    char *app_id;
    char *display_name;
    char *path;
    char *lib_path;
    char *dial_extra;
};

static ssize_t beep_config_app_read(const char *app_id, const char *attr,
        char *buf, size_t buf_sz) {
    char *path;
    if(asprintf(&path, "beep_apps.%s.%s", app_id, attr) == -1) {
        return -1;
    }

    ssize_t ret = beep_config_read_safe(path, buf, buf_sz);
    free(path);
    return ret;
}

AppConfig beep_config_app_load(const char *app_id) {
    AppConfig app = calloc(1, sizeof(struct _app_config));
    char *buf = calloc(1, 256);
    size_t buf_sz;
    if(!buf) {
        goto out_error;
    }

    buf_sz = 256;
    if(beep_config_app_read(app_id, "display_name", buf, buf_sz) > 0) {
        app->display_name = strdup(buf);
    } else {
        goto out_error; // display_name is required
    }

    // TODO: When url can be supplied, only path or url can be specified (XOR)
    buf_sz = 256;
    if(beep_config_app_read(app_id, "path", buf, buf_sz) > 0) {
        app->path = strdup(buf);
    } else {
        goto out_error; // path is required
    }

    buf_sz = 256;
    if(beep_config_app_read(app_id, "lib_path", buf, buf_sz) > 0) {
        app->lib_path = strdup(buf);
    }

    buf_sz = 256;
    if(beep_config_app_read(app_id, "dial_extra", buf, buf_sz) > 0) {
        app->dial_extra = strdup(buf);
    }

    free(buf);
    app->app_id = strdup(app_id);

    return app;

out_error:
    free(buf);
    beep_config_app_free(app);
    return NULL;
}

void beep_config_app_free(AppConfig app) {
    if(!app) {
        return;
    }

    free(app->app_id);
    free(app->display_name);
    free(app->path);
    free(app->lib_path);

    free(app);
}

const char *beep_config_app_get_id(AppConfig app) {
    return app->app_id;
}

const char *beep_config_app_get_display_name(AppConfig app) {
    return app->display_name;
}

const char *beep_config_app_get_path(AppConfig app) {
    return app->path;
}

const char *beep_config_app_get_lib_path(AppConfig app) {
    return app->lib_path;
}

const char *beep_config_app_get_dial_extra(AppConfig app) {
    return app->dial_extra;
}
