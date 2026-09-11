#include <assert.h>
#include <unistd.h>

#include "beepjs.h"

extern "C" {
#include <libubox/usock.h>
// libubus.h will include inline functions that are not compatible with C++.
#include "beep/app.h"
#include "beep/beep_ubus.h"
#include "beep/beep_ubus_debug.h"
#include "beep/flags.h"
}  // extern "C"

#define UBUS_PRIV_FROM_MEMBER(__NS__, __THIS_PTR__, __MEMBER__) \
    reinterpret_cast<__NS__::UbusPriv *>((uint8_t *)__THIS_PTR__ \
    - offsetof(__NS__::UbusPriv, __MEMBER__))

#define SOCK_FILE "/tmp/beepjs.sock"
#define JSONSTR_UNKNOWN_JS_ERROR \
    "{\"success\":false,\"error_code\":0,\"error_message\":\"unknown js error\"}"

namespace {
static bool app_thread_started;
static pthread_t app_thread;
static pthread_mutex_t app_start_mutex;
static pthread_cond_t app_start_cond;
static uloop_fd uloop_fd_app;
static struct ubus_context *ubus_ctx;

namespace JSBeepApp {

typedef struct UbusPriv_s UbusPriv;

typedef struct {
    char *pattern;
    jsval js_cb_func;
} UbusJSEvCallback;

typedef struct {
    JSContext *cx;
    JSObject *jsthis;
    char *app_name;
    UbusPriv *ubus_priv;
    std::list<UbusJSEvCallback *> *ev_cb_list;
    int token;
    int js_ubus_sock;
    uv_poll_t p_handle;
    bool app_started;
} Priv;

struct UbusPriv_s {
    Priv *js_priv;
    char *app_name;
    struct ubus_event_handler ev_hdlr;
    struct ubus_object ubus_object;
    struct ubus_object_type type;
    uloop_fd uloop_fd_read;
    int ubus_js_sock;
};

typedef enum {
    UBUS_JS_METHOD,
    UBUS_JS_BEEP_STATE,
    UBUS_JS_EV_REGISTER,
    UBUS_JS_EV_EVENT
} UbusJSMsgType;

typedef struct {
    struct ubus_context *ctx;
    struct ubus_request_data *req;
    char *method;
    char *msg;
    int ret;
} UbusJSMethodMsg;

typedef struct {
    struct ubus_context *ctx;
    char *component;
    char *event_type;
    char *event_data;
    char *state;
} UbusJSBeepStateMsg;

typedef struct {
    struct ubus_context *ctx;
    char *pattern;
} UbusJSEvRegister;

typedef struct {
    struct ubus_context *ctx;
    char *ev_name;
    char *ev_msg;
} UbusJSEvEvent;

typedef struct {
    UbusJSMsgType type;
    union {
        UbusJSMethodMsg method;
        UbusJSBeepStateMsg beep_state;
        UbusJSEvRegister ev_register;
        UbusJSEvEvent ev_event;
    } data;
} UbusJSMsg;

static JSBool Ctor(JSContext *cx, unsigned argc, jsval *vp);
static void Finalize(JSFreeOp *fop, JSObject *obj);
static JSBool PropGetter(JSContext* cx, JSHandleObject obj,
        JSHandleId idval, JSMutableHandleValue vp);
static JSBool StartApp(JSContext *cx, unsigned argc, jsval *vp);
static JSBool StopApp(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Subscribe(JSContext *cx, unsigned argc, jsval *vp);
static JSBool AudioAcquire(JSContext *cx, unsigned argc, jsval *vp);
static JSBool AudioCanTrackBegin(JSContext *cx, unsigned argc, jsval *vp);
static JSBool AudioTrackBegin(JSContext *cx, unsigned argc, jsval *vp);
static JSBool AudioTrackEnd(JSContext *cx, unsigned argc, jsval *vp);
static JSBool AudioCanBuffer(JSContext *cx, unsigned argc, jsval *vp);
static JSBool AudioBuffer(JSContext *cx, unsigned argc, jsval *vp);
static JSBool AudioStart(JSContext *cx, unsigned argc, jsval *vp);
static JSBool AudioPause(JSContext *cx, unsigned argc, jsval *vp);
static JSBool SetStation(JSContext *cx, unsigned argc, jsval *vp);
static JSBool SetVolume(JSContext *cx, unsigned argc, jsval *vp);
static JSBool SetTrackVolumeScalar(JSContext *cx, unsigned argc, jsval *vp);
static JSBool MsgSockSendMsg(JSContext *cx, unsigned argc, jsval *vp);
static JSBool MsgSockSockClose(JSContext *cx, unsigned argc, jsval *vp);
static JSBool BeepSendState(JSContext *cx, unsigned argc, jsval *vp);

static UbusJSEvCallback *FindEvCallback(Priv *priv, const char *pattern);
static void UbusServCallback(struct uloop_fd *fd, unsigned int events);
static void UVPollCallback(uv_poll_t *handle, int status, int events);
static void UbusPollCallback(struct uloop_fd *fd, unsigned int events);
static int UbusRecv(struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg);
static void UbusEvHandler(struct ubus_context *ctx,
        struct ubus_event_handler *ev, const char *type,
        struct blob_attr *msg);
static JSBool InitUbusPriv(Priv *priv);

static JSBool StartAppThread(void);
static void *AppThread(void *arg);

static JSClass Class = {
    "BeepApp",
    JSCLASS_HAS_PRIVATE,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    Finalize
};

typedef enum {
    P_TOKEN,
    P_STARTED
} Prop;

#define JSBEEPAPPPROP_FLAGS \
    (JSPROP_READONLY | JSPROP_PERMANENT | JSPROP_ENUMERATE)

static JSPropertySpec Props[] = {
    {"token", P_TOKEN, JSBEEPAPPPROP_FLAGS, JSOP_WRAPPER(PropGetter), JSOP_NULLWRAPPER},
    {"started", P_STARTED, JSBEEPAPPPROP_FLAGS, JSOP_WRAPPER(PropGetter), JSOP_NULLWRAPPER},
    {NULL, 0, 0, JSOP_NULLWRAPPER, JSOP_NULLWRAPPER}
};

static JSFunctionSpec Funcs[] = {
    JS_FS("startApp", StartApp, 0, JSPROP_ENUMERATE),
    JS_FS("stopApp", StopApp, 0, JSPROP_ENUMERATE),
    JS_FS("subscribe", Subscribe, 0, JSPROP_ENUMERATE),
    JS_FS("audioAcquire", AudioAcquire, 0, JSPROP_ENUMERATE),
    JS_FS("audioCanTrackBegin", AudioCanTrackBegin, 0, JSPROP_ENUMERATE),
    JS_FS("audioTrackBegin", AudioTrackBegin, 0, JSPROP_ENUMERATE),
    JS_FS("audioTrackEnd", AudioTrackEnd, 0, JSPROP_ENUMERATE),
    JS_FS("audioCanBuffer", AudioCanBuffer, 0, JSPROP_ENUMERATE),
    JS_FS("audioBuffer", AudioBuffer, 0, JSPROP_ENUMERATE),

    // TODO: get rid of audioStart, it's the same as audioResume
    JS_FS("audioStart", AudioStart, 0, JSPROP_ENUMERATE),
    JS_FS("setStation", SetStation, 0, JSPROP_ENUMERATE),
    JS_FS("setVolume", SetVolume, 0, JSPROP_ENUMERATE),
    JS_FS("setTrackVolumeScalar", SetTrackVolumeScalar, 0, JSPROP_ENUMERATE),
    JS_FS("audioPause", AudioPause, 0, JSPROP_ENUMERATE),
    JS_FS("audioResume", AudioStart, 0, JSPROP_ENUMERATE),
    JS_FS("msgSockSendMsg", MsgSockSendMsg, 0, JSPROP_ENUMERATE),
    JS_FS("msgSockSockClose", MsgSockSockClose, 0, JSPROP_ENUMERATE),
    JS_FS("_beepSendState", BeepSendState, 0, JSPROP_ENUMERATE),
    JS_FS_END
};

static JSObject *Proto;
}  // namespace JSBeepApp

namespace JSBeep {

LOG_CATEGORY *log_beepjs_main;

static JSBool Log(JSContext *cx, unsigned argc, jsval *vp);

static JSFunctionSpec Funcs[] = {
    JS_FS("log", Log, 0, JSPROP_ENUMERATE),
    JS_FS_END
};

static JSConstDoubleSpec LogLevels[] = {
    ENUM_TO_CONST_DOUBLE_SPEC(LOG_PRIORITY_TEST),
    ENUM_TO_CONST_DOUBLE_SPEC(LOG_PRIORITY_ERROR),
    ENUM_TO_CONST_DOUBLE_SPEC(LOG_PRIORITY_WARN),
    ENUM_TO_CONST_DOUBLE_SPEC(LOG_PRIORITY_INFO),
    ENUM_TO_CONST_DOUBLE_SPEC(LOG_PRIORITY_DEBUG),
    ENUM_TO_CONST_DOUBLE_SPEC(LOG_PRIORITY_TRACE),
    {0, NULL, 0, {0, 0, 0}}
};
}  // namespace JSBeep


JSBool JSBeepApp::Ctor(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    JSObject *beep_obj;
    Priv *priv = NULL;
    UbusPriv *ubus_priv = NULL;
    JSString *name_str;
    char *name_cstr = NULL;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "S", &name_str)) {
        return JS_FALSE;
    }
    name_cstr = JS_EncodeString(cx, name_str);
    if (!name_cstr) {
        THROW_ERROR(cx, JSE_BAD_ARGS);
        goto error;
    }

