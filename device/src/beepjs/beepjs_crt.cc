#include <assert.h>
#include <pthread.h>

#include "beepjs.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#include <gc/Barrier.h>
#pragma GCC diagnostic pop

namespace {

typedef struct {
    int pri;
    union {
        ModuleInitFunc init;
        ModuleFiniFunc fini;
    } func;
} ModuleCxFunc;

// To ensure c++ object constructor and .init_array safety these will be
// initialized when needed.
std::list<ModuleCxFunc> *InitFuncs;
std::list<ModuleCxFunc> *FiniFuncs;

namespace JSBase {

// Top level for the context.
static JSClass GlobalClass = {
    "global",
    JSCLASS_GLOBAL_FLAGS,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    NULL
};

static JSFunctionSpec GlobalFuncs[] = {
    JS_FS_END
};

// There is where modules, wrappers and internal bits will get stashed.  It is
// unique to us so things with the same functionality and name are not
// mistaken for exact node clones.  These can then be (re)wrapped as needed.
static JSClass BeepJSClass = {
    "beepjs",
    0,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    NULL
};

static JSFunctionSpec BeepJSFuncs[] = {
    JS_FS_END
};
}  // namespace JSBase


// This is the private structure for Error objects.  It's not publicly exposed
// so just recreate it here.
struct JSStackTraceElem {
    js::HeapPtrString funName;
    const char *filename;
    unsigned ulineno;
};

struct JSExnPrivate {
    JSErrorReport *errorReport;
    js::HeapPtrString message;
    js::HeapPtrString filename;
    unsigned lineno;
    unsigned column;
    size_t stackDepth;
    int exnType;
    JSStackTraceElem stackElems[1];
};

// Spidermonkey allows hooks for throw and uncaught errors (JS_SetThrowHook,
// JS_SetDebugErrorHook).  When an error is thrown or JS_Report* is called the
// throw hook will get called every time the error propagates up through each
// stack frame until it is caught or move beyond the global frame.  This will
// call the error hook.  If that doesn't deal with the exception the
// ErrorReporter is called.  By the time the error hook or reporter is called
// the stack is completely unwound so any examination of the stack needs to be
// done during the first throw hook.  Fortunately the error object still
// exists and has all the data we need for a simple backtrace except native
// calls (see below).
static void report_error(JSContext *cx, const char *message, JSErrorReport *report) {
    jsval exn_val;
    JSObject *exn_obj;
    JSClass *exn_cls;
    JSExnPrivate *exn_priv;
    size_t depth;

    fprintf(stderr, "%s\n", message);

    if (!JS_IsExceptionPending(cx))
        goto no_trace;

    JS_GetPendingException(cx, &exn_val);
    exn_obj = JSVAL_TO_OBJECT(exn_val);
    exn_cls = JS_GetClass(exn_obj);
    // If the error did not originate in the top compartment this will be a
    // proxy to the error.
    if (exn_cls && !strcmp(exn_cls->name, "Proxy")
            && js::IsWrapper(exn_obj)) {
        exn_obj = JS_UnwrapObject(exn_obj);
        exn_cls = JS_GetClass(exn_obj);
    }

    if (!exn_cls || strcmp(exn_cls->name, "Error"))
        goto no_trace;

    if (!(exn_cls->flags & JSCLASS_HAS_PRIVATE))
        goto no_trace;

    exn_priv = static_cast<JSExnPrivate *>(JS_GetPrivate(exn_obj));
    if (!exn_priv)
        goto no_trace;

    if (exn_priv->stackDepth == 0)
        goto no_trace;

    // This has our basic backtrace information.  The alternative is to use
    // JS_FrameIterator in the throw hook which will get a little more
    // information.  Unfortunately neither of these methods will show calls
    // to native functions.  Only JS calls get new frames, native calls are
    // just objects within the frame.  There is no public functions to
    // examine the frame with this detail.

    // See vm/Stack.h:59 for vm layout and
    // Stack.cpp:StackIter::settleOnNewState for an example.
    for (depth = 0; depth < exn_priv->stackDepth; depth++) {
        JSStackTraceElem *te = &exn_priv->stackElems[depth];
        JSString *func_str = te->funName;
        char *func_cstr = NULL;
        const char* alt_func_cstr = "?";
        if (func_str) {
            // This takes space on the JS stack so if this error is an OOM we
            // could end up crashing here.
            func_cstr = JS_EncodeString(cx, func_str);
        }
        if (func_cstr && func_cstr[0] == '\0') {
            alt_func_cstr = "null";
            JS_free(cx, func_cstr);
            func_cstr = NULL;
        }
        if (depth == 0 && report->filename && te->filename
                && strcmp(report->filename, te->filename)) {
            fprintf(stderr, "\tat ? (%s:%d:%d)\n", report->filename,
                    (unsigned)report->lineno,
                    (unsigned)report->column);
        }
        fprintf(stderr, "\tat %s (%s:%d:%d)\n",
                func_cstr ? func_cstr : alt_func_cstr,
                te->filename ? te->filename : "?",
                te->ulineno, exn_priv->column);
        if (func_cstr)
            JS_free(cx, func_cstr);
    }

    return;

no_trace:
    fprintf(stderr, "\tat ? (%s:%d:%d)\n",
            report->filename ? report->filename : "?",
            (unsigned)report->lineno,
            (unsigned)report->column);
}

static const char *find_builtin_script(BeepJSRuntime *brt,
        const char *name) {
    for (std::list<BeepJSBuiltinScript>::iterator it =
            brt->builtin_scripts->begin(); it != brt->builtin_scripts->end();
            it++) {
        if (!strcmp(name, (*it).name)) {
            return (*it).script;
        }
    }
    return NULL;
}

static int compile_builtin(JSContext *cx, JSObject *robj, const char *name,
        JSScript **script) {
    BeepJSRuntime *brt = beepjs_get_brt(cx);
    const char *scr_str = find_builtin_script(brt, name);
    if (!scr_str)
        return 0;

    *script = JS_CompileScript(cx, robj, scr_str, strlen(scr_str), name, 0);

    return 1;
}

static int compile_file(JSContext *cx, JSObject *robj, const char *name,
        JSScript **script) {
    FILE *file = fopen(name, "r");

    if (!file) {
        char *alt_name;
        // Must copy string to prevent changing environment
        char *beepjs_path = getenv("BEEPJS_PATH");
        char *sys_paths;

        if(beepjs_path) {
            sys_paths = strdup(beepjs_path);

            for(char *sys_path = strtok(sys_paths,":");
                    sys_path != NULL;
                    sys_path = strtok(NULL,":")) {
                if (sys_path && asprintf(&alt_name, "%s/%s", sys_path, name) != -1) {
                    file = fopen(alt_name, "r");
                    free(alt_name);
                    if(file) {
                        break;
                    }
                }
            }

            free(sys_paths);
        }
    }


    if (!file) {
        return 0;
    }

    *script = JS_CompileUTF8FileHandle(cx, robj, name, file);
    fclose(file);

    return 1;
}

static int exec_script(JSContext *cx, JSObject *obj, const char *name,
        jsval *result) {
    BeepJSRuntime *brt = beepjs_get_brt(cx);
    BeepJSContext *bcx = beepjs_get_bcx(cx);
    JS::RootedScript script(cx);
    JS::RootedObject robj(cx, obj);
    uint32_t old_opts = JS_GetOptions(cx);
    uint32_t new_opts = 0;
    JSBool exec_status;
    bool cached = false;

    if (!brt->opts->no_comp_cache) {
        for (std::list<BeepJSCachedScript *>::iterator it =
                bcx->cached_scripts->begin();
                it != bcx->cached_scripts->end(); it++) {
            if (!strcmp((*it)->name, name)) {
                script = (*it)->script;
                cached = true;
                break;
            }
        }
    } else {
        new_opts |= JSOPTION_COMPILE_N_GO;
    }

    if (!result)
        new_opts |= JSOPTION_NO_SCRIPT_RVAL;

    JS_SetOptions(cx, old_opts | new_opts);

    if (!cached) {
        if (!compile_builtin(cx, robj, name, script.address())
                && !compile_file(cx, robj, name, script.address())) {
            JS_SetOptions(cx, old_opts);
            THROW_ERROR(cx, "can not find script: %s", name);
            return 0;
        }
    }

    if (!brt->opts->no_comp_cache && !cached && script.get()) {
        BeepJSCachedScript *cached_script = (BeepJSCachedScript *)
                JS_malloc(cx, sizeof(BeepJSCachedScript));
        if (!cached_script) {
            THROW_ERROR(cx, JSE_OOM);
            return 0;
        }
        cached_script->name = strdup(name);
        cached_script->script = script;
        JS_AddNamedScriptRoot(cx, &cached_script->script, NULL);
        bcx->cached_scripts->push_back(cached_script);
    }

    exec_status = script ? JS_ExecuteScript(cx, robj, script, result) :
            JS_FALSE;

    JS_SetOptions(cx, old_opts);

    return (exec_status == JS_TRUE) ? 1 : 0;
}
}  // namespace


