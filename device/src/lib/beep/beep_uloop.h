/*
 * NB: beep_async_task MUST be called from the uloop thread!
 */

#ifndef BEEP_ULOOP_H
#define BEEP_ULOOP_H

#include <libubox/uloop.h>

typedef void(*beep_async_task_t)(void *userdata);
typedef void(*beep_async_done_t)(void *userdata);

void beep_async_task(
        beep_async_task_t task,
        beep_async_done_t done,
        void *userdata);

#endif  // BEEP_ULOOP_H