    beep_obj = JS_NewObject(cx, &Class, Proto, NULL);
    if (!beep_obj) {
        THROW_ERROR(cx, JSE_OOM);
        goto error;
    }

    priv = (Priv *)JS_malloc(cx, sizeof(Priv));
    if (!priv) {
        THROW_ERROR(cx, JSE_OOM);
        goto error;
    }
    // Do not use JS_malloc since this may be freed by the ubus thread.
    ubus_priv = (UbusPriv *)malloc(sizeof(UbusPriv));
    if (!ubus_priv) {
        THROW_ERROR(cx, JSE_OOM);
        goto error;
    }
    memset(priv, 0, sizeof(Priv));
    memset(ubus_priv, 0, sizeof(UbusPriv));
    DPRINTF("new js priv: %p ubus priv: %p\n", priv, ubus_priv);

    priv->cx = cx;
    priv->jsthis = beep_obj;
    priv->ubus_priv = ubus_priv;

    // Move app name from JS heap to standard heap.
    priv->app_name = strdup(name_cstr);
    if (!priv->app_name) {
        THROW_ERROR(cx, JSE_OOM);
        goto error;
    }
    JS_free(cx, name_cstr);

    priv->ev_cb_list = new std::list<UbusJSEvCallback *>;
    if (!priv->ev_cb_list) {
        THROW_ERROR(priv->cx, JSE_OOM);
        goto error;
    }

    if (!InitUbusPriv(priv)) {
        goto error;
    }

    JS_AddObjectRoot(cx, &priv->jsthis);
    priv->token = -1;

    JS_SetPrivate(beep_obj, (void *)priv);
    JS_SET_RVAL(cx, vp, OBJECT_TO_JSVAL(beep_obj));
    DPRINTF("%s success\n", __PRETTY_FUNCTION__);
    return JS_TRUE;

error:
    DPRINTF("%s error\n", __PRETTY_FUNCTION__);
    if (name_cstr)
        JS_free(cx, name_cstr);
    if (priv) {
        if (priv->app_name)
            free(priv->app_name);
        if (priv->ev_cb_list)
            delete priv->ev_cb_list;
        JS_free(cx, priv);
    }
    if (ubus_priv) {
        if (ubus_priv->app_name)
            free(ubus_priv->app_name);
        free(ubus_priv);
    }

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_FALSE;
}

void JSBeepApp::Finalize(JSFreeOp *fop, JSObject *obj) {
    Priv *priv = PRIV_FROM_FIN(JSBeepApp);
    if (priv) {
        // If priv and ubus_priv are still connected ubus was never connected
        // and the js thread is still responsible for freeing the memory.
        if (priv->ubus_priv) {
            if (priv->ubus_priv->app_name)
                free(priv->ubus_priv->app_name);

            free(priv->ubus_priv);
        }

        if (priv->app_name)
            free(priv->app_name);

        if (priv->ev_cb_list) {
            for (std::list<UbusJSEvCallback *>::iterator it =
                    priv->ev_cb_list->begin(); it != priv->ev_cb_list->end();
                    it++) {
                free((*it)->pattern);
                JS_RemoveValueRoot(priv->cx, &((*it)->js_cb_func));
                free(*it);
            }

            delete priv->ev_cb_list;
        }

        JS_freeop(fop, priv);
    }
    DPRINTF("%s called! %p %p %p\n", __PRETTY_FUNCTION__, fop, obj, priv);
}

JSBool JSBeepApp::PropGetter(JSContext* cx, JSHandleObject obj,
        JSHandleId idval, JSMutableHandleValue vp) {
    Priv *priv = PRIV_FROM_OBJ(JSBeepApp, obj);
    int32_t val = 0;

    switch (JSID_TO_INT(idval)) {

    case P_TOKEN:
        val = priv->token; break;

    case P_STARTED:
        val = priv->app_started; break;

    default:
        return THROW_ERROR(cx, JSE_INTERNAL);
    }

    vp.set(INT_TO_JSVAL(val));
    return JS_TRUE;
}

