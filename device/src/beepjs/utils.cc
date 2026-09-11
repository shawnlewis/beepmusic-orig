#include "beepjs.h"


namespace JSUtils {

char *ArgvToCStr(JSContext *cx, unsigned argc, jsval *argv) {
    JSString *str;
    char *fullstr = NULL;
    char *wp = NULL;
    char *np;
    size_t alloced = 0;
    size_t len;
    unsigned i;

    for (i = 0; i < argc; i++) {
        str = JS_ValueToString(cx, argv[i]);
        if (!str) {
            if (fullstr)
                free(fullstr);
            THROW_ERROR(cx, "can not convert argument %d to string", i);
            return NULL;
        }
        len = JS_GetStringEncodingLength(cx, str);
        if (wp + len + 1 > fullstr + alloced) {
            alloced = (alloced + len) * 2;
            np = (char *)realloc(fullstr, alloced);
            if (!np) {
                if (fullstr)
                    free(fullstr);
                THROW_ERROR(cx, JSE_OOM);
                return NULL;
            }
            wp = np + (wp - fullstr);
            fullstr = np;
        }
        JS_EncodeStringToBuffer(str, wp, len);
        wp += len;
        *wp++ = ' ';
    }

    *--wp = '\0';

    return fullstr;
}

}  // namespace JSUtils
