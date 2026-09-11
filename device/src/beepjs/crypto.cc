#include <openssl/ssl.h>
#include <openssl/evp.h>

#include "beepjs.h"


namespace {
static const char hex_char[] = "0123456789abcdef";

namespace JSCrypto {
namespace Hash {

typedef struct {
    EVP_MD_CTX md_ctx;
    const EVP_MD *md_type;
} Priv;

static JSBool Ctor(JSContext *cx, unsigned argc, jsval *vp);
static void Finalize(JSFreeOp *fop, JSObject *obj);
static JSBool Digest(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Update(JSContext *cx, unsigned argc, jsval *vp);

static JSClass Class = {
    "Hash",
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
    JS_FS("digest", Digest, 0, JSPROP_ENUMERATE),
    JS_FS("update", Update, 0, JSPROP_ENUMERATE),
    JS_FS_END
};

static JSObject *Proto;
}  // namespace Hash
}  // namespace JSCrypto

JSBool JSCrypto::Hash::Ctor(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    JSString *md_type_str;
    char *md_type_cstr;
    JSObject *crypto_obj;
    Priv *priv;
    const EVP_MD *md_type;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "S", &md_type_str)) {
        return JS_FALSE;
    }
    md_type_cstr = JS_EncodeString(cx, md_type_str);
    if (!md_type_cstr) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }

    md_type = EVP_get_digestbyname(md_type_cstr);
    if (!md_type) {
        JSObject *err_obj = ERROR(cx, "unsupported md type %s", md_type_cstr);
        JS_free(cx, md_type_cstr);
        return THROW_ERROR(cx, err_obj);
    }
    JS_free(cx, md_type_cstr);

    crypto_obj = JS_NewObject(cx, &Class, Proto, NULL);
    if (!crypto_obj) {
        return THROW_ERROR(cx, JSE_OOM);
    }
    priv = (Priv *)JS_malloc(cx, sizeof(Priv));
    if (!priv) {
        return THROW_ERROR(cx, JSE_OOM);
    }
    memset(priv, 0, sizeof(Priv));

    priv->md_type = md_type;
    EVP_MD_CTX_init(&priv->md_ctx);
    EVP_DigestInit_ex(&priv->md_ctx, priv->md_type, NULL);

    JS_SetPrivate(crypto_obj, (void *)priv);
    JS_SET_RVAL(cx, vp, OBJECT_TO_JSVAL(crypto_obj));
    return JS_TRUE;
}

void JSCrypto::Hash::Finalize(JSFreeOp *fop, JSObject *obj) {
    Priv *priv = PRIV_FROM_FIN(JSCrypto::Hash);
    if (priv) {
        EVP_MD_CTX_cleanup(&priv->md_ctx);
        JS_freeop(fop, priv);
    }
    DPRINTF("%s called! %p %p %p\n", __PRETTY_FUNCTION__, fop, obj, priv);
}

JSBool JSCrypto::Hash::Digest(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSCrypto::Hash);
    JSString *enc_str;
    char *enc_cstr;
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_cnt;
    JSString *md_str;
    char md_cstr[EVP_MAX_MD_SIZE * 2];
    unsigned int i;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "S", &enc_str)) {
        return JS_FALSE;
    }
    enc_cstr = JS_EncodeString(cx, enc_str);
    if (!enc_cstr) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }

    if (strcmp(enc_cstr, "hex")) {
        JSObject *err_obj = ERROR(cx, JSE_INVALID_ENC, enc_cstr);
        JS_free(cx, enc_cstr);
        return THROW_ERROR(cx, err_obj);
    }
    JS_free(cx, enc_cstr);

    EVP_DigestFinal_ex(&priv->md_ctx, md, &md_cnt);

    for (i = 0; i < md_cnt; i++) {
        md_cstr[(2 * i)] = hex_char[md[i] >> 4];
        md_cstr[(2 * i) + 1] = hex_char[md[i] & 0xf];
    }
    md_str = JS_NewStringCopyN(cx, md_cstr, (md_cnt * 2));

    JS_SET_RVAL(cx, vp, STRING_TO_JSVAL(md_str));
    return JS_TRUE;
}

JSBool JSCrypto::Hash::Update(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    Priv *priv = PRIV_FROM_FUNC(JSCrypto::Hash);
    jsval data_val;
    JSString *enc_str = NULL;
    char *enc_cstr = NULL;
    bool own_mem = false;
    char *data = NULL;
    size_t len;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "v/S", &data_val,
            &enc_str)) {
        return JS_FALSE;
    }

    if (enc_str) {
        enc_cstr = JS_EncodeString(cx, enc_str);
    }

    if (!enc_cstr || !strcmp(enc_cstr, "buffer")) {
        if (!JSVAL_IS_PRIMITIVE(data_val) && JSBuffer::IsBuffer(cx, JSVAL_TO_OBJECT(data_val))) {
            uv_buf_t buf;
            JSBuffer::GetBuffer(JSVAL_TO_OBJECT(data_val), &buf);
            data = buf.base;
            len = buf.len;
        }
    } else if (!strcmp(enc_cstr, "binary") || !strcmp(enc_cstr, "utf8")) {
        if (JSVAL_IS_STRING(data_val)) {
            JSString *data_str = JSVAL_TO_STRING(data_val);
            len = JS_GetStringEncodingLength(cx, data_str);
            if (len != (size_t)-1) {
                own_mem = true;
                data = JS_EncodeString(cx, data_str);
            }
        }
    } else {
        JSObject *err_obj = ERROR(cx, JSE_INVALID_ENC, enc_cstr);
        JS_free(cx, enc_cstr);
        return THROW_ERROR(cx, err_obj);
    }

    if (enc_cstr)
        JS_free(cx, enc_cstr);
    if (!data) {
        // Could be oom.
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }

    EVP_DigestUpdate(&priv->md_ctx, data, len);

    if (own_mem) {
        JS_free(cx, data);
    }

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

static void fini_crypto_module(JSContext *cx) {
    beepjs_delete_native_space(cx, "crypto");
    if (JSCrypto::Hash::Proto)
        JS_RemoveObjectRoot(cx, &JSCrypto::Hash::Proto);
    EVP_cleanup();
}

static int init_crypto_module(JSContext *cx) {
    JS::RootedObject mod_space_obj(cx, beepjs_create_native_space(cx, "crypto"));
    JSCrypto::Hash::Proto = JS_InitClass(cx, mod_space_obj, NULL,
            &JSCrypto::Hash::Class, JSCrypto::Hash::Ctor, 0, NULL,
            JSCrypto::Hash::Funcs, NULL, NULL);
    if (!JSCrypto::Hash::Proto)
        return 0;
    JS_AddObjectRoot(cx, &JSCrypto::Hash::Proto);

    SSL_library_init();
    OpenSSL_add_all_digests();

    return 1;
}
}  // namespace


BEEPJS_MODULE_INIT(init_crypto_module, 0);
BEEPJS_MODULE_FINI(fini_crypto_module, 0);