void beepjs_destroy_rt(BeepJSRuntime *brt) {
    if (brt) {
        if (brt->rt)
            JS_DestroyRuntime(brt->rt);
        if (brt->builtin_scripts) {
            for (std::list<BeepJSBuiltinScript>::iterator it =
                    brt->builtin_scripts->begin(); it != brt->builtin_scripts->end();
                    it++) {
                free((*it).name);
            }
            delete brt->builtin_scripts;
        }
        free(brt);
    }
}

BeepJSRuntime *beepjs_new_rt(uint32_t maxbytes) {
    JSRuntime *rt = JS_NewRuntime(maxbytes);
    BeepJSRuntime *brt = NULL;

    if (rt == NULL)
        goto error;

    brt = (BeepJSRuntime *)malloc(sizeof(BeepJSRuntime));
    if (brt == NULL)
        goto error;
    memset(brt, 0, sizeof(BeepJSRuntime));

    brt->rt = rt;
    uv_uptime(&brt->start_time);
    brt->builtin_scripts = new std::list<BeepJSBuiltinScript>;
    JS_SetRuntimePrivate(rt, brt);

    return brt;

error:
    beepjs_destroy_rt(brt);
    return NULL;
}

BeepJSRuntime *beepjs_get_brt(JSContext *cx) {
    JSRuntime *rt = JS_GetRuntime(cx);
    return (BeepJSRuntime *)JS_GetRuntimePrivate(rt);
}

