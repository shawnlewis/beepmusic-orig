#include "beepjs.h"


namespace {
namespace JSTimer {

typedef struct {
    JSContext *cx;
    JSObject *jsthis;
    uv_timer_t t_handle;
    bool repeating;
} Priv;

static JSBool Ctor(JSContext *cx, unsigned argc, jsval *vp);
static void Finalize(JSFreeOp *fop, JSObject *obj);
static JSBool Start(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Stop(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Again(JSContext *cx, unsigned argc, jsval *vp);
static JSBool SetRepeat(JSContext *cx, unsigned argc, jsval *vp);
static JSBool GetRepeat(JSContext *cx, unsigned argc, jsval *vp);

static void OnTimerClose(uv_handle_t *handle);
static void OnTimeout(uv_timer_t *handle, int status);

static JSClass Class = {
    "Timer",
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

static JSFunctionSpec Funcs[] = {
    JS_FS("start", Start, 0, JSPROP_ENUMERATE),
    JS_FS("stop", Stop, 0, JSPROP_ENUMERATE),
    JS_FS("again", Again, 0, JSPROP_ENUMERATE),
    JS_FS("setRepeat", SetRepeat, 0, JSPROP_ENUMERATE),
    JS_FS("getRepeat", GetRepeat, 0, JSPROP_ENUMERATE),
    JS_FS_END
};

static JSObject *Proto;
}  // namespace JSTimer


JSBool JSTimer::Ctor(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    JSObject *timer_obj;
    Priv *priv;
    timer_obj = JS_NewObject(cx, &Class, Proto, NULL);
    int ret;
    if (!timer_obj) {
        return THROW_ERROR(cx, JSE_OOM);
    }
    priv = (Priv *)JS_malloc(cx, sizeof(Priv));
    if (!priv) {
        return THROW_ERROR(cx, JSE_OOM);
    }
    memset(priv, 0, sizeof(Priv));
    ret = uv_timer_init(uv_default_loop(), &priv->t_handle);
    if (ret != 0)
        return THROW_ERROR(cx, JSE_INTERNAL);

    priv->cx = cx;
    priv->jsthis = timer_obj;
    JS_AddObjectRoot(cx, &priv->jsthis);

    priv->t_handle.data = priv;

    DPRINTF("%s Constructor: %p %p\n", __PRETTY_FUNCTION__, priv->jsthis,
            &priv->t_handle);

    JS_SetPrivate(timer_obj, (void *)priv);
    JS_SET_RVAL(cx, vp, OBJECT_TO_JSVAL(timer_obj));
    return JS_TRUE;
}

void JSTimer::Finalize(JSFreeOp *fop, JSObject *obj) {
    Priv *priv = PRIV_FROM_FIN(JSTimer);
    DPRINTF("%s called! %p %p %p %p\n", __PRETTY_FUNCTION__, fop, obj, priv, priv ? priv->jsthis : NULL);
    if (priv) {
        JS_freeop(fop, priv);
    }
}

void JSTimer::OnTimerClose(uv_handle_t *handle) {
    Priv *priv = static_cast<Priv *>(handle->data);
    DPRINTF("%s called! %p %p\n", __PRETTY_FUNCTION__, priv->jsthis, handle);
    JS_RemoveObjectRoot(priv->cx, &priv->jsthis);
}

void JSTimer::OnTimeout(uv_timer_t *handle, int status) {
    Priv *priv = static_cast<Priv *>(handle->data);
    jsval rval;
    DPRINTF("%s called! %p %p\n", __PRETTY_FUNCTION__, priv->jsthis, handle);
    JS::Call(priv->cx, priv->jsthis, "_onTimeout", 0, NULL, &rval);
    if (!priv->repeating) {
        if (!uv_is_closing((uv_handle_t *) &priv->t_handle)) {
            uv_close((uv_handle_t *) handle, OnTimerClose);
        }
    }
}

JSBool JSTimer::Start(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv = PRIV_FROM_FUNC(JSTimer);
    DPRINTF("%s called on %p\n", __PRETTY_FUNCTION__, priv->jsthis);
    int ret;
    uint32_t timeout, repeat;
    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "uu",
            &timeout, &repeat)) {
        return JS_FALSE;
    }
    DPRINTF("%s %p params %u %u\n", __PRETTY_FUNCTION__, priv->jsthis, timeout, repeat);

    if (repeat) {
        priv->repeating = true;
    }

    ret = uv_timer_start(&priv->t_handle, OnTimeout, timeout, repeat);
    if (ret) {
        if (!uv_is_closing((uv_handle_t *) &priv->t_handle)) {
            uv_close((uv_handle_t *) &priv->t_handle, OnTimerClose);
        }
    }

    JS_SET_RVAL(cx, vp, INT_TO_JSVAL(ret));
    return JS_TRUE;
}

JSBool JSTimer::Stop(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv = PRIV_FROM_FUNC(JSTimer);
    DPRINTF("%s called on %p\n", __PRETTY_FUNCTION__, priv->jsthis);
    int ret;
    ret = uv_timer_stop(&priv->t_handle);

    if (!uv_is_closing((uv_handle_t *) &priv->t_handle)) {
        uv_close((uv_handle_t *) &priv->t_handle, OnTimerClose);
    }

    JS_SET_RVAL(cx, vp, INT_TO_JSVAL(ret));
    return JS_TRUE;
}

JSBool JSTimer::Again(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv = PRIV_FROM_FUNC(JSTimer);
    int ret;
    ret = uv_timer_again(&priv->t_handle);

    JS_SET_RVAL(cx, vp, INT_TO_JSVAL(ret));
    return JS_TRUE;
}

JSBool JSTimer::SetRepeat(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv = PRIV_FROM_FUNC(JSTimer);
    uint32_t repeat;
    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "u", &repeat)) {
        return JS_FALSE;
    }
    uv_timer_set_repeat(&priv->t_handle, repeat);
    priv->repeating = true;

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSTimer::GetRepeat(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv = PRIV_FROM_FUNC(JSTimer);
    uint64_t ret = uv_timer_again(&priv->t_handle);

    JS_SET_RVAL(cx, vp, UINT_TO_JSVAL((uint32_t)ret));
    return JS_TRUE;
}

static void fini_timer_module(JSContext *cx) {
    beepjs_delete_native_space(cx, "timer");
    if (JSTimer::Proto)
        JS_RemoveObjectRoot(cx, &JSTimer::Proto);
}

static int init_timer_module(JSContext *cx) {
    JS::RootedObject mod_space_obj(cx, beepjs_create_native_space(cx, "timer"));
    JSTimer::Proto = JS_InitClass(cx, mod_space_obj, NULL, &JSTimer::Class,
            JSTimer::Ctor, 0, NULL, JSTimer::Funcs, NULL, NULL);
    if (!JSTimer::Proto)
        return 0;
    JS_AddObjectRoot(cx, &JSTimer::Proto);

    return 1;
}
}  // namespace


BEEPJS_MODULE_INIT(init_timer_module, 0);
BEEPJS_MODULE_FINI(fini_timer_module, 0);