JSBool JSBeepApp::StartApp(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);
    ssize_t w_bytes;

    if (!app_thread_started) {
        if (!StartAppThread())
            return THROW_ERROR(cx, JSE_INTERNAL);
        // Just need to syncronize the on first app to make sure the ubus
        // thread is actually running since calls like audio_acquire can
        // hang.
        pthread_mutex_init(&app_start_mutex, NULL);
        pthread_cond_init(&app_start_cond, NULL);
        pthread_mutex_lock(&app_start_mutex);
    }

    priv->js_ubus_sock = usock(USOCK_UNIX, SOCK_FILE, NULL);
    assert(priv->js_ubus_sock > -1);

    // Write out the priv address for UbusServCallback to setup the other
    // end of the connection.
    w_bytes = write(priv->js_ubus_sock, &priv, sizeof(Priv *));
    assert(w_bytes == sizeof(Priv *));

    uv_poll_init(uv_default_loop(), &priv->p_handle, priv->js_ubus_sock);
    priv->p_handle.data = (void *)priv;
    uv_poll_start(&priv->p_handle, UV_READABLE, UVPollCallback);

    if (!app_thread_started) {
        pthread_cond_wait(&app_start_cond, &app_start_mutex);
        app_thread_started = true;
    }

    priv->app_started = true;

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSBeepApp::StopApp(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);

    uv_poll_stop(&priv->p_handle);
    uv_close((uv_handle_t *)&priv->p_handle, NULL);

    // Check if ubus is connected if so set these pointers to NULL indicated
    // ubus is now responsible for freeing it's memory.
    if (priv->ubus_priv && priv->ubus_priv->js_priv) {
        priv->ubus_priv->js_priv = NULL;
        priv->ubus_priv = NULL;
    }

    close(priv->js_ubus_sock);
    JS_RemoveObjectRoot(priv->cx, &priv->jsthis);

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSBeepApp::Subscribe(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);
    JSString *pattern_str;
    char *pattern_cstr;
    UbusJSEvCallback *ev_cb;
    UbusJSMsg uj_msg;
    ssize_t w_bytes;
    jsval js_cb_func;

    // Check if ubus is connected and ready.
    if (!priv->ubus_priv->js_priv) {
        return THROW_ERROR(cx, JSE_NOT_READY);
    }

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "Sv",
            &pattern_str, &js_cb_func)) {
        return JS_FALSE;
    }

    pattern_cstr = JS_EncodeString(cx, pattern_str);
    if (!pattern_cstr) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }

    ev_cb = FindEvCallback(priv, pattern_cstr);
    if (ev_cb) {
        // Already registered so just update the new callback and reset
        // the gc root.
        JS_RemoveValueRoot(cx, &ev_cb->js_cb_func);
        ev_cb->js_cb_func = js_cb_func;
        JS_AddValueRoot(cx, &ev_cb->js_cb_func);
        JS_free(cx, pattern_cstr);
        return JS_TRUE;
    }

    ev_cb = (UbusJSEvCallback *)malloc(sizeof(UbusJSEvCallback));
    if (!ev_cb) {
        JS_free(cx, pattern_cstr);
        return THROW_ERROR(cx, JSE_OOM);
    }

    // Make a copy of the pattern for the js thread and the ubus thread.
    ev_cb->pattern = strdup(pattern_cstr);
    uj_msg.data.ev_register.pattern = strdup(pattern_cstr);
    JS_free(cx, pattern_cstr);
    if (!ev_cb->pattern || !uj_msg.data.ev_register.pattern) {
        if (ev_cb->pattern) {
            free(ev_cb->pattern);
        }
        if (uj_msg.data.ev_register.pattern) {
            free(uj_msg.data.ev_register.pattern);
        }
        free(ev_cb);
        return THROW_ERROR(cx, JSE_OOM);
    }

    ev_cb->js_cb_func = js_cb_func;
    JS_AddValueRoot(cx, &ev_cb->js_cb_func);

    priv->ev_cb_list->push_back(ev_cb);

    uj_msg.type = UBUS_JS_EV_REGISTER;
    uj_msg.data.ev_register.ctx = ubus_ctx;
    uj_msg.data.ev_register.pattern = strdup(ev_cb->pattern);

    DPRINTF("js sending uj_msg ev_register:\n");
    DPRINTF("  uj_msg.type: %d\n", uj_msg.type);
    DPRINTF("  uj_msg.ctx: %p\n", uj_msg.data.ev_register.ctx);
    DPRINTF("  uj_msg.pattern: %s\n", uj_msg.data.ev_register.pattern);

    w_bytes = write(priv->js_ubus_sock, &uj_msg, sizeof(UbusJSMsg));
    assert(w_bytes == sizeof(UbusJSMsg));

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSBeepApp::AudioAcquire(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);
    int token;

    // Check if ubus is connected and ready.
    if (!priv->ubus_priv->js_priv) {
        return THROW_ERROR(cx, JSE_NOT_READY);
    }

    token = audio_acquire(priv->ubus_priv->ubus_object.name);
    if (token == -1) {
        return THROW_ERROR(cx, "could not acquire token");
    }

    priv->token = token;

    JS_SET_RVAL(cx, vp, INT_TO_JSVAL(token));
    return JS_TRUE;
}

JSBool JSBeepApp::AudioCanTrackBegin(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);
    int rval;

    if (priv->token == -1) {
        return THROW_ERROR(cx, JSE_NOT_READY);
    }

    rval = audio_can_track_begin(priv->token);

    JS_SET_RVAL(cx, vp, INT_TO_JSVAL(rval));
    return JS_TRUE;
}

JSBool JSBeepApp::AudioTrackBegin(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);
    // track_id, title0, title1, title2, image_url
    JSString *arg_strs[6] = {};
    char *arg_cstrs[6] = {};
    int arg_content_length;
    char *audio_file_type;
    char audio_file_code;
    bool is_null[6] = {};
    unsigned int i;
    JSBool ret = JS_TRUE;

    if (priv->token == -1) {
        return THROW_ERROR(cx, JSE_NOT_READY);
    }

    // Need to catch if any args are null since JS_ConvertArguments will
    // convert into a JSString "null".
    for (i = 0; i < argc; i++) {
        if (JSVAL_IS_NULL(JS_ARGV(cx, vp)[i]))
            is_null[i] = true;
    }
    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "/SSSSSSi",
            &arg_strs[0], &arg_strs[1], &arg_strs[2], &arg_strs[3],
            &arg_strs[4], &arg_strs[5], &arg_content_length)) {
        return JS_FALSE;
    }

    for (i = 0; i < sizeof(arg_strs) / sizeof(JSString *); i++) {
        if (arg_strs[i] && !is_null[i]) {
            arg_cstrs[i] = JS_EncodeString(cx, arg_strs[i]);
            if (!arg_cstrs[i]) {
                ret = JS_FALSE;
                THROW_ERROR(cx, JSE_BAD_ARGS);
                break;
            }
        }
    }

    if (ret) {
        audio_file_type = arg_cstrs[5];
        if (!audio_file_type) {
            audio_file_code = 'm';
        } else if (!strcmp(audio_file_type, "aac")) {
            audio_file_code = 'a';
        } else {
            audio_file_code = 'm';
        }
        bool rval = audio_track_begin(priv->token, arg_cstrs[0], arg_cstrs[1],
                arg_cstrs[2], arg_cstrs[3], arg_cstrs[4], audio_file_code,
                arg_content_length);
        JS_SET_RVAL(cx, vp, BOOLEAN_TO_JSVAL((rval) ? JS_TRUE : JS_FALSE));
    }

    for (i = 0; i < sizeof(arg_cstrs) / sizeof(char *); i++) {
        if (arg_cstrs[i])
            JS_free(cx, arg_cstrs[i]);
    }

    return ret;
}

JSBool JSBeepApp::AudioTrackEnd(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);
    bool rval;

    if (priv->token == -1) {
        return THROW_ERROR(cx, JSE_NOT_READY);
    }

    rval = audio_track_end(priv->token);

    JS_SET_RVAL(cx, vp, BOOLEAN_TO_JSVAL((rval) ? JS_TRUE : JS_FALSE));
    return JS_TRUE;
}

