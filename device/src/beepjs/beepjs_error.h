#ifndef BEEPJS_ERROR_H
#define BEEPJS_ERROR_H

typedef enum {
    JSE_UNKNOWN = 0,
    JSE_OOM,
    JSE_INTERNAL,
    JSE_UNSUPPORTED,
    JSE_NOT_READY,
    JSE_BAD_ARGS,
    JSE_EXP_FOR_ARG,
    JSE_REQ_MORE_ARGS,
    JSE_NUM_OOR,
    JSE_INDEX_OOR,
    JSE_LENGTH_OOR,
    JSE_INVALID_PERM,
    JSE_INVALID_ENC
} BeepJSErrCode;

extern const char *js_err_types[];

#define JSE_T_BOOLEAN   js_err_types[0]
#define JSE_T_NUMBER    js_err_types[1]
#define JSE_T_STRING    js_err_types[2]
#define JSE_T_OBJECT    js_err_types[3]
#define JSE_T_FUNCTION  js_err_types[4]
#define JSE_T_BUFFER    js_err_types[5]

JSBool beepjs_throw_error(JSContext *cx, const char *file, int line,
        JSObject *exn_obj);
JSObject *beepjs_new_error(JSContext *cx, const char *file, int line,
        JSString *err_str);
JSBool beepjs_throw_error(JSContext *cx, const char *file, int line,
        JSString *err_str);
JSObject *beepjs_new_error(JSContext *cx, const char *file, int line,
        BeepJSErrCode jse, ...);
JSBool beepjs_throw_error(JSContext *cx, const char *file, int line,
        BeepJSErrCode jse, ...);
JSObject *beepjs_new_error(JSContext *cx, const char *file, int line,
        const char *fmt, ...) __attribute__((format (printf, 4, 5)));
JSBool beepjs_throw_error(JSContext *cx, const char *file, int line,
        const char *fmt, ...) __attribute__((format (printf, 4, 5)));
JSObject *beepjs_new_uv_error(JSContext *cx, const char *file, int line,
        uv_err_code code, const char *call, const char *path);

#define THROW_ERROR(__CX__, ...) \
    beepjs_throw_error(__CX__, __FILE__, __LINE__, __VA_ARGS__)
#define ERROR(__CX__, ...) \
    beepjs_new_error(__CX__, __FILE__, __LINE__, __VA_ARGS__)
#define UV_ERROR(__CX__, __CODE__, __CALL__, __PATH__) \
    beepjs_new_uv_error(__CX__, __FILE__, __LINE__, __CODE__, __CALL__, __PATH__)

#endif
