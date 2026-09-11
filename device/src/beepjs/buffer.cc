#include "beepjs.h"


namespace {
namespace _JSBuffer {

typedef JSBuffer::Perm Perm;

typedef struct {
    uv_buf_t buf;
    Perm perm;
    bool own_mem;
} Priv;

static JSObject *Ctor(JSContext *cx);
static JSBool Ctor(JSContext *cx, unsigned argc, jsval *vp);
static void Finalize(JSFreeOp *fop, JSObject *obj);
static JSBool Read(JSContext *cx, unsigned argc, jsval *vp);
static JSBool ToString(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Write(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Copy(JSContext *cx, unsigned argc, jsval *vp);
static JSBool LenGetter(JSContext* cx, JSHandleObject obj,
        JSHandleId idval, JSMutableHandleValue vp);

static JSBool IsBuffer(JSContext *cx, unsigned argc, jsval *vp);
static JSBool IsEncoding(JSContext *cx, unsigned argc, jsval *vp);

static JSClass Class = {
    "Buffer",
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

static JSPropertySpec Props[] = {
    {"length", 0, (JSPROP_READONLY | JSPROP_PERMANENT | JSPROP_ENUMERATE),
            JSOP_WRAPPER(LenGetter), JSOP_NULLWRAPPER},
    {NULL, 0, 0, JSOP_NULLWRAPPER, JSOP_NULLWRAPPER}
};

static JSFunctionSpec Funcs[] = {
    JS_FS("read", Read, 0, JSPROP_ENUMERATE),
    JS_FS("toString", ToString, 0, JSPROP_ENUMERATE),
    JS_FS("write", Write, 0, JSPROP_ENUMERATE),
    JS_FS("copy", Copy, 0, JSPROP_ENUMERATE),
    JS_FS_END
};

static JSFunctionSpec StaticFuncs[] = {
    JS_FS("isBuffer", IsBuffer, 0, JSPROP_ENUMERATE),
    JS_FS("isEncoding", IsEncoding, 0, JSPROP_ENUMERATE),
    JS_FS_END
};

static JSObject *Proto;
}  // namespace JSBuffer

JSObject *_JSBuffer::Ctor(JSContext *cx) {
    //DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    JSObject *buffer_obj;
    Priv *priv;
    buffer_obj = JS_NewObject(cx, &Class, Proto, NULL);
    if (!buffer_obj) {
        return NULL;
    }
    priv = (Priv *)JS_malloc(cx, sizeof(Priv));
    if (!priv) {
        return NULL;
    }
    memset(priv, 0, sizeof(Priv));

    JS_SetPrivate(buffer_obj, (void *)priv);
    return buffer_obj;
}

JSBool _JSBuffer::Ctor(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv;
    uint32_t len = 0;
    JSString *buffer_str = NULL;
    Priv *existing_priv = NULL;
    JSObject *buffer_obj;

    if (argc < 1) {
        return THROW_ERROR(cx, JSE_REQ_MORE_ARGS, argc);
    }

    if (JSVAL_IS_INT(JS_ARGV(cx, vp)[0])) {
        JS::ToUint32(cx, JS_ARGV(cx, vp)[0], &len);
    } else if (JSVAL_IS_STRING(JS_ARGV(cx, vp)[0])) {
        buffer_str = JSVAL_TO_STRING(JS_ARGV(cx, vp)[0]);
        len = (uint32_t)JS_GetStringEncodingLength(cx, buffer_str);
    } else if (!JSVAL_IS_PRIMITIVE(JS_ARGV(cx, vp)[0])) {
        JSObject *existing_buf_obj = JSVAL_TO_OBJECT(JS_ARGV(cx, vp)[0]);
        if (!JS_InstanceOf(cx, existing_buf_obj, &Class, NULL)) {
            return THROW_ERROR(cx, JSE_BAD_ARGS);
        }
        existing_priv = PRIV_FROM_OBJ(_JSBuffer, existing_buf_obj);
        if (!(existing_priv->perm & JSBuffer::READ)) {
            return THROW_ERROR(cx, JSE_INVALID_PERM);
        }
        len = existing_priv->buf.len;
    } else {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }

    buffer_obj = Ctor(cx);
    if (!buffer_obj)
        return THROW_ERROR(cx, JSE_OOM);

    priv = PRIV_FROM_OBJ(_JSBuffer, buffer_obj);
    priv->buf.base = (char *)JS_malloc(cx, len);
    if (!priv->buf.base) {
        return THROW_ERROR(cx, JSE_OOM);
    }
    priv->buf.len = len;
    priv->perm = (Perm)(JSBuffer::READ | JSBuffer::WRITE);
    priv->own_mem = true;

    if (buffer_str) {
        JS_EncodeStringToBuffer(buffer_str, priv->buf.base, len);
    } else if (existing_priv) {
        memcpy(priv->buf.base, existing_priv->buf.base, len);
    }

    JS_SET_RVAL(cx, vp, OBJECT_TO_JSVAL(buffer_obj));
    return JS_TRUE;
}

void _JSBuffer::Finalize(JSFreeOp *fop, JSObject *obj) {
    Priv *priv = PRIV_FROM_FIN(_JSBuffer);
    if (priv) {
        if (priv->own_mem)
            JS_freeop(fop, priv->buf.base);
        JS_freeop(fop, priv);
    }
    //DPRINTF("%s called! %p %p %p\n", __PRETTY_FUNCTION__, fop, obj, priv);
}

JSBool _JSBuffer::Read(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv = PRIV_FROM_FUNC(_JSBuffer);
    uint32_t offset, size;
    uint8_t *array_ptr;

    // offset, size
    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "uu",
            &offset, &size)) {
        return JS_FALSE;
    }
    if (offset > priv->buf.len) {
        return THROW_ERROR(cx, JSE_INDEX_OOR);
    }
    if (!(priv->perm & JSBuffer::READ)) {
        return THROW_ERROR(cx, JSE_INVALID_PERM);
    }

    size = (offset + size) <= priv->buf.len ? size : priv->buf.len - offset;

    JS::RootedObject array_buffer(cx, JS_NewArrayBuffer(cx, size));
    if (!array_buffer) {
        return THROW_ERROR(cx, JSE_OOM);
    }
    array_ptr = JS_GetArrayBufferData(array_buffer, cx);
    memcpy(array_ptr, ((uint8_t *)priv->buf.base) + offset, size);

    JS_SET_RVAL(cx, vp, OBJECT_TO_JSVAL(array_buffer));
    return JS_TRUE;
}

JSBool _JSBuffer::ToString(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv = PRIV_FROM_FUNC(_JSBuffer);
    JSString *buf_str = NULL;
    JSString *enc_str;
    char *enc_cstr;
    uint32_t offset, size;

    // encoding (string), offset, size
    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "S/uu",
            &enc_str, &offset, &size)) {
        return JS_FALSE;
    }
    if (argc < 3) {
        offset = 0;
        size = priv->buf.len;
    }
    if (offset > priv->buf.len) {
        return THROW_ERROR(cx, JSE_INDEX_OOR);
    }
    if (!(priv->perm & JSBuffer::READ)) {
        return THROW_ERROR(cx, JSE_INVALID_PERM);
    }
    enc_cstr = JS_EncodeString(cx, enc_str);
    if (!enc_cstr) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }
    size = (offset + size) <= priv->buf.len ? size : priv->buf.len - offset;

    if (!strcmp(enc_cstr, "utf8")) {
        buf_str = JS_NewStringCopyN(cx, ((char *)priv->buf.base) + offset, size);
        if (buf_str)
            JS_AddStringRoot(cx, &buf_str);
    }

    JS_free(cx, enc_cstr);

    if (!buf_str)
        return THROW_ERROR(cx, JSE_OOM);

    JS_SET_RVAL(cx, vp, STRING_TO_JSVAL(buf_str));
    JS_RemoveStringRoot(cx, &buf_str);
    return JS_TRUE;
}