JSBool JSBeepApp::AudioCanBuffer(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);
    uint32_t length;
    int rval;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "u", &length)) {
        return JS_FALSE;
    }

    if (priv->token == -1) {
        return THROW_ERROR(cx, JSE_NOT_READY);
    }

    rval = audio_can_buffer(priv->token, length);

    JS_SET_RVAL(cx, vp, INT_TO_JSVAL(rval));
    return JS_TRUE;
}

JSBool JSBeepApp::AudioBuffer(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);
    JSObject *buf_obj;
    uint32_t length;
    uv_buf_t buf;
    bool rval;

    if (priv->token == -1) {
        return THROW_ERROR(cx, JSE_NOT_READY);
    }

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "ou", &buf_obj,
            &length)) {
        return JS_FALSE;
    }
    if (!JSBuffer::IsBuffer(cx, buf_obj)) {
        return THROW_ERROR(cx, JSE_EXP_FOR_ARG, JSE_T_BUFFER, 1);
    }
    if (!JSBuffer::GetBuffer(buf_obj, &buf)) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }
    if (length > buf.len) {
        return THROW_ERROR(cx, JSE_LENGTH_OOR);
    }

    rval = audio_buffer(priv->token, (uint8_t *)buf.base, length);

    JS_SET_RVAL(cx, vp, BOOLEAN_TO_JSVAL((rval) ? JS_TRUE : JS_FALSE));
    return JS_TRUE;
}

JSBool JSBeepApp::AudioPause(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);
    bool rval;

    if (priv->token == -1) {
        return THROW_ERROR(cx, JSE_NOT_READY);
    }

    rval = audio_pause();

    JS_SET_RVAL(cx, vp, BOOLEAN_TO_JSVAL((rval) ? JS_TRUE : JS_FALSE));
    return JS_TRUE;
}

JSBool JSBeepApp::AudioStart(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);
    bool rval;

    if (priv->token == -1) {
        return THROW_ERROR(cx, JSE_NOT_READY);
    }

    rval = audio_resume();

    JS_SET_RVAL(cx, vp, BOOLEAN_TO_JSVAL((rval) ? JS_TRUE : JS_FALSE));
    return JS_TRUE;
}

JSBool JSBeepApp::SetVolume(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);

    uint32_t volume;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "u", &volume)) {
        return JS_FALSE;
    }

    set_master_volume(volume);

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSBeepApp::SetTrackVolumeScalar(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);

    uint32_t volume;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "u", &volume)) {
        return JS_FALSE;
    }

    set_track_volume_scalar(volume);

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSBeepApp::SetStation(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);
    // station_id, station_name, station_image_url (optional),
    // play_station_method, play_station_args
    JSString *arg_strs[5] = {};
    char *arg_cstrs[5] = {};
    unsigned int i;
    JSBool ret = JS_TRUE;

    if (priv->token == -1) {
        return THROW_ERROR(cx, JSE_NOT_READY);
    }

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "SS*SS",
            &arg_strs[0], &arg_strs[1], &arg_strs[3], &arg_strs[4])) {
        return JS_FALSE;
    }

    if (!JSVAL_IS_NULL(JS_ARGV(cx, vp)[1])) {
        if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "**S",
                &arg_strs[2])) {
            return JS_FALSE;
        }
    }

    for (i = 0; i < sizeof(arg_strs) / sizeof(JSString *); i++) {
        if (arg_strs[i]) {
            arg_cstrs[i] = JS_EncodeString(cx, arg_strs[i]);
            if (!arg_cstrs[i]) {
                ret = JS_FALSE;
                THROW_ERROR(cx, JSE_BAD_ARGS);
                break;
            }
        }
    }

    if (ret) {
        struct blob_buf b;
        memset(&b, 0, sizeof(struct blob_buf));
        if (blob_buf_init(&b, BLOBMSG_TYPE_UNSPEC)) {
            return THROW_ERROR(cx, JSE_OOM);
        }
        if (blobmsg_add_json_from_string(&b, arg_cstrs[4])) {
            bool rval = set_station(priv->token, arg_cstrs[0], arg_cstrs[1],
                    arg_cstrs[2], arg_cstrs[3], b.head);
            JS_SET_RVAL(cx, vp, BOOLEAN_TO_JSVAL((rval) ? JS_TRUE : JS_FALSE));
        } else {
            ret = JS_FALSE;
            THROW_ERROR(cx, "bad json string");
        }
        blob_buf_free(&b);
    }

    for (i = 0; i < sizeof(arg_cstrs) / sizeof(char *); i++) {
        if (arg_cstrs[i])
            JS_free(cx, arg_cstrs[i]);
    }

    return ret;
}

JSBool JSBeepApp::MsgSockSendMsg(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);
    // sender_id, msg_namespace, message
    JSString *arg_strs[3] = {};
    char *arg_cstrs[3] = {};
    JSBool ret = JS_TRUE;
    unsigned int i;
    bool rval;

    if (!app_thread_started)
        return THROW_ERROR(cx, JSE_NOT_READY);

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "SSS",
            &arg_strs[0], &arg_strs[1], &arg_strs[2])) {
        return JS_FALSE;
    }

    for (i = 0; i < sizeof(arg_strs) / sizeof(JSString *); i++) {
        if (arg_strs[i]) {
            arg_cstrs[i] = JS_EncodeString(cx, arg_strs[i]);
            if (!arg_cstrs[i]) {
                ret = JS_FALSE;
                THROW_ERROR(cx, JSE_BAD_ARGS);
                break;
            }
        }
    }

    if (ret) {
        // strip "beep.app."
        char* beep_app_pos = strstr(priv->app_name, "beep.app.");
        if (!beep_app_pos) {
            LOG_ERROR(log_beep_main, "app_name doesn't begin with beep.app.");
            THROW_ERROR(cx, JSE_BAD_ARGS);
            return JS_FALSE;
        }

        rval = msg_socket_send_message(priv->app_name + 9, arg_cstrs[0],
                arg_cstrs[1], arg_cstrs[2]);
        JS_SET_RVAL(cx, vp, BOOLEAN_TO_JSVAL((rval) ? JS_TRUE : JS_FALSE));
    }

    for (i = 0; i < sizeof(arg_cstrs) / sizeof(char *); i++) {
        if (arg_cstrs[i])
            JS_free(cx, arg_cstrs[i]);
    }

    return ret;
}

JSBool JSBeepApp::MsgSockSockClose(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);
    JSString *sender_id_str;
    char *sender_id_cstr;
    bool rval;

    if (!app_thread_started)
        return THROW_ERROR(cx, JSE_NOT_READY);

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "S",
            &sender_id_str)) {
        return JS_FALSE;
    }

    sender_id_cstr = JS_EncodeString(cx, sender_id_str);
    if (!sender_id_cstr) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }

    rval = msg_socket_close(priv->app_name, sender_id_cstr);
    JS_SET_RVAL(cx, vp, BOOLEAN_TO_JSVAL((rval) ? JS_TRUE : JS_FALSE));

    JS_free(cx, sender_id_cstr);

    return JS_TRUE;
}

