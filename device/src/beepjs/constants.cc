#include "beepjs.h"


namespace {
namespace JSConstants {

static JSClass Class = {
    "constants",
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
    JS_FS_END
};

static JSConstDoubleSpec ConstDoubles[] = {
#ifdef O_ACCMODE
    DEFINE_TO_CONST_DOUBLE_SPEC(O_ACCMODE),
#endif
#ifdef O_ACCMODE
    DEFINE_TO_CONST_DOUBLE_SPEC(O_ACCMODE),
#endif
#ifdef O_RDONLY
    DEFINE_TO_CONST_DOUBLE_SPEC(O_RDONLY),
#endif
#ifdef O_WRONLY
    DEFINE_TO_CONST_DOUBLE_SPEC(O_WRONLY),
#endif
#ifdef O_RDWR
    DEFINE_TO_CONST_DOUBLE_SPEC(O_RDWR),
#endif
#ifdef O_CREAT
    DEFINE_TO_CONST_DOUBLE_SPEC(O_CREAT),
#endif
#ifdef O_EXCL
    DEFINE_TO_CONST_DOUBLE_SPEC(O_EXCL),
#endif
#ifdef O_NOCTTY
    DEFINE_TO_CONST_DOUBLE_SPEC(O_NOCTTY),
#endif
#ifdef O_TRUNC
    DEFINE_TO_CONST_DOUBLE_SPEC(O_TRUNC),
#endif
#ifdef O_APPEND
    DEFINE_TO_CONST_DOUBLE_SPEC(O_APPEND),
#endif
#ifdef O_NONBLOCK
    DEFINE_TO_CONST_DOUBLE_SPEC(O_NONBLOCK),
#endif
#ifdef O_NDELAY
    DEFINE_TO_CONST_DOUBLE_SPEC(O_NDELAY),
#endif
#ifdef O_SYNC
    DEFINE_TO_CONST_DOUBLE_SPEC(O_SYNC),
#endif
#ifdef O_FSYNC
    DEFINE_TO_CONST_DOUBLE_SPEC(O_FSYNC),
#endif
#ifdef O_ASYNC
    DEFINE_TO_CONST_DOUBLE_SPEC(O_ASYNC),
#endif
#ifdef O_DIRECTORY
    DEFINE_TO_CONST_DOUBLE_SPEC(O_DIRECTORY),
#endif
#ifdef O_NOFOLLOW
    DEFINE_TO_CONST_DOUBLE_SPEC(O_NOFOLLOW),
#endif
#ifdef O_CLOEXEC
    DEFINE_TO_CONST_DOUBLE_SPEC(O_CLOEXEC),
#endif
#ifdef O_DIRECT
    DEFINE_TO_CONST_DOUBLE_SPEC(O_DIRECT),
#endif
#ifdef O_NOATIME
    DEFINE_TO_CONST_DOUBLE_SPEC(O_NOATIME),
#endif
#ifdef O_PATH
    DEFINE_TO_CONST_DOUBLE_SPEC(O_PATH),
#endif
#ifdef S_IFMT
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IFMT),
#endif
#ifdef S_IFSOCK
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IFSOCK),
#endif
#ifdef S_IFLNK
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IFLNK),
#endif
#ifdef S_IFREG
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IFREG),
#endif
#ifdef S_IFBLK
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IFBLK),
#endif
#ifdef S_IFDIR
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IFDIR),
#endif
#ifdef S_IFCHR
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IFCHR),
#endif
#ifdef S_IFIFO
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IFIFO),
#endif
#ifdef S_ISUID
    DEFINE_TO_CONST_DOUBLE_SPEC(S_ISUID),
#endif
#ifdef S_ISGID
    DEFINE_TO_CONST_DOUBLE_SPEC(S_ISGID),
#endif
#ifdef S_ISVTX
    DEFINE_TO_CONST_DOUBLE_SPEC(S_ISVTX),
#endif
#ifdef S_IRWXU
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IRWXU),
#endif
#ifdef S_IRUSR
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IRUSR),
#endif
#ifdef S_IWUSR
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IWUSR),
#endif
#ifdef S_IXUSR
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IXUSR),
#endif
#ifdef S_IRWXG
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IRWXG),
#endif
#ifdef S_IRGRP
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IRGRP),
#endif
#ifdef S_IWGRP
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IWGRP),
#endif
#ifdef S_IXGRP
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IXGRP),
#endif
#ifdef S_IRWXO
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IRWXO),
#endif
#ifdef S_IROTH
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IROTH),
#endif
#ifdef S_IWOTH
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IWOTH),
#endif
#ifdef S_IXOTH
    DEFINE_TO_CONST_DOUBLE_SPEC(S_IXOTH),
#endif
    {0, NULL, 0, {0, 0, 0}}
};
}  // namespace JSConstants

static void fini_constants_module(JSContext *cx) {
    beepjs_delete_native_space(cx, "constants");
}

static int init_constants_module(JSContext *cx) {
    JSObject *natives_obj = beepjs_get_native_space(cx, "");
    JS::RootedObject mod_obj(cx, JS_DefineObject(cx, natives_obj, "constants",
            &JSConstants::Class, NULL, JSPROP_ENUMERATE));
    if (!mod_obj) {
        return 0;
    }
    if (!JS_DefineFunctions(cx, mod_obj, JSConstants::Funcs)) {
        return 0;
    }
    if (!JS_DefineConstDoubles(cx, mod_obj, JSConstants::ConstDoubles)) {
        return 0;
    }

    return 1;
}
}  // namespace


BEEPJS_MODULE_INIT(init_constants_module, 0);
BEEPJS_MODULE_FINI(fini_constants_module, 0);
