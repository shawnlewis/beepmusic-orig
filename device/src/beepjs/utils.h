#ifndef BEEPJS_UTILS_H
#define BEEPJS_UTILS_H

#include "beepjs.h"

namespace JSUtils {

char *ArgvToCStr(JSContext *cx, unsigned argc, jsval *argv);

}

#endif  // BEEPJS_UTILS_H