JSBool JSBeepApp::BeepSendState(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSBeepApp);
    // event_type, event_data, state
    JSString *arg_strs[3] = {};
    size_t arg_lens[3] = {};
    size_t total_len = 0;
    char *arg_cstrs[3] = {};
    char *ptr;
    int i;
    ssize_t w_bytes;
    UbusJSMsg uj_msg;

    if (!app_thread_started)
        return THROW_ERROR(cx, JSE_NOT_READY);

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "SSS",
            &arg_strs[0], &arg_strs[1], &arg_strs[2])) {
        return JS_FALSE;
    }

    total_len = strlen(priv->app_name) + 1;
    for (i = 0; i < 3; i++) {
        arg_lens[i] = JS_GetStringEncodingLength(cx, arg_strs[i]);
        if (arg_lens[i] == (size_t)-1)
            return THROW_ERROR(cx, JSE_BAD_ARGS);
        total_len += (arg_lens[i] + 1);
    }

    // Using one malloc and free which will be to the component pointer.
    ptr = (char *)malloc(total_len);
    if (!ptr)
        return THROW_ERROR(cx, JSE_OOM);

    strcpy(ptr, priv->app_name);
    uj_msg.data.beep_state.component = ptr;
    ptr += (strlen(ptr) + 1);

    for (i = 0; i < 3; i++) {
        arg_cstrs[i] = ptr;
        JS_EncodeStringToBuffer(arg_strs[i], ptr, arg_lens[i]);
        ptr[arg_lens[i]] = '\0';
        ptr += (arg_lens[i] + 1);
    }

    uj_msg.type = UBUS_JS_BEEP_STATE;
    uj_msg.data.beep_state.ctx = ubus_ctx;
    uj_msg.data.beep_state.event_type = arg_cstrs[0];
    uj_msg.data.beep_state.event_data = arg_cstrs[1];
    uj_msg.data.beep_state.state = arg_cstrs[2];

    DPRINTF("js sending uj_msg beep state:\n");
    DPRINTF("  uj_msg.type: %d\n", uj_msg.type);
    DPRINTF("  uj_msg.ctx: %p\n", uj_msg.data.beep_state.ctx);
    DPRINTF("  uj_msg.component: %s\n", uj_msg.data.beep_state.component);
    DPRINTF("  uj_msg.event_type: %s\n", uj_msg.data.beep_state.event_type);
    DPRINTF("  uj_msg.event_data: %s\n", uj_msg.data.beep_state.event_data);
    DPRINTF("  uj_msg.state: %s\n", uj_msg.data.beep_state.state);

    w_bytes = write(priv->js_ubus_sock, &uj_msg, sizeof(UbusJSMsg));
    assert(w_bytes == sizeof(UbusJSMsg));

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBeepApp::UbusJSEvCallback *JSBeepApp::FindEvCallback(Priv *priv,
        const char *pattern) {
    for (std::list<UbusJSEvCallback *>::iterator it =
            priv->ev_cb_list->begin(); it != priv->ev_cb_list->end(); it++) {
        if (!strcmp((*it)->pattern, pattern)) {
            return *it;
        }
    }
    return NULL;
}

void JSBeepApp::UbusServCallback(struct uloop_fd *fd, unsigned int events) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv;
    ssize_t r_bytes;

    if (!app_thread_started)
        pthread_mutex_lock(&app_start_mutex);

    int ubus_js_sock = accept(fd->fd, NULL, NULL);
    assert(ubus_js_sock > -1);

    r_bytes = read(ubus_js_sock, &priv, sizeof(Priv *));
    assert(r_bytes == sizeof(Priv *));
    DPRINTF("found js priv: %p ubus priv: %p\n", priv, priv->ubus_priv);

    beep_ubus_debug_start(NULL, 0, priv->ubus_priv->app_name);
    ubus_add_object_async(ubus_ctx, &priv->ubus_priv->ubus_object, NULL, NULL);
    // Setup the ubus read callback.
    priv->ubus_priv->ubus_js_sock = ubus_js_sock;
    priv->ubus_priv->uloop_fd_read.fd = ubus_js_sock;
    priv->ubus_priv->uloop_fd_read.cb = UbusPollCallback;
    uloop_fd_add(&priv->ubus_priv->uloop_fd_read, ULOOP_READ);
    // Indicate ubus is now connected.
    priv->ubus_priv->js_priv = priv;

    if (!app_thread_started) {
        pthread_cond_signal(&app_start_cond);
        pthread_mutex_unlock(&app_start_mutex);
    }
}

