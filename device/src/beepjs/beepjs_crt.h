#ifndef BEEPJS_RUNTIME_H
#define BEEPJS_RUNTIME_H

#include <list>


#define MAX_ZEAL_OPTS 14

typedef struct {
    char *start;
    size_t cx_stacksize;
    uint32_t rt_maxbytes;
    uint32_t gc_timer;
    bool no_comp_cache;
    bool http_reuse;
    bool verbose;
    int exec_argc;
    char **exec_argv;
    int js_argc;
    char **js_argv;
    int zeal_count;
    int zeal[MAX_ZEAL_OPTS * 2];
} BeepJSOpts;

typedef struct {
    char *name;
    const char *script;
} BeepJSBuiltinScript;

typedef struct {
    JSRuntime *rt;
    BeepJSOpts *opts;
    double start_time;
    std::list<BeepJSBuiltinScript> *builtin_scripts;
} BeepJSRuntime;

typedef struct {
    char *name;
    JSScript *script;
} BeepJSCachedScript;

typedef struct {
    JSContext *cx;
    std::list<BeepJSCachedScript *> *cached_scripts;
} BeepJSContext;

void beepjs_destroy_rt(BeepJSRuntime *brt);
BeepJSRuntime *beepjs_new_rt(uint32_t maxbytes);
BeepJSRuntime *beepjs_get_brt(JSContext *cx);

void beepjs_destroy_cx(BeepJSContext *bcx);
BeepJSContext *beepjs_new_cx(BeepJSRuntime *brt, size_t stackchunksize);
BeepJSContext *beepjs_get_bcx(JSContext *cx);

typedef int (*ModuleInitFunc)(JSContext *cx);
typedef void (*ModuleFiniFunc)(JSContext *cx);
void beepjs_add_module_init_func(ModuleInitFunc func, int pri);
void beepjs_add_module_fini_func(ModuleFiniFunc func, int pri);

#define BEEPJS_MODULE_INIT(__FUNC__, __PRI__) \
    __attribute__((constructor)) \
    static void __add_init_func__ ## __FUNC__(void) { \
        beepjs_add_module_init_func(__FUNC__, __PRI__); \
    }

#define BEEPJS_MODULE_FINI(__FUNC__, __PRI__) \
    __attribute((constructor)) \
    static void __add_fini_func__ ## __FUNC__(void) { \
        beepjs_add_module_fini_func(__FUNC__, __PRI__); \
    }

JSObject *beepjs_get_native_space(JSContext *cx, const char *name);
JSObject *beepjs_create_native_space(JSContext *cx, const char *name);
void beepjs_delete_native_space(JSContext *cx, const char *name);

int beepjs_add_builtin_script(JSContext *cx, const char *name,
        const char *script);
int beepjs_exec_script(JSContext *cx, JSObject *obj, const char *name,
        jsval *result, bool isolate);

#endif
