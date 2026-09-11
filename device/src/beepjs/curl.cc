#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>

#include "beepjs.h"
#include "curl_priv.h"


namespace {
namespace JSCurl {

typedef struct Priv_s Priv;

typedef struct {
    uv_timer_t t_handle;
    CURLM *multi_handle;
    std::list<Priv *> *unbound_privs;
} ModPriv;

struct Priv_s {
    JSContext *cx;
    JSObject *jsthis;
    ModPriv *mpriv;
    uv_poll_t p_handle;
    int pevents;
    CURL *easy_handle;
    int pause_bitmask;
    bool js_abort;
    bool uv_closed;
    bool end_wait;
    bool bound;
    curl_socket_t sockfd;
    std::list<struct curl_slist *> *slists;
    std::list<uv_buf_t> *w_chunks;
};

typedef enum {
    OPT_INVALID,
    OPT_LONG,
    OPT_OFF_T,
    OPT_STRING,
    OPT_CB,
    OPT_CONV_CB,
    OPT_CB_DATA,
    OPT_POSTFIELDS,
    OPT_SLIST
} OptType;

static JSBool Ctor(JSContext *cx, unsigned argc, jsval *vp);
static void Finalize(JSFreeOp *fop, JSObject *obj);
static JSBool EasySetOpt(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Perform(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Reset(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Write(JSContext *cx, unsigned argc, jsval *vp);
static JSBool End(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Abort(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Pause(JSContext *cx, unsigned argc, jsval *vp);
//static JSBool GetInfo(JSContext *cx, unsigned argc, jsval *vp);
//static JSBool GetDate(JSContext *cx, unsigned argc, jsval *vp);
//static JSBool Version(JSContext *cx, unsigned argc, jsval *vp);
//static JSBool Escape(JSContext *cx, unsigned argc, jsval *vp);
//static JSBool Unescape(JSContext *cx, unsigned argc, jsval *vp);

static void Reset(Priv *priv);
static JSBool EasySetOptWorker(JSContext *cx, unsigned argc, jsval *vp);
static CURLcode EasySetDefaultOpts(Priv *priv);
static OptType EasyGetOptType(uint32_t curl_opt);
static struct curl_slist *JSObjToSlist(JSContext *cx, JSObject *obj,
        struct curl_slist *last_tail);
static void FreeLists(Priv *priv);
static size_t EasyWriteCallback(void *ptr, size_t size, size_t nmemb,
        void *userdata);
static size_t EasyHeaderCallback(void *ptr, size_t size, size_t nmemb,
        void *userdata);
static size_t EasyReadCallback(void *ptr, size_t size, size_t nmemb,
        void *userdata);
static void OnUVClose(uv_handle_t *handle);
static void ReportCurlError(Priv *priv, CURLcode cc);
static void ReportCurlClose(Priv *priv);
static void SocketPollCallback(uv_poll_t* handle, int status, int events);
static int SocketCallback(CURL *easy, curl_socket_t s, int what, void *userp,
        void *socketp);
static void OnTimeout(uv_timer_t *handle, int status);
static int MultiTimerCallback(CURLM *multi, long timeout_ms, void *userp);

static JSClass Class = {
    "Curl",
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
    {NULL, 0, 0, JSOP_NULLWRAPPER, JSOP_NULLWRAPPER}
};

static JSFunctionSpec Funcs[] = {
    JS_FS("setopt", EasySetOpt, 0, JSPROP_ENUMERATE),
    JS_FS("perform", Perform, 0, JSPROP_ENUMERATE),
    JS_FS("reset", Reset, 0, JSPROP_ENUMERATE),
    JS_FS("write", Write, 0, JSPROP_ENUMERATE),
    JS_FS("end", End, 0, JSPROP_ENUMERATE),
    JS_FS("abort", Abort, 0, JSPROP_ENUMERATE),
    JS_FS("pause", Pause, 0, JSPROP_ENUMERATE),
    JS_FS_END
};

static JSObject *Proto;
// This should really be stored somewhere in the js context instead of bss
// which limits beepjs to just a single context.  That is all that is needed
// at the momemnt so it is fine.
static ModPriv *MPriv;
}  // namespace JSCurl

JSObject *beepjs_new_curl_error(JSContext *cx, const char *file, int line,
        CURLcode cc) {
    static const char * const fmt = "%s (CURLcode %d)";
    return beepjs_new_error(cx, file, line, fmt, curl_easy_strerror(cc), cc);
}

JSObject *beepjs_new_curl_error(JSContext *cx, const char *file, int line,
        CURLMcode cmc) {
    static const char * const fmt = "%s (CURLMcode %d)";
    return beepjs_new_error(cx, file, line, fmt, curl_multi_strerror(cmc), cmc);
}

#define CURL_ERROR(__CX__, __CODE__) \
    beepjs_new_curl_error(__CX__, __FILE__, __LINE__, __CODE__)

JSBool JSCurl::Ctor(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    JSObject *curl_obj;
    Priv *priv;
    CURL *easy_handle = curl_easy_init();
    CURLcode cc = CURLE_OK;
    if (!easy_handle) {
        return THROW_ERROR(cx, JSE_OOM);
    }
    curl_obj = JS_NewObject(cx, &Class, Proto, NULL);
    if (!curl_obj) {
        return THROW_ERROR(cx, JSE_OOM);
    }
    priv = (Priv *)JS_malloc(cx, sizeof(Priv));
    if (!priv) {
        return THROW_ERROR(cx, JSE_OOM);
    }
    memset(priv, 0, sizeof(Priv));
    DPRINTF("new priv: %p\n", priv);
    priv->pause_bitmask = CURLPAUSE_CONT;
    priv->slists = new std::list<struct curl_slist *>;
    priv->w_chunks = new std::list<uv_buf_t>;

    // priv, easy_handle and cx are used by EasySetDefaultOpts.
    priv->cx = cx;
    priv->easy_handle = easy_handle;
    DPRINTF("new easy handle: %p\n", easy_handle);
    cc = EasySetDefaultOpts(priv);
    if (cc != CURLE_OK) {
        return THROW_ERROR(cx, CURL_ERROR(cx, cc));
    }
    // Need to wait until SocketCallback is called by libcurl to finish
    // setting up the callback data pointers and priv data.  Since we also
    // have to associate it with this js object we need to stash unbound
    // objects in a list for later.
    MPriv->unbound_privs->push_back(priv);

    priv->jsthis = curl_obj;
    priv->mpriv = MPriv;
    JS_AddObjectRoot(cx, &priv->jsthis);

    JS_SetPrivate(curl_obj, (void *)priv);
    JS_SET_RVAL(cx, vp, OBJECT_TO_JSVAL(curl_obj));
    return JS_TRUE;
}

void JSCurl::Finalize(JSFreeOp *fop, JSObject *obj) {
    Priv *priv = PRIV_FROM_FIN(JSCurl);
    if (priv) {
        Reset(priv);
        if (priv->easy_handle)
            curl_easy_cleanup(priv->easy_handle);
        if (priv->slists)
            delete priv->slists;
        if (priv->w_chunks)
            delete priv->w_chunks;
        JS_freeop(fop, priv);
    }
    DPRINTF("%s called! %p %p %p\n", __PRETTY_FUNCTION__, fop, obj, priv);
}

JSBool JSCurl::EasySetOpt(JSContext *cx, unsigned argc, jsval *vp) {
    JSBool ret;

    ret = EasySetOptWorker(cx, argc, vp);
    return ret;
}

JSBool JSCurl::Perform(JSContext *cx, unsigned argc, jsval *vp) {
    // Doesn't call perform but gets everything going for this object.
    Priv *priv = PRIV_FROM_FUNC(JSCurl);
    CURLMcode cmc;
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);

    cmc = curl_multi_add_handle(MPriv->multi_handle, priv->easy_handle);
    if (cmc != CURLM_OK) {
        return THROW_ERROR(cx, CURL_ERROR(cx, cmc));
    }

    DPRINTF("%s done!\n", __PRETTY_FUNCTION__);
    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSCurl::Reset(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv = PRIV_FROM_FUNC(JSCurl);
    Reset(priv);
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSCurl::Write(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv = PRIV_FROM_FUNC(JSCurl);
    JSString *str_str;
    uv_buf_t new_buf;
    size_t str_len;
    bool first;

    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "S", &str_str)) {
        return JS_FALSE;
    }
    str_len = JS_GetStringEncodingLength(cx, str_str);
    if (str_len == (size_t)-1) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }
    new_buf.len = (unsigned int)str_len;
    new_buf.base = (char *)JS_malloc(cx, str_len);
    if (!new_buf.base) {
        return THROW_ERROR(cx, JSE_OOM);
    }
    JS_EncodeStringToBuffer(str_str, new_buf.base, str_len);
    first = priv->w_chunks->empty();
    priv->w_chunks->push_back(new_buf);
    // If there were no write chunks and priv has a non-zero sockfd (meaning
    // SocketCallback was called for this handle) then we should be paused so
    // resume the read callback.
    if (first && priv->sockfd) {
        priv->pause_bitmask &= ~CURLPAUSE_SEND;
        curl_easy_pause(priv->easy_handle, priv->pause_bitmask);
    }

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSCurl::End(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv = PRIV_FROM_FUNC(JSCurl);
    priv->end_wait = true;
    // Cover a corner case were post method was requested but write was never
    // called.
    priv->pause_bitmask &= ~CURLPAUSE_SEND;
    curl_easy_pause(priv->easy_handle, priv->pause_bitmask);
    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSCurl::Abort(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv = PRIV_FROM_FUNC(JSCurl);
    priv->js_abort = true;
    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

JSBool JSCurl::Pause(JSContext *cx, unsigned argc, jsval *vp) {
    Priv *priv = PRIV_FROM_FUNC(JSCurl);
    JSBool pause;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "b", &pause)) {
        return JS_FALSE;
    }

    DPRINTF("%s called! %d %d\n", __PRETTY_FUNCTION__, pause,
            priv->pause_bitmask);

    if (!priv->uv_closed) {
        if (pause) {
            uv_poll_stop(&priv->p_handle);
        } else {
            uv_poll_start(&priv->p_handle, priv->pevents, SocketPollCallback);
        }
    }

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

void JSCurl::Reset(Priv *priv) {
    curl_easy_reset(priv->easy_handle);
    FreeLists(priv);

    if (priv->w_chunks) {
        DPRINTF("iter priv->w_chunks\n");
        for (std::list<uv_buf_t>::iterator it =
                priv->w_chunks->begin(); it != priv->w_chunks->end(); it++) {
            DPRINTF("free (*it).base\n");
            JS_free(priv->cx, (*it).base);
        }
        priv->slists->clear();
    }

    EasySetDefaultOpts(priv);
}

JSBool JSCurl::EasySetOptWorker(JSContext *cx, unsigned argc, jsval *vp) {
    CURLcode cc = CURLE_OK;
    uint32_t curl_opt;
    OptType opt_type;
    jsval opt_val;
    Priv *priv = PRIV_FROM_FUNC(JSCurl);
    DPRINTF("priv->slists->empty: %d\n", priv->slists->empty());

    // There is some ambiguity in how libcurl uses the CURLoption enum.  GCC
    // will treat it as a uint32_t but parts of the curl code incorrectly uses
    // it as a signed int.
    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "uv", &curl_opt,
            &opt_val)) {
        return JS_FALSE;
    }
    switch (opt_type = EasyGetOptType(curl_opt)) {

    case OPT_STRING: {
        char *opt_str;
        if (!JSVAL_IS_STRING(opt_val)) {
            return THROW_ERROR(cx, JSE_EXP_FOR_ARG, JSE_T_STRING, 2);
        }
        opt_str = JS_EncodeString(cx, JSVAL_TO_STRING(opt_val));
        cc = curl_easy_setopt(priv->easy_handle, (CURLoption)curl_opt,
                opt_str);
        DPRINTF("opt_str: %s\n", opt_str);
        JS_free(cx, opt_str);
        break;
    }

    case OPT_SLIST: {
        struct curl_slist *opt_slist = NULL;
        if (JSVAL_IS_STRING(opt_val)) {
            char *opt_str = JS_EncodeString(cx, JSVAL_TO_STRING(opt_val));
            opt_slist = curl_slist_append(opt_slist, opt_str);
            DPRINTF("opt_slist: %s\n", opt_str);
            JS_free(cx, opt_str);
        } else if (!JSVAL_IS_PRIMITIVE(opt_val)) {
            opt_slist = JSObjToSlist(cx, JSVAL_TO_OBJECT(opt_val), opt_slist);
        }
        if (!opt_slist) {
            // Most likely bad parsing but could be oom.
            return THROW_ERROR(cx, JSE_BAD_ARGS);
        }
        cc = curl_easy_setopt(priv->easy_handle, (CURLoption)curl_opt,
            opt_slist);
        if (cc == CURLE_OK) {
            priv->slists->push_back(opt_slist);
        } else {
            curl_slist_free_all(opt_slist);
        }
        break;
    }

    case OPT_LONG: {
        long opt_long;
        if (JSVAL_IS_INT(opt_val)) {
            opt_long = (long)JSVAL_TO_INT(opt_val);
        } else if (JSVAL_IS_DOUBLE(opt_val)) {
            double opt_double = JSVAL_TO_DOUBLE(opt_val);
            if (opt_double > LONG_MAX || opt_double < LONG_MIN) {
                return THROW_ERROR(cx, JSE_NUM_OOR);
            } else {
                opt_long = (long)opt_double;
            }
        } else {
            return THROW_ERROR(cx, JSE_EXP_FOR_ARG, JSE_T_NUMBER, 2);
        }
        cc = curl_easy_setopt(priv->easy_handle, (CURLoption)curl_opt,
                opt_long);
        DPRINTF("opt_long: %ld\n", opt_long);
        break;
    }

    case OPT_OFF_T:
    case OPT_CB:
    case OPT_CONV_CB:
    case OPT_CB_DATA:
    case OPT_POSTFIELDS:
    case OPT_INVALID:
    default:
        return THROW_ERROR(cx, "unsupported option %d", opt_type);
    }

    DPRINTF("%s called! %d %d\n", __PRETTY_FUNCTION__, curl_opt, (int)opt_type);
    if (cc != CURLE_OK) {
        return THROW_ERROR(cx, CURL_ERROR(cx, cc));
    }
    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

CURLcode JSCurl::EasySetDefaultOpts(Priv *priv) {
    BeepJSRuntime *brt = beepjs_get_brt(priv->cx);
    CURLcode cc = CURLE_OK;
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(priv->easy_handle,
            CURLOPT_WRITEFUNCTION, EasyWriteCallback);
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(priv->easy_handle,
            CURLOPT_WRITEDATA, priv);
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(priv->easy_handle,
            CURLOPT_HEADERFUNCTION, EasyHeaderCallback);
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(priv->easy_handle,
            CURLOPT_WRITEHEADER, priv);
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(priv->easy_handle,
            CURLOPT_READFUNCTION, EasyReadCallback);
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(priv->easy_handle,
            CURLOPT_READDATA, priv);
    cc = (cc != CURLE_OK) ? cc : curl_easy_setopt(priv->easy_handle,
            CURLOPT_PRIVATE, priv);

    if (!brt->opts->http_reuse && cc == CURLE_OK) {
        cc = curl_easy_setopt(priv->easy_handle,
            CURLOPT_FORBID_REUSE, 1L);
    }

    return cc;
}

JSCurl::OptType JSCurl::EasyGetOptType(uint32_t curl_opt) {
    // The curl option type macros are for safety and not included for C++.
    // We need to know for argv conversion.
    // This can also be used to whitelist curl options.
    if (curl_opt < CURLOPTTYPE_OBJECTPOINT) {
        return JSCurl::OPT_LONG;
    } else if (curl_opt > CURLOPTTYPE_OFF_T) {
        return JSCurl::OPT_OFF_T;
    } else {
        switch (curl_opt) {

        case CURLOPT_URL:
        case CURLOPT_PROXY:
        case CURLOPT_INTERFACE:
        case CURLOPT_NETRC_FILE:
        case CURLOPT_USERPWD:
        case CURLOPT_USERNAME:
        case CURLOPT_PASSWORD:
        case CURLOPT_PROXYUSERPWD:
        case CURLOPT_PROXYUSERNAME:
        case CURLOPT_PROXYPASSWORD:
        case CURLOPT_NOPROXY:
        case CURLOPT_ACCEPT_ENCODING:
        case CURLOPT_REFERER:
        case CURLOPT_USERAGENT:
        case CURLOPT_COOKIE:
        case CURLOPT_COOKIEFILE:
        case CURLOPT_COOKIEJAR:
        case CURLOPT_COOKIELIST:
        case CURLOPT_FTPPORT:
        case CURLOPT_FTP_ALTERNATIVE_TO_USER:
        case CURLOPT_FTP_ACCOUNT:
        case CURLOPT_RANGE:
        case CURLOPT_CUSTOMREQUEST:
        case CURLOPT_SSLCERT:
        case CURLOPT_SSLCERTTYPE:
        case CURLOPT_SSLKEY:
        case CURLOPT_SSLKEYTYPE:
        case CURLOPT_KEYPASSWD:
        case CURLOPT_SSLENGINE:
        case CURLOPT_CAINFO:
        case CURLOPT_CAPATH:
        case CURLOPT_RANDOM_FILE:
        case CURLOPT_EGDSOCKET:
        case CURLOPT_SSL_CIPHER_LIST:
        case CURLOPT_KRBLEVEL:
        case CURLOPT_SSH_HOST_PUBLIC_KEY_MD5:
        case CURLOPT_SSH_PUBLIC_KEYFILE:
        case CURLOPT_SSH_PRIVATE_KEYFILE:
        case CURLOPT_CRLFILE:
        case CURLOPT_ISSUERCERT:
        case CURLOPT_SOCKS5_GSSAPI_SERVICE:
        case CURLOPT_SSH_KNOWNHOSTS:
        case CURLOPT_MAIL_FROM:
        case CURLOPT_RTSP_SESSION_ID:
        case CURLOPT_RTSP_STREAM_URI:
        case CURLOPT_RTSP_TRANSPORT:
            return JSCurl::OPT_STRING;

        case CURLOPT_HEADERFUNCTION:
        case CURLOPT_WRITEFUNCTION:
        case CURLOPT_READFUNCTION:
        case CURLOPT_IOCTLFUNCTION:
        case CURLOPT_SOCKOPTFUNCTION:
        case CURLOPT_OPENSOCKETFUNCTION:
        case CURLOPT_PROGRESSFUNCTION:
        case CURLOPT_DEBUGFUNCTION:
        case CURLOPT_SSL_CTX_FUNCTION:
        case CURLOPT_SEEKFUNCTION:
            return JSCurl::OPT_CB;

        case CURLOPT_CONV_TO_NETWORK_FUNCTION:
        case CURLOPT_CONV_FROM_NETWORK_FUNCTION:
        case CURLOPT_CONV_FROM_UTF8_FUNCTION:
            return JSCurl::OPT_CONV_CB;

        case CURLOPT_WRITEDATA:
        case CURLOPT_READDATA:
        case CURLOPT_IOCTLDATA:
        case CURLOPT_SOCKOPTDATA:
        case CURLOPT_OPENSOCKETDATA:
        case CURLOPT_PROGRESSDATA:
        case CURLOPT_WRITEHEADER:
        case CURLOPT_DEBUGDATA:
        case CURLOPT_SSL_CTX_DATA:
        case CURLOPT_SEEKDATA:
        case CURLOPT_PRIVATE:
        case CURLOPT_SSH_KEYDATA:
        case CURLOPT_INTERLEAVEDATA:
        case CURLOPT_CHUNK_DATA:
        case CURLOPT_FNMATCH_DATA:
            return JSCurl::OPT_CB_DATA;

        case CURLOPT_POSTFIELDS:
        case CURLOPT_COPYPOSTFIELDS:
            return JSCurl::OPT_POSTFIELDS;

        case CURLOPT_HTTPHEADER:
        case CURLOPT_HTTP200ALIASES:
        case CURLOPT_QUOTE:
        case CURLOPT_POSTQUOTE:
        case CURLOPT_PREQUOTE:
        case CURLOPT_TELNETOPTIONS:
        case CURLOPT_MAIL_RCPT:
            return JSCurl::OPT_SLIST;

        default:
            break;
        }
    }

    return JSCurl::OPT_INVALID;
}

static struct curl_slist *JSCurl::JSObjToSlist(JSContext *cx, JSObject *obj,
        struct curl_slist *last_tail) {
    // There is no function for merging slists so on error we must either free
    // any slist data in last_tail or leak memory.  This function will free
    // it.

    // This function will report an error if key/value pairs are not strings
    // or for arrays when the values are not strings.
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    struct curl_slist *tail = NULL;
    JS::RootedObject iter(cx, JS_NewPropertyIterator(cx, obj));
    jsid id = INT_TO_JSID(0);
    JSBool np;
    JSBool is_array = JS_IsArrayObject(cx, obj);
    if (!iter) {
        if (last_tail) {
            curl_slist_free_all(last_tail);
        }
        return NULL;
    }
    for (; (np = JS_NextProperty(cx, iter, &id)) && !JSID_IS_VOID(id);) {
        char *key_str = NULL;
        char *val_str = NULL;
        char *prop_str = NULL;
        jsval pval;
        if (!JS_GetPropertyById(cx, obj, id, &pval)) {
            np = JS_FALSE;
            break;
        }
        if ((!is_array && !JSID_IS_STRING(id)) || !JSVAL_IS_STRING(pval)) {
            np = JS_FALSE;
            break;
        }
        val_str = JS_EncodeString(cx, JSVAL_TO_STRING(pval));
        if (is_array) {
            // On error prop_str is undefined.
            if (asprintf(&prop_str, "%s", val_str) == -1)
                prop_str = NULL;
        } else {
            key_str = JS_EncodeString(cx, JSID_TO_STRING(id));
            int as_ret;
            if (strlen(val_str)) {
                as_ret = asprintf(&prop_str, "%s: %s", key_str, val_str);
            } else {
                as_ret = asprintf(&prop_str, "%s:", key_str);
            }
            if (as_ret == -1)
                prop_str = NULL;
        }
        if (prop_str) {
            tail = curl_slist_append(last_tail, prop_str);
            DPRINTF("prop_str: \"%s\"\n", prop_str);
            free(prop_str);
        } else {
            np = JS_FALSE;
        }

        if (key_str)
            JS_free(cx, key_str);
        if (val_str)
            JS_free(cx, val_str);
        if (np == JS_FALSE || !tail) {
            break;
        }
        last_tail = tail;
    }
    if (np != JS_TRUE) {
        tail = NULL;
    }

    if (tail == NULL && last_tail != NULL) {
        curl_slist_free_all(last_tail);
    }
    return tail;
}

static void JSCurl::FreeLists(Priv *priv) {
    if (priv->slists) {
        for (std::list<struct curl_slist *>::iterator it =
                priv->slists->begin(); it != priv->slists->end(); it++) {
            DPRINTF("%s: freeing lists\n", __PRETTY_FUNCTION__);
            curl_slist_free_all(*it);
        }
        priv->slists->clear();
    }
}

size_t JSCurl::EasyWriteCallback(void *ptr, size_t size, size_t nmemb,
        void *userdata) {
    static size_t total = 0;
    total += (size * nmemb);
    Priv *priv = static_cast<Priv *>(userdata);
    jsval argv[1];
    jsval rval;
    uv_buf_t buf;
    buf.base = (char *)ptr;
    buf.len = (unsigned int)(size * nmemb);

    DPRINTF("%s called %p %zu %zu %p total: %zu\n", __PRETTY_FUNCTION__, ptr,
            size, nmemb, userdata, total);
    DPRINTF("%s sockfd: %d\n", __PRETTY_FUNCTION__, priv->sockfd);

    if (priv->js_abort) {
        DPRINTF("CURL ABORT IN WRITE CALLBACK\n");
        return 0;
    }

    JS::RootedObject ptr_obj(priv->cx, JSBuffer::NewBuffer(priv->cx, buf,
            JSBuffer::READ));
    argv[0] = OBJECT_TO_JSVAL(ptr_obj);
    JS::Call(priv->cx, priv->jsthis, "_onWriteCallback", 1, argv, &rval);
    JSBuffer::SetPerm(ptr_obj, JSBuffer::NONE);
    DPRINTF("_onWriteCallback returned: %d\n", JSVAL_TO_INT(rval));

    return (size_t)JSVAL_TO_INT(rval);
}

static size_t JSCurl::EasyHeaderCallback(void *ptr, size_t size, size_t nmemb,
        void *userdata) {
    Priv *priv = static_cast<Priv *>(userdata);
    jsval argv[1];
    jsval rval;
    uv_buf_t buf;
    buf.base = (char *)ptr;
    buf.len = (unsigned int)(size * nmemb);

    DPRINTF("%s called %p %zu %zu %p\n", __PRETTY_FUNCTION__, ptr, size, nmemb,
            userdata);
    DPRINTF("data: %.*s\n", (int)(size * nmemb), (char *)ptr);
    JS::RootedObject ptr_obj(priv->cx, JSBuffer::NewBuffer(priv->cx, buf,
            JSBuffer::READ));
    argv[0] = OBJECT_TO_JSVAL(ptr_obj);
    JS::Call(priv->cx, priv->jsthis, "_onHeaderCallback", 1, argv, &rval);
    JSBuffer::SetPerm(ptr_obj, JSBuffer::NONE);
    DPRINTF("_onHeaderCallback returned: %d\n", JSVAL_TO_INT(rval));

    return (size_t)JSVAL_TO_INT(rval);
}

size_t JSCurl::EasyReadCallback(void *ptr, size_t size, size_t nmemb,
        void *userdata) {
    Priv *priv = static_cast<Priv *>(userdata);
    uv_buf_t buf;
    unsigned int left;
    unsigned int total = (unsigned int)(size * nmemb);

    DPRINTF("%s called %p %zu %zu %p\n", __PRETTY_FUNCTION__, ptr, size, nmemb,
            userdata);
    DPRINTF("%s sockfd: %d\n", __PRETTY_FUNCTION__, priv->sockfd);
    fflush(stdout);

    if (!priv->sockfd) {
        // SocketCallback has not been called yet so pause the read callback.
        priv->pause_bitmask |= CURLPAUSE_SEND;
        return CURL_READFUNC_PAUSE;
    } else if (priv->js_abort) {
        DPRINTF("CURL ABORT IN READ CALLBACK\n");
        return CURL_READFUNC_ABORT;
    } else if (priv->w_chunks->empty()) {
        if (priv->end_wait) {
            // If curl was waiting for read callback to complete and
            // this.end() was called from js let curl know we're done.
            return 0;
        } else {
            // Nothing to write but not done pause the read callback.
            priv->pause_bitmask |= CURLPAUSE_SEND;
            return CURL_READFUNC_PAUSE;
        }
    } else {
        buf = priv->w_chunks->front();
        left = (buf.len > total) ? (buf.len - total) : 0;
        total = buf.len - left;
        memcpy(ptr, buf.base, total);
        if (left) {
            memmove(buf.base, buf.base + total, left);
            buf.len = left;
        } else {
            priv->w_chunks->pop_front();
            JS_free(priv->cx, buf.base);
        }
    }

    return total;
}

void JSCurl::OnUVClose(uv_handle_t *handle) {
    DPRINTF("%s called %p\n", __PRETTY_FUNCTION__, handle);
    Priv *priv = static_cast<Priv *>(handle->data);
    JS_RemoveObjectRoot(priv->cx, &priv->jsthis);
}

void JSCurl::ReportCurlError(Priv *priv, CURLcode cc) {
    jsval argv[1];
    jsval rval;
    char *msg = NULL;

    if (cc == CURLE_OK)
        return;

    if (asprintf(&msg, "%s (%d)", curl_easy_strerror(cc), cc) == -1)
        return;

    JS::RootedString msg_str(priv->cx, JS_NewStringCopyZ(priv->cx, msg));
    free(msg);

    if (!msg_str)
        return;

    argv[0] = STRING_TO_JSVAL(msg_str);
    JS::Call(priv->cx, priv->jsthis, "_onCurlError", 1, argv, &rval);
}

void JSCurl::ReportCurlClose(Priv *priv) {
    jsval rval;
    JS::Call(priv->cx, priv->jsthis, "_onCurlClose", 0, NULL, &rval);
}

void JSCurl::SocketPollCallback(uv_poll_t* handle, int status, int events) {
    Priv *priv = static_cast<Priv *>(handle->data);
    CURLMsg *msg;
    int msgs_in_queue;
    int running_handles;
    int ev_bitmask = ((status & UV_READABLE) ? CURL_POLL_IN : 0)
            | ((status & UV_WRITABLE) ? CURL_POLL_OUT : 0);

    DPRINTF("%s called %p %d %d priv: %p\n", __PRETTY_FUNCTION__, handle,
            status, events, priv);

    uv_timer_stop(&priv->mpriv->t_handle);
    // This will trigger a callback for the particular sockfd.
    curl_multi_socket_action(priv->mpriv->multi_handle, priv->sockfd,
            ev_bitmask, &running_handles);

    while ((msg = curl_multi_info_read(priv->mpriv->multi_handle,
            &msgs_in_queue))) {
        // The messages returned are not necessarily the associated with the
        // polling handle so update priv for each message using
        // CURLINFO_PRIVATE.
        priv = NULL;
        assert(curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE,
                &priv) == CURLE_OK);
        assert(priv);

        // Currently the only msg is for CURLMSG_DONE so for reference this
        // explain how everything shuts down.
        // Note: All of this synchronous to each other.
        // An event occurs on sockfd; libuv calls SocketPollCallback (this).
        // The call to curl_multi_socket_action triggers a call into
        // Easy{Read,Write}Callback (if there is data left) then
        // EasySocketCallback where what == CURL_POLL_REMOVE.  This calls
        // uv_poll_stop and uv_close with OnUVClose as the callback, which
        // it can't since this function has not returned yet.  The call
        // to curl_multi_info_read will then return with CURLMSG_DONE where we
        // call remove this easy_handle from the multi_handle.  Next we
        // report back to javascript that we're done and if an error occured.
        // When that completes and this function returns libuv will close
        // this handle and call OnUVClose.
        if (msg->msg == CURLMSG_DONE) {
            ReportCurlClose(priv);
            if (msg->data.result != CURLE_OK && !priv->js_abort) {
                ReportCurlError(priv, msg->data.result);
            }
            curl_multi_remove_handle(priv->mpriv->multi_handle,
                    priv->easy_handle);
        }
    }
}

int JSCurl::SocketCallback(CURL *easy, curl_socket_t s, int what, void *userp,
        void *socketp) {
    ModPriv *mpriv = static_cast<ModPriv *>(userp);
    Priv *priv = NULL;
    DPRINTF("%s called! %p %d %d %p %p\n", __PRETTY_FUNCTION__, easy, s, what,
            userp, socketp);
    if (socketp == NULL) {
        assert(curl_easy_getinfo(easy, CURLINFO_PRIVATE, &priv) == CURLE_OK);
        assert(priv);
        mpriv->unbound_privs->remove(priv);
        priv->bound = true;
        DPRINTF("easy handle %p found on %p\n", easy, priv);
        // Finish setting up callback data and priv started in Ctor.
        curl_multi_assign(mpriv->multi_handle, s, (void *)priv);
        priv->sockfd = s;
        // Setup uv polling, the first call to uv_poll_start is below.
        uv_poll_init_socket(uv_default_loop(), &priv->p_handle, priv->sockfd);
        priv->p_handle.data = (void *)priv;
        // If we already have pending data for the read callback resume.
        if (!priv->w_chunks->empty()) {
            priv->pause_bitmask &= ~CURLPAUSE_SEND;
            curl_easy_pause(priv->easy_handle, priv->pause_bitmask);
        }
    } else {
        priv = static_cast<Priv *>(socketp);
    }

    if (what == CURL_POLL_REMOVE) {
        uv_poll_stop(&priv->p_handle);
        curl_multi_assign(mpriv->multi_handle, priv->sockfd, NULL);
        // There is a short amount of time after uv_close has been called
        // (which will make all other uv calls on this handle assert) and
        // when javascript is actually notified that we're closed.  Gate
        // other uv_calls through this flag.
        // An alternative if we need this to be available instantly to
        // javscript is to also publish this flag through an object getter.
        priv->uv_closed = true;
        uv_close((uv_handle_t *)&priv->p_handle, OnUVClose);
    } else if (what & CURL_POLL_INOUT) {
        priv->pevents = ((what & CURL_POLL_IN) ? UV_READABLE : 0)
                | ((what & CURL_POLL_OUT) ? UV_WRITABLE : 0);
        uv_poll_start(&priv->p_handle, priv->pevents, SocketPollCallback);
    } else {
        DPRINTF("invalid what %d\n", what);
        abort();
    }

    return 0;
}

void JSCurl::OnTimeout(uv_timer_t *handle, int status) {
    static int last_running_handles = 0;

    ModPriv *mpriv = static_cast<ModPriv *>(handle->data);
    int running_handles;

    //DPRINTF("%s called %p %p %d\n", __PRETTY_FUNCTION__, handle, mpriv,
    //        status);

    // This will either trigger a timeout or if timeout_ms was -1 "kickstart"
    // libcurl.
    curl_multi_socket_action(mpriv->multi_handle, CURL_SOCKET_TIMEOUT, 0,
            &running_handles);

    // If we get an error before a socket is actually opened (i.e. name
    // resolution we can't report the error from SocketPollCallback.
    // We can check for any pending messages here since OnTimeout will
    // get called with -1 when a running handle is removed.  Any messages
    // for unbound privs can then be reported (privs are bound by
    // SocketCallback and will get reported by SocketPollCallback.
    // Compare the number of running_handles from the last call since
    // OnTimeout is called many times to reduce overhead.
    if (running_handles < last_running_handles) {
        Priv *priv;
        CURLMsg *msg;
        int msgs_in_queue;

        while ((msg = curl_multi_info_read(mpriv->multi_handle,
                &msgs_in_queue))) {
            priv = NULL;
            assert(curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE,
                    &priv) == CURLE_OK);
            assert(priv);

            if (!priv->bound && msg->msg == CURLMSG_DONE) {
                DPRINTF("%s got msg for unbound priv: %p\n",
                        __PRETTY_FUNCTION__, priv);
                ReportCurlClose(priv);
                if (msg->data.result != CURLE_OK && !priv->js_abort) {
                    ReportCurlError(priv, msg->data.result);
                }
                curl_multi_remove_handle(mpriv->multi_handle,
                        msg->easy_handle);
            }
        }
    }

    last_running_handles = running_handles;
}

int JSCurl::MultiTimerCallback(CURLM *multi, long timeout_ms, void *userp) {
    //DPRINTF("%s called %p %ld %p\n", __PRETTY_FUNCTION__, multi, timeout_ms,
    //        userp);
    int ret;
    // This is called with a value of when we should call into a multi
    // performing function (curl_multi_socket_action).  timeout_ms value of -1
    // means no timeout is set, 0 means the timeout has been reached.
    if (timeout_ms <= 0)
        timeout_ms = 1;
    ret = uv_timer_start(&MPriv->t_handle, OnTimeout, timeout_ms, 0);
    return (ret == 0) ? 0 : -1;
}

static void fini_curl_module(JSContext *cx) {
    if (JSCurl::MPriv) {
        if (JSCurl::MPriv->unbound_privs) {
            // If there are still unbound privs when the module is destroyed
            // then no SocketCallback was called on them so there will not
            // be a close event for that object.  So the objects have to be
            // unrooted here so the gc can remove them.
            // This is most likely a http.request without a http.end (which
            // is bad and cause leaks from js).
            DPRINTF("unbound_priv size: %zu\n",
                    JSCurl::MPriv->unbound_privs->size());
            for (std::list<JSCurl::Priv *>::iterator it =
                    JSCurl::MPriv->unbound_privs->begin();
                    it != JSCurl::MPriv->unbound_privs->end();
                    it++) {
                JSCurl::Priv *priv = *it;
                JS_RemoveObjectRoot(priv->cx, &priv->jsthis);
            }
            delete JSCurl::MPriv->unbound_privs;
        }
        if (JSCurl::MPriv->multi_handle)
            curl_multi_cleanup(JSCurl::MPriv->multi_handle);
        //if (JSCurl::MPriv->t_handle)
        //    uv_timer_stop(JSCurl::MPriv->t_handle);
        JS_free(cx, JSCurl::MPriv);
    }
    beepjs_delete_native_space(cx, "curl");
    if (JSCurl::Proto)
        JS_RemoveObjectRoot(cx, &JSCurl::Proto);
    curl_global_cleanup();
}

static int init_curl_module(JSContext *cx) {
    int ret;
    CURLMcode cmc = CURLM_OK;
    jsval vp;
    JS::RootedObject mod_space_obj(cx, beepjs_create_native_space(cx, "curl"));
    // We should be just a single thread right now.
    curl_global_init(CURL_GLOBAL_ALL);

    vp = INT_TO_JSVAL(CURL_MAX_WRITE_SIZE);
    if (!JS_SetProperty(cx, mod_space_obj, "CURL_MAX_WRITE_SIZE", &vp))
        goto error;

    JSCurl::Proto = JS_InitClass(cx, mod_space_obj, NULL, &JSCurl::Class,
            JSCurl::Ctor, 0, JSCurl::Props, JSCurl::Funcs, NULL, NULL);
    if (!JSCurl::Proto)
        goto error;
    JS_AddObjectRoot(cx, &JSCurl::Proto);
    if (!JS_DefineConstDoubles(cx, JSCurl::Proto, js_curl_opt_double_spec))
        goto error;

    JSCurl::MPriv = (JSCurl::ModPriv *)JS_malloc(cx, sizeof(JSCurl::ModPriv));
    if (!JSCurl::MPriv)
        goto error;
    memset(JSCurl::MPriv, 0, sizeof(JSCurl::ModPriv));

    ret = uv_timer_init(uv_default_loop(), &JSCurl::MPriv->t_handle);
    if (ret != 0)
        goto error;
    JSCurl::MPriv->t_handle.data = JSCurl::MPriv;

    JSCurl::MPriv->multi_handle = curl_multi_init();
    if (!JSCurl::MPriv->multi_handle)
        goto error;
    cmc = (cmc != CURLM_OK) ? cmc : curl_multi_setopt(
            JSCurl::MPriv->multi_handle, CURLMOPT_SOCKETFUNCTION,
            JSCurl::SocketCallback);
    cmc = (cmc != CURLM_OK) ? cmc : curl_multi_setopt(
            JSCurl::MPriv->multi_handle, CURLMOPT_SOCKETDATA,
            JSCurl::MPriv);
    cmc = (cmc != CURLM_OK) ? cmc : curl_multi_setopt(
            JSCurl::MPriv->multi_handle, CURLMOPT_TIMERFUNCTION,
            JSCurl::MultiTimerCallback);
    cmc = (cmc != CURLM_OK) ? cmc : curl_multi_setopt(
            JSCurl::MPriv->multi_handle, CURLMOPT_TIMERDATA,
            JSCurl::MPriv);
    if (cmc != CURLM_OK)
        goto error;

    JSCurl::MPriv->unbound_privs = new std::list<JSCurl::Priv *>;

    return 1;

error:
    fini_curl_module(cx);
    return 0;
}
}  // namespace


BEEPJS_MODULE_INIT(init_curl_module, 0);
BEEPJS_MODULE_FINI(fini_curl_module, 0);