// Order of operations:
//   ubus method:
//     (ubus thread) msg from ubus -> UbusRecv -> defer req -> write out UbusJSMsg
//     (js thread) UVPollCallback -> read in UbusJSMsg
//     (js thread) call into js engine
//     (js thread) UVPollCallback -> write out UbusJSMsg
//     (ubus thread) UbusPollCallback -> read in UbusJSMsg -> complete defer req
//   beep send state:
//     (js thread) call from js engine to BeepSendState
//     (js thread) write out UbusJSMsg
//     (ubus thread) UbusPollCallback -> read in UbusJSMsg -> call beep_send_state
//   event register:
//     (js thread) call from js engine to Subscribe
//     (js thread) write out UbusJSMsg
//     (ubus thread) UbusPollCallback -> read in UbusJSMsg ->
//       call ubus_register_event_handler
//   event:
//     (ubus thread) event from ubus -> UbusEvHandler -> write out UbusJSMsg
//     (js thread) UVPollCallback -> read in UbusJSMsg
//     (js thread) call into js engine if ev_name callback is found.
// Note: All json messages are sent as flat string pointers though UbusJSMsg
// and converted to a blob_buf in the ubus thread.  This makes it easier to
// dump messages as they are being passed around.
void JSBeepApp::UVPollCallback(uv_poll_t *handle, int status, int events) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_MEMBER(JSBeepApp, handle, p_handle);
    DPRINTF("found ubus priv: %p\n", priv);
    DPRINTF("status %d events %d\n", status, events);
    ssize_t rw_bytes;
    UbusJSMsg uj_msg;

    rw_bytes = read(priv->js_ubus_sock, &uj_msg, sizeof(UbusJSMsg));
    assert(rw_bytes == sizeof(UbusJSMsg));

    DPRINTF("js recving uj_msg:\n");
    DPRINTF("  uj_msg.type: %d\n", uj_msg.type);

    switch (uj_msg.type) {

    case UBUS_JS_METHOD: {
        bool is_shutdown = false;
        JSString *method_str;
        JSString *req_str;
        jsval argv[2];
        jsval rval;
        JSString *resp_str;
        size_t resp_len;

        // It is much easier to just intercept any shutdown method given the
        // request/response architecture.  If a js user wishes to never shutdown
        // they can just override the stopApp method.  Otherwise this will call
        // the native stopApp method after sending a response.
        if (!strcmp("shutdown", uj_msg.data.method.method))
            is_shutdown = true;

        DPRINTF("  uj_msg.ctx: %p\n", uj_msg.data.method.ctx);
        DPRINTF("  uj_msg.req: %p\n", uj_msg.data.method.req);
        DPRINTF("  uj_msg.method: %s\n", uj_msg.data.method.method);
        DPRINTF("  uj_msg.msg: %s\n", uj_msg.data.method.msg);
        DPRINTF("  uj_msg.ret: %d\n", uj_msg.data.method.ret);

        method_str = JS_NewStringCopyZ(priv->cx, uj_msg.data.method.method);
        req_str = JS_NewStringCopyZ(priv->cx, uj_msg.data.method.msg);
        free(uj_msg.data.method.msg);  // UbusRecv (blobmsg_format_json)
        uj_msg.data.method.msg = NULL;

        argv[0] = STRING_TO_JSVAL(method_str);
        argv[1] = STRING_TO_JSVAL(req_str);
        if (JS::Call(priv->cx, priv->jsthis, "_onUbusMethod", 2, argv, &rval)) {
            assert(JSVAL_IS_STRING(rval));
            resp_str = JSVAL_TO_STRING(rval);
            resp_len = JS_GetStringEncodingLength(priv->cx, resp_str);
            assert(resp_len != (size_t)-1);
            uj_msg.data.method.msg = (char *)malloc(resp_len + 1);
            assert(uj_msg.data.method.msg);
            JS_EncodeStringToBuffer(resp_str, uj_msg.data.method.msg, resp_len);
            uj_msg.data.method.msg[resp_len] = '\0';
        } else {
            // Uncaught error occurred in the js engine.  Unfortunately it can't be
            // trapped here before it moves to the error reporter so just report
            // unknown error back to ubus, the backtrace will still show up on
            // stderr for beepjs.  If a js user wants to make a custom error it
            // can with JSON.stringify.
            uj_msg.data.method.msg = strdup(JSONSTR_UNKNOWN_JS_ERROR);
        }

        DPRINTF("js sending uj_msg method:\n");
        DPRINTF("  uj_msg.type: %d\n", uj_msg.type);
        DPRINTF("  uj_msg.ctx: %p\n", uj_msg.data.method.ctx);
        DPRINTF("  uj_msg.req: %p\n", uj_msg.data.method.req);
        DPRINTF("  uj_msg.method: %s\n", uj_msg.data.method.method);
        DPRINTF("  uj_msg.msg: %s\n", uj_msg.data.method.msg);
        DPRINTF("  uj_msg.ret: %d\n", uj_msg.data.method.ret);

        rw_bytes = write(priv->js_ubus_sock, &uj_msg, sizeof(UbusJSMsg));
        assert(rw_bytes == sizeof(UbusJSMsg));

        if (is_shutdown)
            JS::Call(priv->cx, priv->jsthis, "stopApp", 0, NULL, &rval);

        break;
    }

    case UBUS_JS_EV_EVENT: {
        UbusJSEvCallback *ev_cb = FindEvCallback(priv,
                uj_msg.data.ev_event.ev_name);
        JSString *ev_name_str;
        JSString *ev_msg_str;
        jsval argv[2];
        jsval rval;

        DPRINTF("  uj_msg.ctx: %p\n", uj_msg.data.ev_event.ctx);
        DPRINTF("  uj_name.ev_name: %s\n", uj_msg.data.ev_event.ev_name);
        DPRINTF("  uj_msg.ev_msg: %s\n", uj_msg.data.ev_event.ev_msg);

        if (!ev_cb) {
            DPRINTF("callback for event %s not found\n",
                    uj_msg.data.ev_event.ev_name);
            return;
        }

        ev_name_str = JS_NewStringCopyZ(priv->cx,
                uj_msg.data.ev_event.ev_name);
        ev_msg_str = JS_NewStringCopyZ(priv->cx, uj_msg.data.ev_event.ev_msg);

        argv[0] = STRING_TO_JSVAL(ev_name_str);
        argv[1] = STRING_TO_JSVAL(ev_msg_str);

        if (!JS::Call(priv->cx, priv->jsthis, ev_cb->js_cb_func, 2, argv, &rval)) {
            DPRINTF("Error calling into event cb function for %s\n",
                    ev_cb->pattern);
        }

        free(uj_msg.data.ev_event.ev_name); // UbusEvHandler
        free(uj_msg.data.ev_event.ev_msg);  // UbusEvHandler (blobmsg_format_json)
        break;
    }

    default:
        DPRINTF("invalid uj_msg.type: %d\n", uj_msg.type);
        abort();
    }
}

void JSBeepApp::UbusPollCallback(struct uloop_fd *fd, unsigned int events) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    UbusPriv *ubus_priv = UBUS_PRIV_FROM_MEMBER(JSBeepApp, fd, uloop_fd_read);
    DPRINTF("found ubus priv: %p\n", ubus_priv);
    ssize_t r_bytes;
    UbusJSMsg uj_msg;

    // If StopApp closes the other side of this connection libubox will get
    // EPOLLHUP or EPOLLERR and call uloop_fd_delete.  This function will get
    // called one last time so we can close the socket.
    // There could be a rooting problem if the socket is closed on this side
    // and StopApp never gets called.
    if (fd->eof || fd->error) {
        close(ubus_priv->ubus_js_sock);
        ubus_remove_object_async(ubus_ctx, &ubus_priv->ubus_object, NULL, NULL);
        // StopApp should have 'disconnected' the js priv and ubus_priv
        // by setting priv->ubus_priv and ubus_priv->js_priv to NULL.  This
        // indicates a normal shutdown and that the ubus thread is responsible
        // for freeing this memory.  This ensures that the memory needed by
        // each thread is freed by only that thread.
        if (!ubus_priv->js_priv) {
            if (ubus_priv->app_name)
                free(ubus_priv->app_name);
            free(ubus_priv);
        }

        // We typically only have ever have a single app running per beepjs
        // instance.  At this point we're about to shutdown the entire program
        // so stop the debug instance as well so the ubus thread can end.
        // Alternatively we could attach a reference counter to number of
        // active ubus objects and only stop the debug instance if we get back
        // to 0.  This wouldn't work if we wanted to do that but keep beepjs
        // open but by then the ubus thread will have ended so we would have
        // to keep it alive by other means or start another one.
        beep_ubus_debug_stop();
        return;
    }

    r_bytes = read(ubus_priv->ubus_js_sock, &uj_msg, sizeof(UbusJSMsg));
    assert(r_bytes == sizeof(UbusJSMsg));

    DPRINTF("ubus recving uj_msg:\n");
    DPRINTF("  uj_msg.type: %d\n", uj_msg.type);

    switch (uj_msg.type) {

    case UBUS_JS_METHOD: {
        blob_buf b;

        DPRINTF("  uj_msg.ctx: %p\n", uj_msg.data.method.ctx);
        DPRINTF("  uj_msg.req: %p\n", uj_msg.data.method.req);
        DPRINTF("  uj_msg.method: %s\n", uj_msg.data.method.method);
        DPRINTF("  uj_msg.msg: %s\n", uj_msg.data.method.msg);
        DPRINTF("  uj_msg.ret: %d\n", uj_msg.data.method.ret);

        memset(&b, 0, sizeof(struct blob_buf));
        blob_buf_init(&b, BLOBMSG_TYPE_UNSPEC);
        blobmsg_add_json_from_string(&b, uj_msg.data.method.msg);

        ubus_send_reply(uj_msg.data.method.ctx, uj_msg.data.method.req,
                b.head);
        ubus_complete_deferred_request(uj_msg.data.method.ctx,
                uj_msg.data.method.req, uj_msg.data.method.ret);

        free(uj_msg.data.method.req);  // UbusRecv
        free(uj_msg.data.method.method);  // UbusRecv
        free(uj_msg.data.method.msg);  // UVPollCallback
        blob_buf_free(&b);  // UbusPollCallback
        break;
    }

    case UBUS_JS_BEEP_STATE: {
        blob_buf event_data;
        blob_buf state;

        DPRINTF("  uj_msg.ctx: %p\n", uj_msg.data.beep_state.ctx);
        DPRINTF("  uj_msg.component: %s\n", uj_msg.data.beep_state.component);
        DPRINTF("  uj_msg.event_type: %s\n",
                uj_msg.data.beep_state.event_type);
        DPRINTF("  uj_msg.event_data: %s\n",
                uj_msg.data.beep_state.event_data);
        DPRINTF("  uj_msg.state: %s\n", uj_msg.data.beep_state.state);

        memset(&event_data, 0, sizeof(struct blob_buf));
        memset(&state, 0, sizeof(struct blob_buf));
        blob_buf_init(&event_data, BLOBMSG_TYPE_UNSPEC);
        blob_buf_init(&state, BLOBMSG_TYPE_UNSPEC);
        blobmsg_add_json_from_string(&event_data,
                uj_msg.data.beep_state.event_data);
        blobmsg_add_json_from_string(&state,
                uj_msg.data.beep_state.state);

        beep_send_state(uj_msg.data.beep_state.ctx,
                uj_msg.data.beep_state.component,
                uj_msg.data.beep_state.event_type,
                event_data.head,
                state.head);

        free(uj_msg.data.beep_state.component);  // BeepSendState
        blob_buf_free(&event_data);  // UbusPollCallback
        blob_buf_free(&state);  // UbusPollCallback
        break;
    }

    case UBUS_JS_EV_REGISTER: {
        DPRINTF("  uj_msg.ctx: %p\n", uj_msg.data.ev_register.ctx);
        DPRINTF("  uj_msg.pattern: %s\n", uj_msg.data.ev_register.pattern);

        if (!ubus_priv->ev_hdlr.cb) {
            ubus_priv->ev_hdlr.cb = UbusEvHandler;
        }

        ubus_register_event_handler_async(uj_msg.data.ev_register.ctx,
                &ubus_priv->ev_hdlr, uj_msg.data.ev_register.pattern, NULL, NULL);

        free(uj_msg.data.ev_register.pattern);  // Subscribe
        break;
    }

    default:
        DPRINTF("invalid uj_msg.type: %d\n", uj_msg.type);
        abort();
    }
}

