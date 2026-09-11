#ifndef BEEPJS_BUFFER_H
#define BEEPJS_BUFFER_H

#include "beepjs.h"

namespace JSBuffer {

typedef enum {
    NONE = 0x0,
    WRITE = 0x2,
    READ = 0x4,
    ALL = 0x6
} Perm;

JSObject *NewBuffer(JSContext *cx, uv_buf_t buf, Perm perm = ALL);
bool SetBuffer(JSObject *obj, uv_buf_t buf);
bool GetBuffer(JSObject *obj, uv_buf_t *buf);
bool SetPerm(JSObject *obj, Perm perm);
bool GetPerm(JSObject *obj, Perm *perm);
bool IsBuffer(JSContext *cx, JSObject *obj);

}

#endif  // BEEPJS_BUFFER_H

