/*
 * NB: beep_async_task MUST be called from the uloop thread!
 */
#include <stdlib.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#include "debug.h"
#include "beep_uloop.h"

// #define TRACE_ASYNC_TASK

struct beep_async_task_container {
    struct uloop_fd uloop_event_fd;
    void *userdata;

    //pthread_mutex_t mutex;
    beep_async_task_t task;
    beep_async_done_t done;
};

static void uloop_event_fd_callback(
        struct uloop_fd *u, unsigned int events) {
#ifdef TRACE_ASYNC_TASK
    LOG_DEBUG(log_beep_main, "start");
#endif

    struct beep_async_task_container *container =
        container_of(u, struct beep_async_task_container, uloop_event_fd);

    (*container->done)(container->userdata);
    uloop_fd_delete(&container->uloop_event_fd);

    free(container);

#ifdef TRACE_ASYNC_TASK
    LOG_DEBUG(log_beep_main, "end");
#endif
}

static void *run_async_task(void *arg) {
    struct beep_async_task_container *container =
        (struct beep_async_task_container *)arg;

#ifdef TRACE_ASYNC_TASK
    LOG_DEBUG(log_beep_main, "start");
#endif

    (*container->task)(container->userdata);

#ifdef TRACE_ASYNC_TASK
    LOG_DEBUG(log_beep_main, "async_task returned");
#endif

    uint64_t x = 1;
    write(container->uloop_event_fd.fd, &x, sizeof(uint64_t));

#ifdef TRACE_ASYNC_TASK
    LOG_DEBUG(log_beep_main, "end");
#endif
    return NULL;
}

void beep_async_task(
        beep_async_task_t task,
        beep_async_done_t done,
        void *userdata) {
#ifdef TRACE_ASYNC_TASK
    LOG_DEBUG(log_beep_main, "start");
#endif
    struct beep_async_task_container *container =
        calloc(1, sizeof(struct beep_async_task_container));

    container->userdata = userdata;
    container->task = task;
    container->done = done;
    container->uloop_event_fd.cb = &uloop_event_fd_callback;
    container->uloop_event_fd.fd = eventfd(0, 0);
    uloop_fd_add(&container->uloop_event_fd, ULOOP_READ);

    int ret;
    pthread_t thread_id;

#ifdef TRACE_ASYNC_TASK
    LOG_DEBUG(log_beep_main, "creating thread");
#endif

    ret = pthread_create(&thread_id, NULL, &run_async_task,
            container);
    if(ret != 0) {
        LOG_ERROR(log_beep_main, "Failed to create new thread: %s",
                strerror(errno));
        free(container);
        return;
    }
    pthread_detach(thread_id);

#ifdef TRACE_ASYNC_TASK
    LOG_DEBUG(log_beep_main, "end");
#endif
}