int JSBeepApp::UbusRecv(struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    UbusPriv *ubus_priv = UBUS_PRIV_FROM_MEMBER(JSBeepApp, obj, ubus_object);
    DPRINTF("found ubus priv: %p\n", ubus_priv);
    ssize_t w_bytes;
    UbusJSMsg uj_msg;

    uj_msg.type = UBUS_JS_METHOD;
    uj_msg.data.method.ctx = ctx;
    uj_msg.data.method.req = (struct ubus_request_data *)
            malloc(sizeof(struct ubus_request_data));
    uj_msg.data.method.method = strdup(method);
    uj_msg.data.method.msg = blobmsg_format_json(msg, true);
    uj_msg.data.method.ret = UBUS_STATUS_OK;
    ubus_defer_request(ctx, req, uj_msg.data.method.req);

    DPRINTF("ubus sending uj_msg method:\n");
    DPRINTF("  uj_msg.type: %d\n", uj_msg.type);
    DPRINTF("  uj_msg.ctx: %p\n", uj_msg.data.method.ctx);
    DPRINTF("  uj_msg.req: %p\n", uj_msg.data.method.req);
    DPRINTF("  uj_msg.method: %s\n", uj_msg.data.method.method);
    DPRINTF("  uj_msg.msg: %s\n", uj_msg.data.method.msg);
    DPRINTF("  uj_msg.ret: %d\n", uj_msg.data.method.ret);

    w_bytes = write(ubus_priv->ubus_js_sock, &uj_msg, sizeof(UbusJSMsg));
    assert(w_bytes == sizeof(UbusJSMsg));

    return UBUS_STATUS_OK;
}

void JSBeepApp::UbusEvHandler(struct ubus_context *ctx,
        struct ubus_event_handler *ev, const char *type,
        struct blob_attr *msg) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    UbusPriv *ubus_priv = UBUS_PRIV_FROM_MEMBER(JSBeepApp, ev, ev_hdlr);
    ssize_t w_bytes;
    UbusJSMsg uj_msg;

    uj_msg.type = UBUS_JS_EV_EVENT;
    uj_msg.data.ev_event.ctx = ctx;
    uj_msg.data.ev_event.ev_name = strdup(type);
    uj_msg.data.ev_event.ev_msg = blobmsg_format_json(msg, true);

    DPRINTF("ubus sending uj_msg ev_event:\n");
    DPRINTF("  uj_msg.type: %d\n", uj_msg.type);
    DPRINTF("  uj_msg.ctx: %p\n", uj_msg.data.ev_event.ctx);
    DPRINTF("  uj_name.ev_name: %s\n", uj_msg.data.ev_event.ev_name);
    DPRINTF("  uj_msg.ev_msg: %s\n", uj_msg.data.ev_event.ev_msg);

    w_bytes = write(ubus_priv->ubus_js_sock, &uj_msg, sizeof(UbusJSMsg));
    DPRINTF("w_bytes: %zd errno: %d %s\n", w_bytes, errno, strerror(errno));
    DPRINTF("ubus_priv: %p\n", ubus_priv);
    assert(w_bytes == sizeof(UbusJSMsg));
}

static const struct blobmsg_policy message_policy[] = {
    {"type", BLOBMSG_TYPE_STRING},
    {"message", BLOBMSG_TYPE_TABLE}
};

static const struct blobmsg_policy play_preset_policy[] = {
    {"preset_info", BLOBMSG_TYPE_TABLE}
};


static const struct blobmsg_policy play_station_policy[] = {
    {"id", BLOBMSG_TYPE_STRING}
};

static const struct blobmsg_policy track_started_policy[] = {
    {"track_id", BLOBMSG_TYPE_STRING}
};

static const struct blobmsg_policy msg_socket_sender_connected_policy[] = {
    {"sender_id", BLOBMSG_TYPE_STRING},
    {"user_agent", BLOBMSG_TYPE_STRING}
};

static const struct blobmsg_policy msg_socket_sender_disconnected_policy[] = {
    {"sender_id", BLOBMSG_TYPE_STRING}
};

static const struct blobmsg_policy msg_socket_message_received_policy[] = {
    {"sender_id", BLOBMSG_TYPE_STRING},
    {"namespace", BLOBMSG_TYPE_STRING},
    {"message", BLOBMSG_TYPE_STRING}
};


#define CXX_UBUS_METHOD(__NAME__, __HANDLER__, __POLICY__) \
    {__NAME__, __HANDLER__, __POLICY__, ARRAY_SIZE(__POLICY__)}

#define CXX_UBUS_METHOD_NOARG(__NAME__, __HANDLER__) \
    {__NAME__, __HANDLER__}