void beepjs_destroy_cx(BeepJSContext *bcx) {
    for (std::list<ModuleCxFunc>::iterator it = FiniFuncs->begin();
            it != FiniFuncs->end(); it++) {
        (*it).func.fini(bcx->cx);
    }
    for (std::list<BeepJSCachedScript *>::iterator it =
            bcx->cached_scripts->begin();
            it != bcx->cached_scripts->end(); it++) {
        JS_RemoveScriptRoot(bcx->cx, &((*it)->script));
        JS_free(bcx->cx, (*it)->name);
        JS_free(bcx->cx, *it);
    }
    JS_DestroyContext(bcx->cx);
    delete bcx->cached_scripts;
    free(bcx);
}

BeepJSContext *beepjs_new_cx(BeepJSRuntime *brt, size_t stackchunksize) {
    JSContext *cx;
    BeepJSContext *bcx = NULL;
    JSObject *global;

    cx = JS_NewContext(brt->rt, stackchunksize);
    if (cx == NULL)
        goto error;

    bcx = (BeepJSContext *)malloc(sizeof(BeepJSContext));
    if (bcx == NULL)
        goto error;
    memset(bcx, 0, sizeof(BeepJSContext));

    bcx->cx = cx;
    bcx->cached_scripts = new std::list<BeepJSCachedScript *>;
    JS_SetContextPrivate(cx, bcx);

    JS_SetOptions(cx, JSOPTION_METHODJIT);
    JS_SetVersion(cx, JSVERSION_LATEST);
#ifdef JS_GC_ZEAL
    int i;
    if (brt->opts->zeal_count) {
        DPRINTF("Setting gc zeal:");
        for (i = 0; i < brt->opts->zeal_count; i++) {
            DPRINTF(" %d=%d", brt->opts->zeal[(2 * i)],
                    brt->opts->zeal[(2 * i) + 1]);
            JS_SetGCZeal(cx, brt->opts->zeal[(2 * i)],
                    brt->opts->zeal[(2 * i) + 1]);
        }
        DPRINTF("\n");
        brt->opts->zeal_count = 0;
    }
#endif

    JS_SetErrorReporter(cx, report_error);

    global = JS_NewGlobalObject(cx, &JSBase::GlobalClass, NULL);
    if (global == NULL)
        goto error;
    if (!JS_InitStandardClasses(cx, global))
        goto error;
    if (!JS_DefineFunctions(cx, global, JSBase::GlobalFuncs))
        goto error;

    {
        JS::RootedObject beepjs_obj(cx, JS_DefineObject(cx, global, "beepjs",
                &JSBase::BeepJSClass, NULL, JSPROP_ENUMERATE));
        if (!beepjs_obj)
            goto error;
        if (!JS_DefineFunctions(cx, beepjs_obj, JSBase::BeepJSFuncs))
            goto error;

        JS::RootedObject natives_obj(cx, JS_NewObject(cx, NULL, NULL, NULL));
        if (!natives_obj)
            goto error;
        jsval vp = OBJECT_TO_JSVAL(natives_obj);
        if (!JS_SetProperty(cx, beepjs_obj, "natives", &vp))
            goto error;
    }

    for (std::list<ModuleCxFunc>::iterator it = InitFuncs->begin();
            it != InitFuncs->end(); it++) {
        if (!(*it).func.init(cx)) {
            goto error;
        }
    }

    return bcx;

error:
    if (cx)
        JS_DestroyContext(cx);
    if (bcx) {
        if (bcx->cached_scripts)
            delete bcx->cached_scripts;
        free(bcx);
    }
    return NULL;
}