JSBool _JSBuffer::Write(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv = PRIV_FROM_FUNC(_JSBuffer);
    JSString *str_str, *enc_str;
    char *enc_cstr;
    uint32_t written = 0;
    uint32_t offset, size;

    // string, offset, length, encoding (string)
    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "SuuS",
            &str_str, &offset, &size, &enc_str)) {
        return JS_FALSE;
    }
    if (offset > priv->buf.len) {
        return THROW_ERROR(cx, JSE_INDEX_OOR);
    }
    if (!(priv->perm & JSBuffer::WRITE)) {
        return THROW_ERROR(cx, JSE_INVALID_PERM);
    }
    enc_cstr = JS_EncodeString(cx, enc_str);
    if (!enc_cstr) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }
    size = (offset + size) <= priv->buf.len ? size : priv->buf.len - offset;

    if (!strcmp(enc_cstr, "utf8")) {
        written = (uint32_t)JS_EncodeStringToBuffer(str_str,
                ((char *)priv->buf.base) + offset, size);
    }

    JS_free(cx, enc_cstr);

    JS_SET_RVAL(cx, vp, INT_TO_JSVAL((int32_t)written));
    return JS_TRUE;
}

JSBool _JSBuffer::Copy(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv = PRIV_FROM_FUNC(_JSBuffer);
    JSObject *target_buf_obj;
    Priv *target_priv;
    uint32_t target_start = 0;
    uint32_t source_start = 0;
    // Note: this is called source_end in the docs but it is a length not
    // an offset.
    uint32_t source_end = priv->buf.len;
    uint32_t copied = 0;

    // targetBuffer, targetStart, sourceStart, sourceEnd
    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "o/uuu",
            &target_buf_obj, &target_start, &source_start, &source_end)) {
        return JS_FALSE;
    }
    if (!JS_InstanceOf(cx, target_buf_obj, &Class, NULL)) {
        return THROW_ERROR(cx, JSE_EXP_FOR_ARG, JSE_T_BUFFER, 1);
    }
    target_priv = PRIV_FROM_OBJ(_JSBuffer, target_buf_obj);

    if (!(priv->perm & JSBuffer::READ) ||
            !(target_priv->perm & JSBuffer::WRITE)) {
        return THROW_ERROR(cx, JSE_INVALID_PERM);
    }

    if (target_start > target_priv->buf.len || source_start > priv->buf.len) {
        return THROW_ERROR(cx, JSE_INDEX_OOR);
    }
    copied = (source_start + source_end) <= priv->buf.len ? source_end :
            priv->buf.len - source_start;
    copied = (target_start + copied) <= target_priv->buf.len ? copied :
            target_priv->buf.len - target_start;

    memcpy(target_priv->buf.base + target_start,
            priv->buf.base + source_start, copied);

    JS_SET_RVAL(cx, vp, INT_TO_JSVAL((int32_t)copied));
    return JS_TRUE;
}