static const struct ubus_method app_ubus_methods[] = {
    CXX_UBUS_METHOD("message", JSBeepApp::UbusRecv, message_policy),
    CXX_UBUS_METHOD("track_started", JSBeepApp::UbusRecv, track_started_policy),
    CXX_UBUS_METHOD("play_preset", JSBeepApp::UbusRecv, play_preset_policy),
    CXX_UBUS_METHOD_NOARG("skip", JSBeepApp::UbusRecv),
    CXX_UBUS_METHOD_NOARG("resume", JSBeepApp::UbusRecv),
    CXX_UBUS_METHOD_NOARG("pause", JSBeepApp::UbusRecv),
    CXX_UBUS_METHOD_NOARG("get_state", JSBeepApp::UbusRecv),
    CXX_UBUS_METHOD_NOARG("audio_token_revoked", JSBeepApp::UbusRecv),
    CXX_UBUS_METHOD_NOARG("track_begin_ready", JSBeepApp::UbusRecv),
    CXX_UBUS_METHOD_NOARG("audio_ended", JSBeepApp::UbusRecv),
    CXX_UBUS_METHOD_NOARG("shutdown", JSBeepApp::UbusRecv),
    CXX_UBUS_METHOD("msg_socket_sender_connected", JSBeepApp::UbusRecv,
            msg_socket_sender_connected_policy),
    CXX_UBUS_METHOD("msg_socket_sender_disconnected", JSBeepApp::UbusRecv,
            msg_socket_sender_disconnected_policy),
    CXX_UBUS_METHOD("msg_socket_message_received", JSBeepApp::UbusRecv,
            msg_socket_message_received_policy)
};

JSBool JSBeepApp::InitUbusPriv(Priv *priv) {
    // The ubus_priv needs it's own copy of the app_name as it may not
    // be destroyed at the same time as the JS object.
    priv->ubus_priv->app_name = strdup(priv->app_name);
    if (!priv->ubus_priv->app_name) {
        return THROW_ERROR(priv->cx, JSE_OOM);
    }

    priv->ubus_priv->type.name = priv->ubus_priv->app_name;
    priv->ubus_priv->type.id = 0;
    priv->ubus_priv->type.methods = app_ubus_methods;
    priv->ubus_priv->type.n_methods =
            ARRAY_SIZE(app_ubus_methods);

    priv->ubus_priv->ubus_object.name = priv->ubus_priv->app_name;
    priv->ubus_priv->ubus_object.type = &priv->ubus_priv->type;
    priv->ubus_priv->ubus_object.methods = app_ubus_methods;
    priv->ubus_priv->ubus_object.n_methods =
            ARRAY_SIZE(app_ubus_methods);

    return JS_TRUE;
}

JSBool JSBeepApp::StartAppThread(void) {
    unlink(SOCK_FILE);
    uloop_fd_app.fd = usock(USOCK_SERVER | USOCK_UNIX, SOCK_FILE, NULL);
    uloop_fd_app.cb = UbusServCallback;
    if (uloop_fd_app.fd == -1) {
        return JS_FALSE;
    }
    pthread_create(&app_thread, NULL, AppThread, NULL);
    pthread_detach(app_thread);
    return JS_TRUE;
}

extern "C" int ubus_complete_request_allowed;

void *JSBeepApp::AppThread(void *arg) {
    DPRINTF("calling app_init for beepjs\n");
    ubus_complete_request_allowed = false;
    ubus_ctx = app_init("beepjs", NULL);
    uloop_fd_add(&uloop_fd_app, ULOOP_READ);
    DPRINTF("calling app_start\n");
    app_start();
    DPRINTF("back from app_start\n");
    return NULL;
}

JSBool JSBeep::Log(JSContext *cx, unsigned argc, jsval *vp) {
    uint32_t pri;
    LogPriority log_pri;
    jsval *argv = JS_ARGV(cx, vp);
    char *fullstr;

    if (argc < 2) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "u", &pri)) {
        return JS_FALSE;
    }

    switch (pri) {
        case LOG_PRIORITY_TEST: log_pri = LOG_PRIORITY_TEST; break;
        case LOG_PRIORITY_ERROR: log_pri = LOG_PRIORITY_ERROR; break;
        case LOG_PRIORITY_WARN: log_pri = LOG_PRIORITY_WARN; break;
        case LOG_PRIORITY_INFO: log_pri = LOG_PRIORITY_INFO; break;
        case LOG_PRIORITY_DEBUG: log_pri = LOG_PRIORITY_DEBUG; break;
        case LOG_PRIORITY_TRACE: log_pri = LOG_PRIORITY_TRACE; break;
        default: log_pri = LOG_PRIORITY_DEBUG; break;
    }

    fullstr = JSUtils::ArgvToCStr(cx, argc - 1, &argv[1]);
    if (!fullstr) {
        return JS_FALSE;
    }

    lprintf(log_beepjs_main, log_pri, NULL, 0, "%s", fullstr);
    free(fullstr);

    return JS_TRUE;
}

static void fini_beep_module(JSContext *cx) {
    unlink(SOCK_FILE);
    beepjs_delete_native_space(cx, "beep");
    if (JSBeepApp::Proto)
        JS_RemoveObjectRoot(cx, &JSBeepApp::Proto);
}

static int init_beep_module(JSContext *cx) {
    JS::RootedObject mod_space_obj(cx,
            beepjs_create_native_space(cx, "beep"));
    JSBeepApp::Proto = JS_InitClass(cx, mod_space_obj, NULL, &JSBeepApp::Class,
            JSBeepApp::Ctor, 0, JSBeepApp::Props, JSBeepApp::Funcs, NULL,
            NULL);
    BeepJSRuntime *brt = beepjs_get_brt(cx);
    int i;
    int beep_argc = 1;
    char *beep_argv[5] = {};

    if (!JSBeepApp::Proto)
        return 0;
    if (!JS_DefineFunctions(cx, mod_space_obj, JSBeep::Funcs))
        return 0;
    if (!JS_DefineConstDoubles(cx, mod_space_obj, JSBeep::LogLevels))
        return 0;

    JS_AddObjectRoot(cx, &JSBeepApp::Proto);

    log_beep_main = LOG_CATEGORY_GET("beepjs");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    JSBeep::log_beepjs_main = LOG_CATEGORY_GET((brt->opts->js_argc > 1) ?
            brt->opts->js_argv[1] : "beepjs");
    log_category_set_priority(JSBeep::log_beepjs_main, LOG_PRIORITY_DEBUG);

    // Need to only provide certain args to beep_flags_init or it will exit.
    beep_argv[0] = brt->opts->js_argv[0];
    for (i = 1; i < brt->opts->js_argc && i < 5; i++) {
        if (!strncmp(brt->opts->js_argv[i], "--uciconfig=", 12)
                || !strncmp(brt->opts->js_argv[i], "--network_delay=", 16)
                || !strncmp(brt->opts->js_argv[i], "--ubus=", 7)) {
            beep_argv[beep_argc++] = brt->opts->js_argv[i];
        }
    }

    beep_flags_init(beep_argc, beep_argv);

    return 1;
}
}  // namespace


BEEPJS_MODULE_INIT(init_beep_module, 0);
BEEPJS_MODULE_FINI(fini_beep_module, 0);
