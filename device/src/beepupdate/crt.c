#include <assert.h>
#include <getopt.h>

#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>

#include "machdefs.h"
#include "beepupdate.h"

#define UPDATE_CMD_STATUS_STR                       "status"
#define UPDATE_CMD_UPDATE_STR                       "update"
#define UPDATE_CMD_BOOT_ACTION_STR                  "boot-action"
#define UPDATE_CMD_BEEP_STOP_STR                    "beep-stop"
#define UPDATE_CMD_BEEP_START_STR                   "beep-start"
#define UPDATE_CMD_BOOTCOUNT_STR                    "bc"
#define UPDATE_CMD_RESET_BOOTCOUNT_STR              "reset-bc"

#define UPDATE_CONFIG_DEFAULT_SERV                  "reverb.beepdevices.com"
#define UPDATE_CONFIG_DEFAULT_PORT                  "40937"

enum {
    OPT_RECOVERY = 256,
    OPT_REQUESTED,
    OPT_INSTALL_PREFIX,
    OPT_SYSLOG,
    OPT_CHECK_UNLESS_FORCE
};

static const struct option beepupdate_long_opts[] = {
    // const char *name, int has_arg, int *flag, int val
    {"recovery", no_argument, NULL, OPT_RECOVERY},
    {"requested", required_argument, NULL, OPT_REQUESTED},
    {"prefix", required_argument, NULL, OPT_INSTALL_PREFIX},
    {"syslog", no_argument, NULL, OPT_SYSLOG},
    {"check-unless-force", no_argument, NULL, OPT_CHECK_UNLESS_FORCE},
    {NULL, 0, NULL, 0}
};

UpdateSystemConfig *sysconfig;


void system_config_cleanup(void) {
    if (sysconfig) {
        if (sysconfig->update_serv)
            bfree(sysconfig->update_serv);
        if (sysconfig->update_port)
            bfree(sysconfig->update_port);
        if (sysconfig->device_id)
            bfree(sysconfig->device_id);
        if (sysconfig->device_auth)
            bfree(sysconfig->device_auth);
        if (sysconfig->sys_ver)
            bfree(sysconfig->sys_ver);
        if (sysconfig->beep_ver)
            bfree(sysconfig->beep_ver);
        if (sysconfig->requested)
            bfree(sysconfig->requested);
        if (sysconfig->prefix)
            bfree(sysconfig->prefix);
        bfree(sysconfig);
        sysconfig = NULL;
    }
}

void config_init(int argc, char **argv) {
    int c;
    int opt_index;
    UpdateCode code;

    sysconfig = (UpdateSystemConfig *)
            bmalloc(sizeof(UpdateSystemConfig));
    assert(sysconfig);

    update_system_state();

    while ((c = getopt_long(argc, argv, "", beepupdate_long_opts,
            &opt_index)) != -1) {
        switch (c) {

        case OPT_RECOVERY: {
            sysconfig->state &= ~STATE_PRIMARY_PART;
            sysconfig->state |= STATE_RECOVERY_PART;
            break;
        }

        case OPT_REQUESTED: {
            if (optarg)
                sysconfig->requested = bstrdup(optarg);
            break;
        }

        case OPT_INSTALL_PREFIX: {
            if (optarg)
                sysconfig->prefix = bstrdup(optarg);
            break;
        }

        case OPT_SYSLOG: {
            sysconfig->state |= STATE_SYSLOG_READY;
            break;
        }

        case OPT_CHECK_UNLESS_FORCE: {
            sysconfig->check_unless_force = true;
            break;
        }

        default:
            exit(2);
            break;
        }
    }

    if (update_log_init() != UPDATE_OK) {
        abort();
    }

    code = load_uci_config(sysconfig);
    if (code != UPDATE_OK) {
        LOG_WARN("load config code: %d", code);
        // Missing some config information see if we can recover.
        if (!sysconfig->device_id || !sysconfig->device_auth) {
            LOG_ERROR("Missing device uci config");
            exit(2);
        }
        if (!sysconfig->update_serv)
            sysconfig->update_serv = bstrdup(UPDATE_CONFIG_DEFAULT_SERV);
        if (!sysconfig->update_port)
            sysconfig->update_port = bstrdup(UPDATE_CONFIG_DEFAULT_PORT);
    }

    code = load_sw_versions(sysconfig);
    if (code != UPDATE_OK)
        LOG_WARN("load ver code: %d", code);

    if (!sysconfig->requested)
        sysconfig->requested = bstrdup("newest");
    if (!sysconfig->prefix)
        sysconfig->prefix = bstrdup(UPDATE_DEFAULT_INST_PREFIX);

    if (optind >= argc) {
        sysconfig->cmd = UPDATE_CMD_UPDATE;
    } else {
        if (!strcmp(UPDATE_CMD_STATUS_STR, argv[optind])) {
            sysconfig->cmd = UPDATE_CMD_STATUS;
        } else if (!strcmp(UPDATE_CMD_UPDATE_STR, argv[optind])) {
            sysconfig->cmd = UPDATE_CMD_UPDATE;
        } else if (!strcmp(UPDATE_CMD_BOOT_ACTION_STR, argv[optind])) {
            sysconfig->cmd = UPDATE_CMD_BOOT_ACTION;
        } else if (!strcmp(UPDATE_CMD_BEEP_STOP_STR, argv[optind])) {
            sysconfig->cmd = UPDATE_CMD_BEEP_STOP;
        } else if (!strcmp(UPDATE_CMD_BEEP_START_STR, argv[optind])) {
            sysconfig->cmd = UPDATE_CMD_BEEP_START;
        } else if (!strcmp(UPDATE_CMD_BOOTCOUNT_STR, argv[optind])) {
            sysconfig->cmd = UPDATE_CMD_BOOTCOUNT;
        } else if (!strcmp(UPDATE_CMD_RESET_BOOTCOUNT_STR, argv[optind])) {
            sysconfig->cmd = UPDATE_CMD_RESET_BOOTCOUNT;
        } else {
            LOG_ERROR("unknown cmd '%s'", argv[optind]);
            exit(2);
        }
    }
}

void libraries_cleanup(void) {
    curl_global_cleanup();
    EVP_cleanup();
}

void libraries_init(void) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        LOG_ERROR("curl_global_init failed");
        abort();
    }

    if (SSL_library_init() != 1) {
        LOG_ERROR("SSL_library_init failed");
        abort();
    }

    OpenSSL_add_all_digests();
    OpenSSL_add_all_ciphers();
}
