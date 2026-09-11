#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "beepjs.h"

#ifdef _MIPS_ARCH
#define DEFAULT_RT_SIZE                             (8*1024*1024)
#else
#define DEFAULT_RT_SIZE                             (8*1024*1024)
#endif
#define DEFAULT_CX_SIZE                             (8*1024)
#define DEFAULT_START_SCRIPT                        "_start.js"
#define DEFAULT_GC_TIMER                            (120)


bool en_dprintf;
static uv_idle_t tick_spinner;
static uv_signal_t uv_sigint;

static void idle_spinner(uv_idle_t* handle, int status) {
    assert((uv_idle_t*) handle == &tick_spinner);
    assert(status == 0);
    // We can hook events that have to happen after javascript execution but
    // start periodically at uv_default_main() execution.
    uv_idle_stop(handle);
}

static void uv_sigint_handler(uv_signal_t *handler, int signum) {
    printf("SIGINT received\n");
    exit(0);
}

static void beepjs_usage_exit(const char *name, int status) {
    printf("Usage: %s [options] <script> [arguments]\n\n", name);
#ifndef NHELP
    printf("  -h, --help            Show this information\n");
    printf("  --start=file          Startup file (default %s)\n", DEFAULT_START_SCRIPT);
    printf("  --cx-stack=bytes      Stack chunk size (default %d)\n", DEFAULT_CX_SIZE);
    printf("  --rt-maxbytes=bytes   Max bytes before GC runs (default %d)\n", DEFAULT_RT_SIZE);
    printf("  --gc-timer=sec        Min seconds between GC (default %d)\n", DEFAULT_GC_TIMER);
    printf("  --no-comp-cache       Do not cache compiled scripts\n");
    printf("  --http-reuse          Allow connection reuse (not recommended)\n");
    printf("  --verbose             Be verbosy\n");
    printf("  --zeal=z,f            Set zeal option z with frequency f\n");
    printf("  --no-beep-log         Use stdout and stderr for console.*\n");
    printf("\nThe following are zeal option codes\n");
    printf("   0    No additional GC\n");
    printf("   1    Additional GC at common danger points\n");
    printf("   2    GC every f allocs (default: 100)\n");
    printf("   3    Browser only\n");
    printf("   4    Verify pre-write barriers between insn\n");
    printf("   5    Browser only\n");
    printf("   6    Verify stack rooting (ignore XML and Reflect)\n");
    printf("   7    Verify stack rooting (all roots)\n");
    printf("   8    Incremental GC in two slices: 1) mark roots 2) finish collection\n");
    printf("   9    Incremental GC in two slices: 1) mark all 2) new marking and finish\n");
    printf("  10    Incremental GC in multiple slices\n");
    printf("  11    Verify post-write barriers between insn\n");
    printf("  12    Browser only\n");
    printf("  13    Purge analysis state every f allocs (default: 100)\n");
    printf("\nrequire paths:\n");
    printf("  beepjs tries to load require'd modules from cwd. If the named module\n");
    printf("  is not found in cwd beepjs reads the environment variable BEEPJS_PATH\n");
    printf("  and tries to load from the directory specified therein.\n");
#endif
    exit(status);
}

static char **beepjs_copy_argv(int argc, char **argv) {
    int i;
    char **copy_argv;

    if (argc == 0)
        return NULL;

    copy_argv = (char **)malloc(sizeof(char *) * argc);
    assert(copy_argv);
    for (i = 0; i < argc; i++) {
        copy_argv[i] = strdup(argv[i]);
    }

    return copy_argv;
}

enum {
    OPT_START = 256,
    OPT_CX_STACK,
    OPT_RT_MAXBYTES,
    OPT_GC_TIMER,
    OPT_NO_COMPILE_CACHE,
    OPT_HTTP_REUSE,
    OPT_VERBOSE,
    OPT_NO_BEEP_LOG,
    OPT_ZEAL
};

