#include <unistd.h>

#include "beepjs.h"


#ifdef _MIPS_ARCH
#define BEEPJS_ARCH_STRING "mips"
#else
#define BEEPJS_ARCH_STRING "x86"
#endif

namespace {
namespace JSProcess {

static JSBool Abort(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Binding(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Exit(JSContext *cx, unsigned argc, jsval *vp);
static JSBool UVStop(JSContext *cx, unsigned argc, jsval *vp);

static JSClass Class = {
    "process",
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
    JS_FS("abort", JSProcess::Abort, 0, JSPROP_ENUMERATE),
    JS_FS("binding", JSProcess::Binding, 0, JSPROP_ENUMERATE),
    JS_FS("exit", JSProcess::Exit, 0, JSPROP_ENUMERATE),
    JS_FS("uvStop", JSProcess::UVStop, 0, JSPROP_ENUMERATE),
    JS_FS_END
};
}  // namespace JSProcess

JSBool JSProcess::Abort(JSContext *cx, unsigned argc, jsval *vp) {
    abort();
    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSProcess::Binding(JSContext *cx, unsigned argc, jsval *vp) {
    JSString *name_str;
    char *name_cstr;
    JSObject *binding_obj;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "S", &name_str)) {
        return JS_FALSE;
    }
    name_cstr = JS_EncodeString(cx, name_str);
    if (!name_cstr) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }

    binding_obj = beepjs_get_native_space(cx, name_cstr);
    if (binding_obj) {
        JS_SET_RVAL(cx, vp, OBJECT_TO_JSVAL(binding_obj));
    } else {
        JS_SET_RVAL(cx, vp, JSVAL_NULL);
    }
    JS_free(cx, name_cstr);

    return JS_TRUE;
}

JSBool JSProcess::UVStop(JSContext *cx, unsigned argc, jsval *vp) {
    uv_stop(uv_default_loop());
    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSProcess::Exit(JSContext *cx, unsigned argc, jsval *vp) {
    int status;
    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "i", &status)) {
        status = -255;
    }
    // Not very nice.  Should really halt uv_default_loop and let everything
    // shutdown correctly.
    exit(status);
    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

static void fini_process_module(JSContext *cx) {
    beepjs_delete_native_space(cx, "process");
}

static int argv_to_array(JSContext *cx, JSObject *obj, const char *name,
        int argc, char **argv) {
    jsval vp;
    int i;
    JS::RootedObject argv_obj(cx, JS_NewArrayObject(cx, 0, NULL));
    vp = OBJECT_TO_JSVAL(argv_obj);
    if (!JS_SetProperty(cx, obj, name, &vp)) {
        return 0;
    }
    for (i = 0; i < argc; i++) {
        JS::RootedString arg_str(cx, JS_NewStringCopyZ(cx, argv[i]));
        vp = STRING_TO_JSVAL(arg_str);
        JS_SetElement(cx, argv_obj, i, &vp);
    }
    return 1;
}

static int init_process_module(JSContext *cx) {
    JSObject *natives_obj = beepjs_get_native_space(cx, "");
    JS::RootedObject mod_obj(cx, JS_DefineObject(cx, natives_obj, "process",
            &JSProcess::Class, NULL, JSPROP_ENUMERATE));
    jsval vp;
    BeepJSRuntime *brt = beepjs_get_brt(cx);
    if (!mod_obj) {
        return 0;
    }
    if (!JS_DefineFunctions(cx, mod_obj, JSProcess::Funcs)) {
        return 0;
    }

    // Setup process.{execArgv,argv}.
    if (!argv_to_array(cx, mod_obj, "execArgv", brt->opts->exec_argc,
            brt->opts->exec_argv)) {
        return 0;
    }
    if (!argv_to_array(cx, mod_obj, "argv", brt->opts->js_argc,
            brt->opts->js_argv)) {
        return 0;
    }

    JS::RootedObject env_obj(cx, JS_NewObject(cx, NULL, NULL, NULL));
    vp = OBJECT_TO_JSVAL(env_obj);
    if (!JS_SetProperty(cx, mod_obj, "env", &vp)) {
        return 0;
    }
    for (int i = 0; environ[i]; i++) {
        char *val = strstr(environ[i], "=");
        if (!val)
            continue;
        *val = '\0';
        JS::RootedString val_str(cx, JS_NewStringCopyZ(cx, val + 1));
        vp = STRING_TO_JSVAL(val_str);
        JS_SetProperty(cx, env_obj, environ[i], &vp);
        *val = '=';
    }

    JS::RootedString plat_str(cx, JS_NewStringCopyZ(cx, "linux"));
    vp = STRING_TO_JSVAL(plat_str);
    JS_SetProperty(cx, mod_obj, "platform", &vp);

    JS::RootedString arch_str(cx, JS_NewStringCopyZ(cx, BEEPJS_ARCH_STRING));
    vp = STRING_TO_JSVAL(arch_str);
    JS_SetProperty(cx, mod_obj, "arch", &vp);

    return 1;
}
}  // namespace


BEEPJS_MODULE_INIT(init_process_module, -10);
BEEPJS_MODULE_FINI(fini_process_module, 110);
