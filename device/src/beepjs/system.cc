#include "beepjs.h"


namespace {
namespace JSSystem {

uv_timer_t gc_t_handle;

static JSBool Backtrace(JSContext *cx, unsigned argc, jsval *vp);
static JSBool ExecScript(JSContext *cx, unsigned argc, jsval *vp);
static JSBool GC(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Shell(JSContext *cx, unsigned argc, jsval *vp)
        __attribute__((__unused__));

static void DoGC(JSContext *cx, bool manual);
static void GCTimerCB(uv_timer_t *handle, int status);

static JSClass Class = {
    "system",
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

//static JSPropertySpec Props[] = {
//    {NULL, 0, 0, JSOP_NULLWRAPPER, JSOP_NULLWRAPPER}
//};

static JSFunctionSpec Funcs[] = {
    JS_FS("backtrace", Backtrace, 0, JSPROP_ENUMERATE),
    JS_FS("execScript", ExecScript, 0, JSPROP_ENUMERATE),
    JS_FS("GC", GC, 0, JSPROP_ENUMERATE),
    //JS_FS("shell", Shell, 1, JSPROP_ENUMERATE),
    JS_FS_END
};
}  // namespace JSSystem


namespace JSStdOS {

static JSBool Write(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Flush(JSContext *cx, unsigned argc, jsval *vp);
static FILE *GetFp(JSContext *cx, jsval *vp);
static int Init(JSContext *cx, JSObject *obj, const char *name, FILE *fp);

static JSClass Class = {
    "std_ostream",
    JSCLASS_HAS_PRIVATE,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    NULL
};

//static JSPropertySpec Props[] = {
//    {NULL, 0, 0, JSOP_NULLWRAPPER, JSOP_NULLWRAPPER}
//};

static JSFunctionSpec Funcs[] = {
    JS_FS("write", JSStdOS::Write, 0, JSPROP_ENUMERATE),
    JS_FS("flush", JSStdOS::Flush, 0, JSPROP_ENUMERATE),
    JS_FS_END
};
} // namespace JSStdOS


JSBool JSSystem::Backtrace(JSContext *cx, unsigned argc, jsval *vp) {
    js_DumpBacktrace(cx);
    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSSystem::ExecScript(JSContext *cx, unsigned argc, jsval *vp) {
    JS::RootedString name_str(cx);
    JS::RootedObject scope_obj(cx);
    JS::RootedValue rval(cx);
    char *name;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "So",
            name_str.address(), scope_obj.address())) {
        return JS_FALSE;
    }
    name = JS_EncodeString(cx, name_str);
    if (!name) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }

    if (!beepjs_exec_script(cx, scope_obj, name, rval.address(), true)) {
        JS_free(cx, name);
        return JS_FALSE;
    }

    JS_free(cx, name);
    JS_SET_RVAL(cx, vp, rval);
    return JS_TRUE;
}

JSBool JSSystem::GC(JSContext *cx, unsigned argc, jsval *vp) {
    DoGC(cx, true);
    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSSystem::Shell(JSContext *cx, unsigned argc, jsval *vp) {
    JSString *str;
    char *cmd;
    int rc;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "S", &str)) {
        return JS_FALSE;
    }

    cmd = JS_EncodeString(cx, str);
    rc = system(cmd);
    JS_free(cx, cmd);
    if (rc != 0) {
        return THROW_ERROR(cx, "shell command failed with exit code %d", rc);
    }

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

void JSSystem::DoGC(JSContext *cx, bool manual) {
    DPRINTF("%s called! %d\n", __PRETTY_FUNCTION__, manual);
    if (manual && gc_t_handle.data) {
        uv_timer_again(&gc_t_handle);
    }
    JS_GC(JS_GetRuntime(cx));
}

void JSSystem::GCTimerCB(uv_timer_t *handle, int status) {
    DoGC((JSContext *)handle->data, false);
}

JSBool JSStdOS::Write(JSContext *cx, unsigned argc, jsval *vp) {
    FILE *fp = GetFp(cx, vp);
    jsval *argv;
    char *fullstr;

    if (!fp) {
        return THROW_ERROR(cx, JSE_INTERNAL);
    }

    argv = JS_ARGV(cx, vp);
    fullstr = JSUtils::ArgvToCStr(cx, argc, argv);
    if (!fullstr) {
        return JS_FALSE;
    }

    fputs(fullstr, fp);
    free(fullstr);

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSStdOS::Flush(JSContext *cx, unsigned argc, jsval *vp) {
    FILE *fp = GetFp(cx, vp);
    if (!fp) {
        return THROW_ERROR(cx, JSE_INTERNAL);
    }
    fflush(fp);
    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

FILE *JSStdOS::GetFp(JSContext *cx, jsval *vp) {
    JSObject *this_ = JS_THIS_OBJECT(cx, vp);
    if (!this_) {
        return NULL;
    }
    return (FILE *)JS_GetInstancePrivate(cx, this_, &Class, NULL);
}

int JSStdOS::Init(JSContext *cx, JSObject *obj, const char *name, FILE *fp) {
    JS::RootedObject std_so_obj(cx, JS_DefineObject(cx, obj, name,
        &Class, NULL, JSPROP_ENUMERATE));
    if (!std_so_obj) {
        return 0;
    }
    if (!JS_DefineFunctions(cx, std_so_obj, Funcs)) {
        return 0;
    }
    JS_SetPrivate(std_so_obj, fp);
    return 1;
}

static void fini_sys_module(JSContext *cx) {
    if (JSSystem::gc_t_handle.data) {
        uv_timer_stop(&JSSystem::gc_t_handle);
        JSSystem::gc_t_handle.data = NULL;
    }
    beepjs_delete_native_space(cx, "system");
}

static int init_sys_module(JSContext *cx) {
    BeepJSRuntime *brt = beepjs_get_brt(cx);
    JSObject *natives_obj = beepjs_get_native_space(cx, "");
    JS::RootedObject mod_obj(cx, JS_DefineObject(cx, natives_obj, "system",
            &JSSystem::Class, NULL, JSPROP_ENUMERATE));
    if (!mod_obj) {
        return 0;
    }
    if (!JS_DefineFunctions(cx, mod_obj, JSSystem::Funcs)) {
        return 0;
    }
    if (!JSStdOS::Init(cx, mod_obj, "stdout", stdout) ||
        !JSStdOS::Init(cx, mod_obj, "stderr", stderr)) {
        return 0;
    }

    if (brt->opts->gc_timer && !JSSystem::gc_t_handle.data) {
        if (uv_timer_init(uv_default_loop(), &JSSystem::gc_t_handle)) {
            return 0;
        }
        if (uv_timer_start(&JSSystem::gc_t_handle, JSSystem::GCTimerCB,
                brt->opts->gc_timer * 1000, brt->opts->gc_timer * 1000)) {
            return 0;
        }

        // Prevent the gc timer from keeping the uv loop alive.
        uv_unref((uv_handle_t *)&JSSystem::gc_t_handle);

        // Set this context as the priv so we can call GC.
        JSSystem::gc_t_handle.data = cx;
    }

    return 1;
}
}  // namespace


BEEPJS_MODULE_INIT(init_sys_module, 0);
BEEPJS_MODULE_FINI(fini_sys_module, 0);