JSBool _JSBuffer::LenGetter(JSContext* cx, JSHandleObject obj,
        JSHandleId idval, JSMutableHandleValue vp) {
    Priv *priv = PRIV_FROM_OBJ(_JSBuffer, obj);
    if (!priv) {
        return THROW_ERROR(cx, JSE_INTERNAL);
    }
    vp.set(UINT_TO_JSVAL(priv->buf.len));
    return JS_TRUE;
}

JSBool _JSBuffer::IsBuffer(JSContext *cx, unsigned argc, jsval *vp) {
    JSBool ret = JS_FALSE;
    JSObject *obj;
    if (JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "o", &obj)) {
        ret = JS_InstanceOf(cx, obj, &Class, NULL);
    }
    JS_SET_RVAL(cx, vp, BOOLEAN_TO_JSVAL(ret));
    DPRINTF("%s called! ret: %d\n", __PRETTY_FUNCTION__, ret);
    return JS_TRUE;
}

JSBool _JSBuffer::IsEncoding(JSContext *cx, unsigned argc, jsval *vp) {
    JS_SET_RVAL(cx, vp, BOOLEAN_TO_JSVAL(JS_TRUE));
    return JS_TRUE;
}

static void fini_buffer_module(JSContext *cx) {
    beepjs_delete_native_space(cx, "buffer");
    if (_JSBuffer::Proto)
        JS_RemoveObjectRoot(cx, &_JSBuffer::Proto);
}

static int init_buffer_module(JSContext *cx) {
    JS::RootedObject mod_space_obj(cx,
            beepjs_create_native_space(cx, "buffer"));
    _JSBuffer::Proto = JS_InitClass(cx, mod_space_obj, NULL, &_JSBuffer::Class,
            _JSBuffer::Ctor, 0, _JSBuffer::Props, _JSBuffer::Funcs, NULL,
            _JSBuffer::StaticFuncs);
    if (!_JSBuffer::Proto)
        return 0;
    JS_AddObjectRoot(cx, &_JSBuffer::Proto);

    return 1;
}
}  // namespace


JSObject *JSBuffer::NewBuffer(JSContext *cx, uv_buf_t buf, Perm perm) {
    JS::RootedObject buffer_obj(cx, _JSBuffer::Ctor(cx));
    if (!buffer_obj)
        return NULL;
    if (!SetBuffer(buffer_obj, buf) || !SetPerm(buffer_obj, perm))
        return NULL;
    return buffer_obj;
}

bool JSBuffer::SetBuffer(JSObject *obj, uv_buf_t buf) {
    _JSBuffer::Priv *priv = PRIV_OR_NULL_FROM_OBJ(_JSBuffer, obj);
    if (!priv)
        return false;
    priv->buf = buf;
    return true;
}

bool JSBuffer::GetBuffer(JSObject *obj, uv_buf_t *buf) {
    _JSBuffer::Priv *priv = PRIV_OR_NULL_FROM_OBJ(_JSBuffer, obj);
    if (!priv)
        return false;
    *buf = priv->buf;
    return true;
}

bool JSBuffer::SetPerm(JSObject *obj, Perm perm) {
    _JSBuffer::Priv *priv = PRIV_OR_NULL_FROM_OBJ(_JSBuffer, obj);
    if (!priv)
        return false;
    priv->perm = perm;
    return true;
}

bool JSBuffer::GetPerm(JSObject *obj, Perm *perm) {
    _JSBuffer::Priv *priv = PRIV_OR_NULL_FROM_OBJ(_JSBuffer, obj);
    if (!priv)
        return false;
    *perm = priv->perm;
    return true;
}

bool JSBuffer::IsBuffer(JSContext *cx, JSObject *obj) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return (JS_InstanceOf(cx, obj, &_JSBuffer::Class, NULL) == JS_TRUE) ?
            true : false;
}


BEEPJS_MODULE_INIT(init_buffer_module, 0);
BEEPJS_MODULE_FINI(fini_buffer_module, 0);
