#ifndef BEEPJS_H
#define BEEPJS_H

#include <uv.h>
#include <jsapi.h>
#include <jsdbgapi.h>
#include <jsfriendapi.h>
#include <jswrapper.h>

#include "beepjs_crt.h"
#include "beepjs_error.h"
#include "buffer.h"
#include "utils.h"

static inline JSObject *GetBeepJS(JSContext *cx) {
    JSObject *global = JS_GetGlobalObject(cx);
    jsval ret;
    if (!JS_GetProperty(cx, global, "beepjs", &ret))
        return NULL;
    return JSVAL_TO_OBJECT(ret);
}

template <class T>
static inline T *IntGetPrivAs(JSObject *obj, const char *type, bool null_ok,
        const char *file, int line) {
    T *ret = static_cast<T *>(JS_GetPrivate(obj));
    if (!null_ok && !ret) {
        fprintf(stderr, "Failed to get %s priv at %s:%d\n", type, file, line);
        abort();
    }
    return ret;
}

#define PRIV_FROM_OBJ(__NS__, __OBJ__) \
    IntGetPrivAs<__NS__::Priv>(__OBJ__, #__NS__, false, __FILE__, __LINE__)

#define PRIV_OR_NULL_FROM_OBJ(__NS__, __OBJ__) \
    IntGetPrivAs<__NS__::Priv>(__OBJ__, #__NS__, true, __FILE__, __LINE__)

#define PRIV_FROM_FUNC(__NS__) \
    PRIV_FROM_OBJ(__NS__, JS_THIS_OBJECT(cx, vp))

#define PRIV_FROM_FIN(__NS__) \
    PRIV_OR_NULL_FROM_OBJ(__NS__, obj)

#define PRIV_FROM_MEMBER(__NS__, __THIS_PTR__, __MEMBER__) \
    reinterpret_cast<__NS__::Priv *>((uint8_t *)__THIS_PTR__ \
    - offsetof(__NS__::Priv, __MEMBER__))

#define JSPROP_UNUSED (0)

#define ENUM_TO_CONST_DOUBLE_SPEC(__ENUM__) { \
    __ENUM__, \
    #__ENUM__, \
    JSPROP_READONLY | JSPROP_PERMANENT | JSPROP_ENUMERATE, \
    {0, 0, 0} \
    }

#define DEFINE_TO_CONST_DOUBLE_SPEC(__DEFINE__) { \
    __DEFINE__, \
    #__DEFINE__, \
    JSPROP_READONLY | JSPROP_PERMANENT | JSPROP_ENUMERATE, \
    {0, 0, 0} \
    }

extern bool en_dprintf;

#ifdef NDPRINTF
#define DPRINTF(__FMT__, ...)
#else
#define DPRINTF(__FMT__, ...) \
    if (en_dprintf) fprintf(stderr, __FMT__, ## __VA_ARGS__)
#endif

#endif  // BEEPJS_H
