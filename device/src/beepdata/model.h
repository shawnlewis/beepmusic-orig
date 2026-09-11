#ifndef BEEP_MODEL_H
#define BEEP_MODEL_H

#include <json.h>

#include "beep/ds.h"
#include "jsonfile.h"

extern BeepJsonFile* history;
extern int beep_model_run_num;

typedef enum {
    BEEP_MODEL_HISTORY_EVENT_STATION_CHANGE,
    BEEP_MODEL_HISTORY_EVENT_PAUSE,
    BEEP_MODEL_HISTORY_EVENT_RESUME,
    BEEP_MODEL_HISTORY_EVENT_STOP
} BeepModelHistoryEventType;

typedef struct {
    int run_num;
    struct timeval time;
    BeepModelHistoryEventType event;
    json_object* event_data;
} BeepModelHistoryEvent;

typedef struct {
    struct timeval start_time;
    struct timeval elapsed_time;
    json_object* station;
} BeepModelStationSpan;


// Public API
void beep_model_history_init(const char* path, int lucky_duration_local);
void beep_model_history_write_heartbeat(void);
void beep_model_history_add(
        BeepModelHistoryEventType event_type, json_object* event_data);
QUEUE beep_model_history_query_lucky(void);
void beep_model_span_queue_destroy(QUEUE spans);


// Exposed for testing
QUEUE beep_model_get_station_spans(void);

// Debugging
void print_history(QUEUE spans);
void print_spans(QUEUE spans);

#endif  // BEEP_MODEL_H