static const struct option beepjs_long_opts[] = {
    // const char *name, int has_arg, int *flag, int val
    {"start", required_argument, NULL, OPT_START},
    {"cx-stack", required_argument, NULL, OPT_CX_STACK},
    {"rt-maxbytes", required_argument, NULL, OPT_RT_MAXBYTES},
    {"gc-timer", required_argument, NULL, OPT_GC_TIMER},
    {"no-comp-cache", no_argument, NULL, OPT_NO_COMPILE_CACHE},
    {"http-reuse", no_argument, NULL, OPT_HTTP_REUSE},
    {"verbose", no_argument, NULL, OPT_VERBOSE},
    {"no-beep-log", no_argument, NULL, OPT_NO_BEEP_LOG},
    {"zeal", required_argument, NULL, OPT_ZEAL},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

static int beepjs_proc_argv(int argc, char **argv, BeepJSOpts *opts) {
    int c;
    int i;
    int opt_index;

    // Split argv[0] [options] script [script arguments] where options all
    // start with '-' into:
    // argv[0] [options] and
    // "beepjs" script [script arguments]
    opts->exec_argc = 1;
    while (opts->exec_argc < argc && *argv[opts->exec_argc] == '-')
        opts->exec_argc++;
    opts->js_argc = argc - opts->exec_argc + 1;
    opts->exec_argv = beepjs_copy_argv(opts->exec_argc, argv);
    opts->js_argv = beepjs_copy_argv(opts->js_argc, argv + opts->exec_argc - 1);
    free(opts->js_argv[0]);
    opts->js_argv[0] = strdup("beepjs");

    while ((c = getopt_long(opts->exec_argc, opts->exec_argv, "h",
            beepjs_long_opts, &opt_index)) != -1) {
        switch (c) {

        case OPT_START: {
            if (!optarg) beepjs_usage_exit(argv[0], 3);
            opts->start = strdup(optarg);
            break;
        }

        case OPT_CX_STACK: {
            long int val = strtol(optarg, NULL, 0);
            if (val <= 0) beepjs_usage_exit(argv[0], 3);
            opts->cx_stacksize = (size_t)val;
            break;
        }

        case OPT_RT_MAXBYTES: {
            long int val = strtol(optarg, NULL, 0);
            if (val <= 0) beepjs_usage_exit(argv[0], 3);
            opts->rt_maxbytes = (uint32_t)val;
            break;
        }

        case OPT_GC_TIMER: {
            long int val = strtol(optarg, NULL, 0);
            if (val < 0) beepjs_usage_exit(argv[0], 3);
            opts->gc_timer = (uint32_t)val;
            break;
        }

        case OPT_NO_COMPILE_CACHE: {
            opts->no_comp_cache = true;
            break;
        }

        case OPT_HTTP_REUSE: {
            opts->http_reuse = true;
            break;
        }

        case OPT_VERBOSE: {
            opts->verbose = true;
            en_dprintf = true;
            break;
        }

        case OPT_ZEAL: {
            if (!optarg) beepjs_usage_exit(argv[0], 3);
            char *fp = NULL;
            long int zeal = strtol(optarg, &fp, 10);
            long int freq;
            if (!fp || *fp != ',') beepjs_usage_exit(argv[0], 3);
            freq = strtol(++fp, &fp, 10);
            if (!fp || *fp != '\0') beepjs_usage_exit(argv[0], 3);
            if (zeal > 13) {
                fprintf(stderr, "Error: Zeal %ld out of range (max 13)\n",
                        zeal);
                exit(3);
            }
            if (opts->zeal_count == MAX_ZEAL_OPTS) {
                fprintf(stderr, "Error: Too many zeal options (max %d)\n",
                        MAX_ZEAL_OPTS);
                exit(3);
            }
            opts->zeal[(2 * opts->zeal_count)] = (int)zeal;
            opts->zeal[(2 * opts->zeal_count) + 1] = (int)freq;
            opts->zeal_count++;
            break;
        }

        // Checked in _start.js.
        case OPT_NO_BEEP_LOG: break;

        case 'h':
            beepjs_usage_exit(argv[0], 0);
            break;

        case '?':
            break;

        default:
            abort();
            break;
        }
    }

    if (opts->verbose) {
        DPRINTF("beepjs main opts: \n");
        DPRINTF("  start: %s\n", (opts->start != NULL) ? opts->start : "<empty>");
        DPRINTF("  cx_stacksize: %zu\n", opts->cx_stacksize);
        DPRINTF("  rt_maxbytes: %u\n", opts->rt_maxbytes);
        DPRINTF("  gc_timer: %u\n", opts->gc_timer);
        DPRINTF("  no_comp_cache: %d\n", opts->no_comp_cache);
        DPRINTF("  http_reuse: %d\n", opts->http_reuse);
        DPRINTF("  exec_argv:");
        for (i = 0; i < opts->exec_argc; i++)
            DPRINTF(" %s", opts->exec_argv[i]);
        DPRINTF("\n  js_argv:");
        for (i = 0; i < opts->js_argc; i++)
            DPRINTF(" %s", opts->js_argv[i]);
        DPRINTF("\n");
    }

    return 1;
}

static void beepjs_free_opts(BeepJSOpts *opts) {
    int i;
    if (opts) {
        if (opts->start)
            free(opts->start);
        if (opts->exec_argv) {
            for (i = 0; i < opts->exec_argc; i++) {
                free(opts->exec_argv[i]);
            }
            free(opts->exec_argv);
        }
        if (opts->js_argv) {
            for (i = 0; i < opts->js_argc; i++) {
                free(opts->js_argv[i]);
            }
            free(opts->js_argv);
        }
        free(opts);
    }
}

static BeepJSOpts *beepjs_init_opts(int argc, char **argv) {
    BeepJSOpts *opts = (BeepJSOpts *)malloc(sizeof(BeepJSOpts));
    assert(opts);
    memset(opts, 0, sizeof(BeepJSOpts));

    opts->rt_maxbytes = DEFAULT_RT_SIZE;
    opts->cx_stacksize = DEFAULT_CX_SIZE;
    opts->gc_timer = DEFAULT_GC_TIMER;

    if (!beepjs_proc_argv(argc, argv, opts)) {
        beepjs_usage_exit(argv[0], 1);
    }

    return opts;
}

int main(int argc, char **argv) {
    BeepJSOpts *opts = beepjs_init_opts(argc, argv);
    BeepJSRuntime *brt;
    BeepJSContext *bcx;
    JSObject *global;
    const char *start_script_name = DEFAULT_START_SCRIPT;

    argv = uv_setup_args(argc, argv);

    signal(SIGPIPE, SIG_IGN);

    uv_disable_stdio_inheritance();
    uv_idle_init(uv_default_loop(), &tick_spinner);
    uv_idle_start(&tick_spinner, idle_spinner);
    uv_signal_init(uv_default_loop(), &uv_sigint);
    uv_signal_start(&uv_sigint, uv_sigint_handler, SIGINT);
    uv_unref((uv_handle_t *)&uv_sigint);

    brt = beepjs_new_rt(opts->rt_maxbytes);
    if (brt == NULL)
        return 1;
    brt->opts = opts;

    bcx = beepjs_new_cx(brt, opts->cx_stacksize);
    if (bcx == NULL)
        return 1;
    global = JS_GetGlobalObject(bcx->cx);

    if (opts->start)
        start_script_name = opts->start;
    if (!beepjs_exec_script(bcx->cx, global, start_script_name, NULL, true)) {
        fprintf(stderr, "Error: in start script: %s\n", start_script_name);
    } else {
        uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    }

    beepjs_destroy_cx(bcx);
    beepjs_destroy_rt(brt);
    JS_ShutDown();

    beepjs_free_opts(opts);

    return 0;
}
