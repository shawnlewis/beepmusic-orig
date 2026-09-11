#include <assert.h>
#include <cstdarg>

#include "beepjs.h"

typedef struct {
    BeepJSErrCode jse;
    const char *fmt;
} BeepJSErr;

static const BeepJSErr js_errs[] = {
    {JSE_UNKNOWN, "unknown"},
    {JSE_OOM, "out of memory"},
    {JSE_INTERNAL, "internal"},
    {JSE_UNSUPPORTED, "%s unsupported"},
    {JSE_NOT_READY, "not ready"},
    {JSE_BAD_ARGS, "bad args"},
    {JSE_EXP_FOR_ARG, "expected %s for arg %d"},
    {JSE_REQ_MORE_ARGS, "requires more than %d arguments"},
    {JSE_NUM_OOR, "number out of range"},
    {JSE_INDEX_OOR, "index out of range"},
    {JSE_LENGTH_OOR, "length out of range"},
    {JSE_INVALID_PERM, "invalid permission"},
    {JSE_INVALID_ENC, "invalid encoding %s"}
};

const char *js_err_types[] = {
    "boolean",
    "number",
    "string",
    "object",
    "function",
    "buffer"
};

// The throw functions return false so it can be used like:
// if(err) return beepjs_throw_error(error stuff);
JSBool beepjs_throw_error(JSContext *cx, const char *file, int line,
        JSObject *exn_obj) {
    if (!exn_obj) {
        fprintf(stderr, "Error: null error object (%s:%d)\n", file, line);
        assert(exn_obj);
    }
    JS_SetPendingException(cx, OBJECT_TO_JSVAL(exn_obj));
    return JS_FALSE;
}

JSObject *beepjs_new_error(JSContext *cx, const char *file, int line,
        JSString *err_str) {
    JSObject *global = JS_GetGlobalForScopeChain(cx);
    JSString *file_str = JS_NewStringCopyZ(cx, file);
    jsval argv[3];
    jsval rval;

    if (!global || !err_str || !file_str) {
        return NULL;
    }

    argv[0] = STRING_TO_JSVAL(err_str);
    argv[1] = STRING_TO_JSVAL(file_str);
    argv[2] = INT_TO_JSVAL(line);

    if (!JS::Call(cx, global, "Error", 3, argv, &rval))
        return NULL;

    return JSVAL_TO_OBJECT(rval);
}

JSBool beepjs_throw_error(JSContext *cx, const char *file, int line,
        JSString *err_str) {
    return beepjs_throw_error(cx, file, line,
            beepjs_new_error(cx, file, line, err_str));
}

__attribute__((constructor))
static void beepjs_check_errs(void) {
    // Checks js_errs to make sure indexes are contiguously incrementing.
    // Ensures offsets are the same as BeepJSErrCode values.
    unsigned int i;
    for (i = 0; i < sizeof(js_errs) / sizeof(BeepJSErr); i++)
        assert(js_errs[i].jse == i);
}

static const BeepJSErr *beepjs_get_err(BeepJSErrCode jse) {
    if (jse < (sizeof(js_errs) / sizeof(BeepJSErr)))
        return &js_errs[jse];
    return NULL;
}

static JSObject *beepjs_new_error_va(JSContext *cx, const char *file, int line,
        BeepJSErrCode jse, va_list ap) {
    const BeepJSErr *js_err = beepjs_get_err(jse);
    char *err_cstr;
    int vas_ret;
    JSString *err_str;
    JSObject *err_obj;

    if (!js_err) {
        return NULL;
    }

    vas_ret = vasprintf(&err_cstr, js_err->fmt, ap);

    if (vas_ret == -1) {
        return NULL;
    }

    err_str = JS_NewStringCopyZ(cx, err_cstr);
    err_obj = beepjs_new_error(cx, file, line, err_str);
    free(err_cstr);

    return err_obj;
}

JSObject *beepjs_new_error(JSContext *cx, const char *file, int line,
        BeepJSErrCode jse, ...) {
    va_list ap;
    JSObject *err_obj;
    va_start(ap, jse);
    err_obj = beepjs_new_error_va(cx, file, line, jse, ap);
    va_end(ap);
    return err_obj;
}

JSBool beepjs_throw_error(JSContext *cx, const char *file, int line,
        BeepJSErrCode jse, ...) {
    va_list ap;
    JSObject *err_obj;
    va_start(ap, jse);
    err_obj = beepjs_new_error_va(cx, file, line, jse, ap);
    va_end(ap);
    return beepjs_throw_error(cx, file, line, err_obj);
}

static JSObject *beepjs_new_error_va(JSContext *cx, const char *file, int line,
        const char *fmt, va_list ap) {
    char *err_cstr;
    int vas_ret;
    JSString *err_str;
    JSObject *err_obj;

    vas_ret = vasprintf(&err_cstr, fmt, ap);

    if (vas_ret == -1) {
        return NULL;
    }

    err_str = JS_NewStringCopyZ(cx, err_cstr);
    err_obj = beepjs_new_error(cx, file, line, err_str);
    free(err_cstr);

    return err_obj;
}

JSObject *beepjs_new_error(JSContext *cx, const char *file, int line,
        const char *fmt, ...) {
    va_list ap;
    JSObject *err_obj;
    va_start(ap, fmt);
    err_obj = beepjs_new_error_va(cx, file, line, fmt, ap);
    va_end(ap);
    return err_obj;
}

JSBool beepjs_throw_error(JSContext *cx, const char *file, int line,
        const char *fmt, ...) {
    va_list ap;
    JSObject *err_obj;
    va_start(ap, fmt);
    err_obj = beepjs_new_error_va(cx, file, line, fmt, ap);
    va_end(ap);
    return beepjs_throw_error(cx, file, line, err_obj);
}

JSObject *beepjs_new_uv_error(JSContext *cx, const char *file, int line,
        uv_err_code code, const char *call, const char *path) {
    uv_err_t uv_err;
    const char *err_name;
    const char *strerror;
    static const char * const fmt = "uv_%s %s, %s (%s)";

    uv_err.code = code;
    err_name = uv_err_name(uv_err);
    strerror = uv_strerror(uv_err);

    return beepjs_new_error(cx, file, line, fmt, call, err_name, strerror,
            path);
}