BeepJSContext *beepjs_get_bcx(JSContext *cx) {
    return (BeepJSContext *)JS_GetContextPrivate(cx);
}

__attribute__((destructor))
static void beepjs_delete_cx_funcs(void) {
    if (InitFuncs) {
        delete InitFuncs;
    }
    if (FiniFuncs) {
        delete FiniFuncs;
    }
}

void beepjs_insert_module_cx_func(std::list<ModuleCxFunc> *&funcs,
        const ModuleCxFunc &new_cx_func) {
    std::list<ModuleCxFunc>::iterator it;
    if (!funcs) {
        funcs = new std::list<ModuleCxFunc>;
    }
    for (it = funcs->begin(); it != funcs->end(); it++) {
        if (new_cx_func.pri < (*it).pri) {
            funcs->insert(it, new_cx_func);
            break;
        }
    }
    if (it == funcs->end())
        funcs->push_back(new_cx_func);
}

void beepjs_add_module_init_func(ModuleInitFunc func, int pri) {
    ModuleCxFunc new_cx_func;
    new_cx_func.pri = pri;
    new_cx_func.func.init = func;
    beepjs_insert_module_cx_func(InitFuncs, new_cx_func);
}

void beepjs_add_module_fini_func(ModuleFiniFunc func, int pri) {
    ModuleCxFunc new_cx_func;
    new_cx_func.pri = pri;
    new_cx_func.func.fini = func;
    beepjs_insert_module_cx_func(FiniFuncs, new_cx_func);
}

JSObject *beepjs_get_native_space(JSContext *cx, const char *name) {
    JSObject *beepjs_obj = GetBeepJS(cx);
    JSObject *natives_obj;
    jsval natives;
    jsval ret;
    JSBool hp;
    if (!beepjs_obj)
        return NULL;
    if (!JS_GetProperty(cx, beepjs_obj, "natives", &natives))
        return NULL;
    natives_obj = JSVAL_TO_OBJECT(natives);
    // Special case to just return natives if name == ""
    if (strcmp(name, "") == 0)
        return natives_obj;
    if (!JS_HasProperty(cx, natives_obj, name, &hp))
        return NULL;
    if (hp != JS_TRUE)
        return NULL;
    if (!JS_GetProperty(cx, natives_obj, name, &ret))
        return NULL;
    return JSVAL_TO_OBJECT(ret);
}

