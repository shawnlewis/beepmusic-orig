#include "beepjs.h"


namespace {
namespace JSFS {

typedef struct {
    JSContext *cx;
    JSObject *jsthis;
    JSObject *cb;
    const char *func;
    uv_fs_t req;
} ReqData;

static void UVFSCallback(uv_fs_t *req);

static JSBool Close(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Open(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Read(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Fdatasync(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Fsync(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Rename(JSContext *cx, unsigned argc, jsval *vp);
static JSBool FTruncate(JSContext *cx, unsigned argc, jsval *vp);
static JSBool RMDir(JSContext *cx, unsigned argc, jsval *vp);
static JSBool MKDir(JSContext *cx, unsigned argc, jsval *vp);
static JSBool ReadDir(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Stat(JSContext *cx, unsigned argc, jsval *vp);
static JSBool LStat(JSContext *cx, unsigned argc, jsval *vp);
static JSBool FStat(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Link(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Symlink(JSContext *cx, unsigned argc, jsval *vp);
static JSBool ReadLink(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Unlink(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Write(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Chmod(JSContext *cx, unsigned argc, jsval *vp);
static JSBool FChmod(JSContext *cx, unsigned argc, jsval *vp);
static JSBool Chown(JSContext *cx, unsigned argc, jsval *vp);
static JSBool FChown(JSContext *cx, unsigned argc, jsval *vp);
static JSBool UTimes(JSContext *cx, unsigned argc, jsval *vp);
static JSBool FUTimes(JSContext *cx, unsigned argc, jsval *vp);

static JSClass Class = {
    "fs",
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
    JS_FS("close", JSFS::Close, 0, JSPROP_ENUMERATE),
    JS_FS("open", JSFS::Open, 0, JSPROP_ENUMERATE),
    JS_FS("read", JSFS::Read, 0, JSPROP_ENUMERATE),
    JS_FS("fdatasync", JSFS::Fdatasync, 0, JSPROP_ENUMERATE),
    JS_FS("fsync", JSFS::Fsync, 0, JSPROP_ENUMERATE),
    JS_FS("rename", JSFS::Rename, 0, JSPROP_ENUMERATE),
    JS_FS("ftruncate", JSFS::FTruncate, 0, JSPROP_ENUMERATE),
    JS_FS("rmdir", JSFS::RMDir, 0, JSPROP_ENUMERATE),
    JS_FS("mkdir", JSFS::MKDir, 0, JSPROP_ENUMERATE),
    JS_FS("readdir", JSFS::ReadDir, 0, JSPROP_ENUMERATE),
    JS_FS("stat", JSFS::Stat, 0, JSPROP_ENUMERATE),
    JS_FS("lstat", JSFS::LStat, 0, JSPROP_ENUMERATE),
    JS_FS("fstat", JSFS::FStat, 0, JSPROP_ENUMERATE),
    JS_FS("link", JSFS::Link, 0, JSPROP_ENUMERATE),
    JS_FS("symlink", JSFS::Symlink, 0, JSPROP_ENUMERATE),
    JS_FS("readlink", JSFS::ReadLink, 0, JSPROP_ENUMERATE),
    JS_FS("unlink", JSFS::Unlink, 0, JSPROP_ENUMERATE),
    JS_FS("write", JSFS::Write, 0, JSPROP_ENUMERATE),
    JS_FS("chmod", JSFS::Chmod, 0, JSPROP_ENUMERATE),
    JS_FS("fchmod", JSFS::FChmod, 0, JSPROP_ENUMERATE),
    JS_FS("chown", JSFS::Chown, 0, JSPROP_ENUMERATE),
    JS_FS("fchown", JSFS::FChown, 0, JSPROP_ENUMERATE),
    JS_FS("utimes", JSFS::UTimes, 0, JSPROP_ENUMERATE),
    JS_FS("futimes", JSFS::FUTimes, 0, JSPROP_ENUMERATE),
    JS_FS_END
};
}  // namespace JSFS

namespace JSFSStats {

typedef struct {
    uv_statbuf_t stat;
} Priv;

static JSObject *Ctor(JSContext *cx, uv_statbuf_t *stat);
static JSBool Ctor(JSContext *cx, unsigned argc, jsval *vp);
static void Finalize(JSFreeOp *fop, JSObject *obj);
static JSBool FlagGetter(JSContext* cx, JSHandleObject obj,
        JSHandleId idval, JSMutableHandleValue vp);

static JSClass Class = {
    "Stats",
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
    STATP_DEV,
    STATP_INO,
    STATP_MODE,
    STATP_NLINK,
    STATP_UID,
    STATP_GID,
    STATP_RDEV,
    STATP_SIZE,
    STATP_BLKSIZE,
    STATP_BLOCKS,
    STATP_ATIME,
    STATP_MTIME,
    STATP_CTIME
} StatP;

#define JSFSSTATPROP_FLAGS \
    (JSPROP_READONLY | JSPROP_PERMANENT | JSPROP_ENUMERATE)

static JSPropertySpec Props[] = {
    {"dev", STATP_DEV, JSFSSTATPROP_FLAGS, JSOP_WRAPPER(FlagGetter), JSOP_NULLWRAPPER},
    {"ino", STATP_INO, JSFSSTATPROP_FLAGS, JSOP_WRAPPER(FlagGetter), JSOP_NULLWRAPPER},
    {"mode", STATP_MODE, JSFSSTATPROP_FLAGS, JSOP_WRAPPER(FlagGetter), JSOP_NULLWRAPPER},
    {"nlink", STATP_NLINK, JSFSSTATPROP_FLAGS, JSOP_WRAPPER(FlagGetter), JSOP_NULLWRAPPER},
    {"uid", STATP_UID, JSFSSTATPROP_FLAGS, JSOP_WRAPPER(FlagGetter), JSOP_NULLWRAPPER},
    {"gid", STATP_GID, JSFSSTATPROP_FLAGS, JSOP_WRAPPER(FlagGetter), JSOP_NULLWRAPPER},
    {"rdev", STATP_RDEV, JSFSSTATPROP_FLAGS, JSOP_WRAPPER(FlagGetter), JSOP_NULLWRAPPER},
    {"size", STATP_SIZE, JSFSSTATPROP_FLAGS, JSOP_WRAPPER(FlagGetter), JSOP_NULLWRAPPER},
    {"blksize", STATP_BLKSIZE, JSFSSTATPROP_FLAGS, JSOP_WRAPPER(FlagGetter), JSOP_NULLWRAPPER},
    {"blocks", STATP_BLOCKS, JSFSSTATPROP_FLAGS, JSOP_WRAPPER(FlagGetter), JSOP_NULLWRAPPER},
    {"atime", STATP_ATIME, JSFSSTATPROP_FLAGS, JSOP_WRAPPER(FlagGetter), JSOP_NULLWRAPPER},
    {"mtime", STATP_MTIME, JSFSSTATPROP_FLAGS, JSOP_WRAPPER(FlagGetter), JSOP_NULLWRAPPER},
    {"ctime", STATP_CTIME, JSFSSTATPROP_FLAGS, JSOP_WRAPPER(FlagGetter), JSOP_NULLWRAPPER},
    {NULL, 0, 0, JSOP_NULLWRAPPER, JSOP_NULLWRAPPER}
};

static JSFunctionSpec Funcs[] = {
    JS_FS_END
};

static JSObject *Proto;
}  // namespace JSFSStats

// ASYNC_CALL is always gated by JS_ObjectIsCallable (although may not be the
// expected definition).
#define ASYNC_CALL(FUNC, CB, ...) \
    ReqData *_req_data = (ReqData *)JS_malloc(cx, sizeof(ReqData)); \
    if (!_req_data) { \
        return THROW_ERROR(cx, JSE_OOM); \
    } \
    _req_data->cx = cx; \
    _req_data->jsthis = JS_THIS_OBJECT(cx, vp); \
    _req_data->cb = cb; \
    _req_data->func = #FUNC; \
    _req_data->req.data = (void *)_req_data; \
    JS_AddObjectRoot(cx, &_req_data->jsthis); \
    JS_AddObjectRoot(cx, &_req_data->cb); \
    int _result = uv_fs_ ## FUNC(uv_default_loop(), &_req_data->req, \
            __VA_ARGS__, UVFSCallback); \
    if (_result < 0) { \
        _req_data->req.result = _result; \
        _req_data->req.path = NULL; \
        _req_data->req.errorno = uv_last_error(uv_default_loop()).code; \
        UVFSCallback(&_req_data->req); \
    }

#define SYNC_CALL(FUNC, PATH, ...) \
    uv_fs_t _req; \
    int _result = uv_fs_ ## FUNC(uv_default_loop(), &_req, __VA_ARGS__, NULL); \
    if (_result < 0) { \
        uv_err_code _code = uv_last_error(uv_default_loop()).code; \
        uv_fs_req_cleanup(&_req); \
        THROW_ERROR(cx, UV_ERROR(cx, _code, #FUNC, PATH)); \
    }

#define SYNC_REQ _req
#define SYNC_RESULT _result
#define SYNC_ERROR_RET() \
    do { \
        if (_result < 0) { \
            return JS_FALSE; \
        } \
    } while(0)
#define SYNC_DONE() uv_fs_req_cleanup(&_req)

void JSFS::UVFSCallback(uv_fs_t *req) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    DPRINTF("result: %zu errorno: %d fs_type: %d data: %p\n", req->result,
            req->errorno, req->fs_type, req->data);

    ReqData *req_data = (ReqData *)req->data;
    unsigned argc = 1;
    jsval argv[2];
    jsval rval;

    if (req->result < 0) {
        argv[0] = OBJECT_TO_JSVAL(UV_ERROR(req_data->cx, req->errorno,
                req_data->func, req->path ? req->path : "?"));
    } else {
        argc = 2;
        argv[0] = JSVAL_NULL;

        switch (req->fs_type) {
        case UV_FS_CLOSE:
        case UV_FS_UNLINK:
            argc = 1;
            break;

        case UV_FS_OPEN:
            argv[1] = INT_TO_JSVAL(req->result);
            break;

        case UV_FS_READ:
            argv[1] = INT_TO_JSVAL(req->result);
            break;

        case UV_FS_STAT: {
            JSObject *ret_obj = JSFSStats::Ctor(req_data->cx,
                    static_cast<uv_statbuf_t *>(req->ptr));
            argv[1] = OBJECT_TO_JSVAL(ret_obj);
            break;
        }

        case UV_FS_WRITE:
            argv[1] = INT_TO_JSVAL(req->result);
            break;

        default:
            argc = 1;
            argv[0] = OBJECT_TO_JSVAL(ERROR(req_data->cx,
                    "fs async %s unsupported", req_data->func));
            break;
        }
    }

    // This can return false if we throw an error in the callback so ignore return value.
    JS::Call(req_data->cx, req_data->jsthis, OBJECT_TO_JSVAL(req_data->cb), argc, argv, &rval);

    JS_RemoveObjectRoot(req_data->cx, &req_data->jsthis);
    JS_RemoveObjectRoot(req_data->cx, &req_data->cb);
    uv_fs_req_cleanup(req);
    JS_free(req_data->cx, req_data);
}

JSBool JSFS::Close(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    int32_t fd;
    JSObject *cb = NULL;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "i/o", &fd, &cb)) {
        return JS_FALSE;
    }

    if (cb && JS_ObjectIsCallable(cx, cb)) {
        ASYNC_CALL(close, cb, fd);
        JS_SET_RVAL(cx, vp, JSVAL_VOID);
        return JS_TRUE;
    } else {
        SYNC_CALL(close, 0, fd);
        SYNC_ERROR_RET();
        SYNC_DONE();
        JS_SET_RVAL(cx, vp, JSVAL_VOID);
        return JS_TRUE;
    }
}

JSBool JSFS::Open(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    JSString *path_str;
    int32_t flags;
    int32_t mode;
    JSObject *cb = NULL;
    char *path_cstr;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "Sii/o", &path_str,
            &flags, &mode, &cb)) {
        return JS_FALSE;
    }
    path_cstr = JS_EncodeString(cx, path_str);
    if (!path_cstr) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }

    if (cb && JS_ObjectIsCallable(cx, cb)) {
        ASYNC_CALL(open, cb, path_cstr, flags, mode);
        JS_free(cx, path_cstr);
        JS_SET_RVAL(cx, vp, JSVAL_VOID);
        return JS_TRUE;
    } else {
        SYNC_CALL(open, path_cstr, path_cstr, flags, mode);
        JS_free(cx, path_cstr);
        SYNC_ERROR_RET();
        JS_SET_RVAL(cx, vp, INT_TO_JSVAL(SYNC_RESULT));
        SYNC_DONE();
        return JS_TRUE;
    }
}

JSBool JSFS::Read(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    int32_t fd;
    JSObject *buf_obj;
    uint32_t offset;
    uint32_t length;
    int32_t pos;
    JSObject *cb = NULL;
    uv_buf_t buf;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "iouui/o", &fd,
            &buf_obj, &offset, &length, &pos, &cb)) {
        return JS_FALSE;
    }
    if (!JSBuffer::IsBuffer(cx, buf_obj)) {
        return THROW_ERROR(cx, JSE_EXP_FOR_ARG, JSE_T_BUFFER, 2);
    }
    if (!JSBuffer::GetBuffer(buf_obj, &buf)) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }
    if (offset >= buf.len) {
        return THROW_ERROR(cx, JSE_INDEX_OOR);
    }
    if (offset + length > buf.len) {
        return THROW_ERROR(cx, JSE_LENGTH_OOR);
    }

    if (cb && JS_ObjectIsCallable(cx, cb)) {
        ASYNC_CALL(read, cb, fd, buf.base + offset, length, pos);
        JS_SET_RVAL(cx, vp, JSVAL_VOID);
        return JS_TRUE;
    } else {
        SYNC_CALL(read, 0, fd, buf.base + offset, length, pos);
        SYNC_ERROR_RET();
        JS_SET_RVAL(cx, vp, INT_TO_JSVAL(SYNC_RESULT));
        SYNC_DONE();
        return JS_TRUE;
    }
}

JSBool JSFS::Fdatasync(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::Fsync(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::Rename(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::FTruncate(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::RMDir(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::MKDir(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::ReadDir(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::Stat(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    JSString *path_str;
    JSObject *cb = NULL;
    char *path_cstr;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "S/o", &path_str,
            &cb)) {
        return JS_FALSE;
    }
    path_cstr = JS_EncodeString(cx, path_str);
    if (!path_cstr) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }

    if (cb && JS_ObjectIsCallable(cx, cb)) {
        ASYNC_CALL(stat, cb, path_cstr);
        JS_free(cx, path_cstr);
        JS_SET_RVAL(cx, vp, JSVAL_VOID);
        return JS_TRUE;
    } else {
        SYNC_CALL(stat, path_cstr, path_cstr);
        JS_free(cx, path_cstr);
        SYNC_ERROR_RET();
        JSObject *ret_obj = JSFSStats::Ctor(cx,
                static_cast<uv_statbuf_t *>(SYNC_REQ.ptr));
        JS_SET_RVAL(cx, vp, OBJECT_TO_JSVAL(ret_obj));
        SYNC_DONE();
        return JS_TRUE;
    }
}

JSBool JSFS::LStat(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::FStat(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    int32_t fd;
    JSObject *cb = NULL;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "i/o", &fd,
            &cb)) {
        return JS_FALSE;
    }

    if (cb && JS_ObjectIsCallable(cx, cb)) {
        ASYNC_CALL(fstat, cb, fd);
        JS_SET_RVAL(cx, vp, JSVAL_VOID);
        return JS_TRUE;
    } else {
        SYNC_CALL(fstat, 0, fd);
        SYNC_ERROR_RET();
        JSObject *ret_obj = JSFSStats::Ctor(cx,
                static_cast<uv_statbuf_t *>(SYNC_REQ.ptr));
        JS_SET_RVAL(cx, vp, OBJECT_TO_JSVAL(ret_obj));
        SYNC_DONE();
        return JS_TRUE;
    }
}

JSBool JSFS::Link(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::Symlink(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::ReadLink(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::Unlink(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    JSString *path_str;
    JSObject *cb = NULL;
    char *path_cstr;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "S/o", &path_str,
            &cb)) {
        return JS_FALSE;
    }
    path_cstr = JS_EncodeString(cx, path_str);
    if (!path_cstr) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }

    if (cb && JS_ObjectIsCallable(cx, cb)) {
        ASYNC_CALL(unlink, cb, path_cstr);
        JS_free(cx, path_cstr);
        JS_SET_RVAL(cx, vp, JSVAL_VOID);
        return JS_TRUE;
    } else {
        SYNC_CALL(unlink, path_cstr, path_cstr);
        JS_free(cx, path_cstr);
        SYNC_ERROR_RET();
        SYNC_DONE();
        JS_SET_RVAL(cx, vp, JSVAL_VOID);
        return JS_TRUE;
    }
}

JSBool JSFS::Write(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    int32_t fd;
    JSObject *buf_obj;
    uint32_t offset;
    uint32_t length;
    int32_t pos;
    JSObject *cb = NULL;
    uv_buf_t buf;

    if (!JS_ConvertArguments(cx, argc, JS_ARGV(cx, vp), "iouui/o", &fd,
            &buf_obj, &offset, &length, &pos, &cb)) {
        return JS_FALSE;
    }
    if (!JSBuffer::IsBuffer(cx, buf_obj)) {
        return THROW_ERROR(cx, JSE_EXP_FOR_ARG, JSE_T_BUFFER, 2);
    }
    if (!JSBuffer::GetBuffer(buf_obj, &buf)) {
        return THROW_ERROR(cx, JSE_BAD_ARGS);
    }
    if (offset >= buf.len) {
        return THROW_ERROR(cx, JSE_INDEX_OOR);
    }
    if (offset + length > buf.len) {
        return THROW_ERROR(cx, JSE_LENGTH_OOR);
    }

    if (cb && JS_ObjectIsCallable(cx, cb)) {
        ASYNC_CALL(write, cb, fd, buf.base + offset, length, pos);
        JS_SET_RVAL(cx, vp, JSVAL_VOID);
        return JS_TRUE;
    } else {
        SYNC_CALL(write, 0, fd, buf.base + offset, length, pos);
        SYNC_ERROR_RET();
        JS_SET_RVAL(cx, vp, INT_TO_JSVAL(SYNC_RESULT));
        SYNC_DONE();
        return JS_TRUE;
    }
}

JSBool JSFS::Chmod(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::FChmod(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::Chown(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::FChown(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::UTimes(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSBool JSFS::FUTimes(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    return THROW_ERROR(cx, JSE_UNSUPPORTED, __FUNCTION__);
}

JSObject *JSFSStats::Ctor(JSContext *cx, uv_statbuf_t *stat) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    JSObject *stats_obj;
    Priv *priv;
    stats_obj = JS_NewObject(cx, &Class, Proto, NULL);
    if (!stats_obj) {
        return NULL;
    }
    priv = (Priv *)JS_malloc(cx, sizeof(Priv));
    if (!priv) {
        return NULL;
    }
    memset(priv, 0, sizeof(Priv));
    if (stat) {
        priv->stat = *stat;
    }

    JS_SetPrivate(stats_obj, (void *)priv);
    return stats_obj;
}

JSBool JSFSStats::Ctor(JSContext *cx, unsigned argc, jsval *vp) {
    DPRINTF("%s called!\n", __PRETTY_FUNCTION__);
    JSObject *stats_obj = Ctor(cx, NULL);
    if (!stats_obj)
        return THROW_ERROR(cx, JSE_OOM);

    JS_SET_RVAL(cx, vp, OBJECT_TO_JSVAL(stats_obj));
    return JS_TRUE;
}

void JSFSStats::Finalize(JSFreeOp *fop, JSObject *obj) {
    Priv *priv = PRIV_FROM_FIN(JSFSStats);
    if (priv) {
        JS_freeop(fop, priv);
    }
    DPRINTF("%s called! %p %p %p\n", __PRETTY_FUNCTION__, fop, obj, priv);
}

JSBool JSFSStats::FlagGetter(JSContext* cx, JSHandleObject obj,
        JSHandleId idval, JSMutableHandleValue vp) {
    Priv *priv = PRIV_FROM_OBJ(JSFSStats, obj);
    uint32_t val = 0;

    switch (JSID_TO_INT(idval)) {

    case STATP_DEV:
        val = (uint32_t)priv->stat.st_dev; break;

    case STATP_INO:
        val = (uint32_t)priv->stat.st_ino; break;

    case STATP_MODE:
        val = (uint32_t)priv->stat.st_mode; break;

    case STATP_NLINK:
        val = (uint32_t)priv->stat.st_nlink; break;

    case STATP_UID:
        val = (uint32_t)priv->stat.st_uid; break;

    case STATP_GID:
        val = (uint32_t)priv->stat.st_gid; break;

    case STATP_RDEV:
        val = (uint32_t)priv->stat.st_rdev; break;

    case STATP_SIZE:
        val = (uint32_t)priv->stat.st_size; break;

    case STATP_BLKSIZE:
        val = (uint32_t)priv->stat.st_blksize; break;

    case STATP_BLOCKS:
        val = (uint32_t)priv->stat.st_blocks; break;

    case STATP_ATIME:
        val = (uint32_t)priv->stat.st_atime; break;

    case STATP_MTIME:
        val = (uint32_t)priv->stat.st_mtime; break;

    case STATP_CTIME:
        val = (uint32_t)priv->stat.st_ctime; break;

    default:
        return THROW_ERROR(cx, "invalid id %d", JSID_TO_INT(idval));
    }

    vp.set(UINT_TO_JSVAL(val));
    return JS_TRUE;
}

static void fini_fs_module(JSContext *cx) {
    beepjs_delete_native_space(cx, "fs");
    if (JSFSStats::Proto)
        JS_RemoveObjectRoot(cx, &JSFSStats::Proto);
}

static int init_fs_module(JSContext *cx) {
    JSObject *natives_obj = beepjs_get_native_space(cx, "");
    JS::RootedObject mod_obj(cx, JS_DefineObject(cx, natives_obj, "fs",
            &JSFS::Class, NULL, JSPROP_ENUMERATE));
    if (!mod_obj) {
        return 0;
    }
    if (!JS_DefineFunctions(cx, mod_obj, JSFS::Funcs)) {
        return 0;
    }

    JSFSStats::Proto = JS_InitClass(cx, mod_obj, NULL, &JSFSStats::Class,
            JSFSStats::Ctor, 0, JSFSStats::Props, JSFSStats::Funcs, NULL,
            NULL);
    if (!JSFSStats::Proto) {
        return 0;
    }
    JS_AddObjectRoot(cx, &JSFSStats::Proto);

    return 1;
}
}  // namespace


BEEPJS_MODULE_INIT(init_fs_module, 0);
BEEPJS_MODULE_FINI(fini_fs_module, 0);