JSObject *beepjs_create_native_space(JSContext *cx, const char *name) {
    JSObject *natives_obj = beepjs_get_native_space(cx, "");
    JSObject *check = beepjs_get_native_space(cx, name);
    // Return null if there is a property with the same name.
    if (!natives_obj || check)
        return NULL;
    JS::RootedObject new_space_obj(cx, JS_NewObject(cx, NULL, NULL, NULL));
    jsval vp = OBJECT_TO_JSVAL(new_space_obj);
    if (!JS_SetProperty(cx, natives_obj, name, &vp))
        return NULL;
    return new_space_obj;
}

void beepjs_delete_native_space(JSContext *cx, const char *name) {
    JSObject *natives_obj = beepjs_get_native_space(cx, "");
    JSObject *check = beepjs_get_native_space(cx, name);
    if (check) {
        JS_DeleteProperty(cx, natives_obj, name);
    }
}

int beepjs_add_builtin_script(JSContext *cx, const char *name,
        const char *script) {
    BeepJSRuntime *brt = beepjs_get_brt(cx);
    const char *check = find_builtin_script(brt, name);
    BeepJSBuiltinScript new_builtin;
    char *name_dup;

    if (check) {
        return (check == script) ? 1 : 0;
    }

    name_dup = strdup(name);
    new_builtin.name = name_dup;
    new_builtin.script = script;
    brt->builtin_scripts->push_back(new_builtin);

    return 1;
}

int beepjs_exec_script(JSContext *cx, JSObject *obj, const char *name,
        jsval *result, bool isolate) {
    if (isolate) {
        return exec_script(cx, obj, name, result);
    } else {
        // The issue with JS_ExecuteScript is regardless of the object each
        // script is compiled and executed with any orphaned object created in
        // that script (i.e. 'var foo = 'bar'') will get placed on the current
        // global object marked with JSCLASS_GLOBAL_FLAGS.  This will
        // effectively give all scripts the same namespace.

        // To work around this _start.js::execScript creates a Module object
        // containing everything we want each script to have on start.  It is
        // a simple object that contains named properties with default getters
        // so it is simple to copy each properties.

        // This code will create a new global object which also requires a new
        // compartment and then copies each property over to the new global.
        // Anything the executed scripts needs to pass back to the caller is
        // done via the copied exports object.  After the autocompartment goes
        // out of scope the gc is free to collect anything not referenced
        // through exports.

        // The undocumented magic function JS_WrapObject allows us to access
        // the passed in module object in the new compartment without causing
        // an abort inside of spidermonkey.  The alternative is to call
        // private members of cx which we don't have access to as it's an
        // incomplete type outside of spidermonkey.
        JS::RootedObject robj(cx, obj);
        JS::RootedObject new_global(cx, JS_NewGlobalObject(cx, &JSBase::GlobalClass, NULL));
        if (!new_global)
            return 0;
        {
            JSAutoCompartment ac(cx, new_global);
            if (!JS_InitStandardClasses(cx, new_global))
                return 0;
            if (!JS_DefineFunctions(cx, new_global, JSBase::GlobalFuncs))
                return 0;
            if (!JS_WrapObject(cx, robj.address()))
                return 0;
            JS::RootedObject iter(cx, JS_NewPropertyIterator(cx, robj));
            if (!iter)
                return 0;
            jsid pid = INT_TO_JSID(0);
            JSBool np;
            for (; (np = JS_NextProperty(cx, iter, &pid)) && !JSID_IS_VOID(pid);) {
                jsval pidval;
                assert(JS_IdToValue(cx, pid, &pidval));
                JSString *pid_str = JS_ValueToString(cx, pidval);
                if (pid_str) {
                    char *pid_cstr = JS_EncodeString(cx, pid_str);
                    if (pid_cstr) {
                        jsval pval;
                        if (!JS_GetPropertyById(cx, robj, pid, &pval))
                            return 0;
                        if (!JS_SetProperty(cx, new_global, pid_cstr, &pval))
                            return 0;
                        JS_free(cx, pid_cstr);
                    } else {
                        return 0;
                    }
                } else {
                    return 0;
                }
            }

            return exec_script(cx, new_global, name, result);
        }
    }
}
