#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "beep/debug.h"
#include "beep/json.h"
#include "model.h"

BeepJsonFile *history;
int beep_model_run_num;

static int lucky_duration;

BeepModelStationSpan* beep_model_span_init(
        struct timeval start_time, json_object* station) {
    BeepModelStationSpan* span = malloc(sizeof(BeepModelStationSpan));
    span->start_time = start_time;
    json_object_get(station);
    span->station = station;
    return span;
}

void beep_model_span_mark_end(
        BeepModelStationSpan* span, BeepModelHistoryEvent* end_event) {
    timersub(&end_event->time, &span->start_time, &span->elapsed_time);
}

void beep_model_span_destroy(BeepModelStationSpan* span) {
    json_object_put(span->station);
    free(span);
}

void beep_model_span_queue_destroy(QUEUE spans) {
    qCloseWithFunction(spans, (void (*)(void*)) beep_model_span_destroy);
}

void beep_model_history_write_row(
        struct timeval time,
        BeepModelHistoryEventType event_type,
        json_object* event_data) {
    json_object* row = json_object_new_object();
    json_object_object_add(row, "time_s",
            json_object_new_int(time.tv_sec));
    json_object_object_add(row, "time_us",
            json_object_new_int(time.tv_usec));
    json_object_object_add(row, "event_type",
            json_object_new_int(event_type));
    json_object_object_add(row, "event_data", event_data);
    beep_jsonfile_add_row(history, row);
    json_object_put(row);
}

void beep_model_history_init(const char* path, int lucky_duration_local) {
    lucky_duration = lucky_duration_local;
    history = beep_jsonfile_init(path);
    if (!history) {
        LOG_ERROR(log_beep_main,
                "Couldn\'t open jsonfile, if developing pass "
                "--data_path=beepdata.db");
        exit(1);
    }
    int heartbeat_secs;
    if (beep_jsonfile_read_metadata_int(
                history, "heartbeat_sec", &heartbeat_secs) == 0) {
        struct timeval heartbeat_time;
        heartbeat_time.tv_sec = heartbeat_secs;
        heartbeat_time.tv_usec = 0;
        beep_model_history_write_row(
                heartbeat_time, BEEP_MODEL_HISTORY_EVENT_STOP, NULL);
    }
}

void beep_model_history_write_heartbeat() {
    struct timeval cur_time;
    gettimeofday(&cur_time, NULL);
    beep_jsonfile_write_metadata_int(
            history, "heartbeat_sec", cur_time.tv_sec);
}

void beep_model_history_add(
        BeepModelHistoryEventType event_type, json_object* event_data) {
    struct timeval cur_time;
    gettimeofday(&cur_time, NULL);
    beep_model_history_write_row(cur_time, event_type, event_data);
}

QUEUE beep_model_get_station_spans() {
    // TODO: on init of beep_model, commit a made up station stopped event for
    //     previous heartbeat time
    QUEUE spans = qMake();
    BeepModelStationSpan* pending = NULL;
    beep_jsonfile_foreach(row, history) {
        BeepModelHistoryEvent event;
        event.time.tv_sec = json_object_get_int(
                json_object_object_get(row, "time_s"));
        event.time.tv_usec = json_object_get_int(
                json_object_object_get(row, "time_us"));
        event.event = json_object_get_int(
                json_object_object_get(row, "event_type"));
        event.event_data = json_object_object_get(row, "event_data");
        json_object_get(event.event_data);
        json_object_put(row);

        if (event.event == BEEP_MODEL_HISTORY_EVENT_STATION_CHANGE) {
            // commit old span, with this timestamp
            if (pending) {
                beep_model_span_mark_end(pending, &event);
                qEnque(spans, pending);
                pending = NULL;
            }

            // Create new pending based on this event
            pending = beep_model_span_init(event.time, event.event_data);
        } else if (event.event == BEEP_MODEL_HISTORY_EVENT_PAUSE) {
            // commit old span with this timestamp
            if (pending) {
                beep_model_span_mark_end(pending, &event);
                qEnque(spans, pending);
                pending = NULL;
            }
        } else if (event.event == BEEP_MODEL_HISTORY_EVENT_RESUME) {
            if (pending) {
                // This is an error.
                LOG_ERROR(log_beep_main, "Got unexpected resume");
            }
            if (qLen(spans) > 0) {
                // create pending event from qLast.
                pending = beep_model_span_init(
                        event.time,
                        ((BeepModelStationSpan*) qLast(spans))->station);
            }
        } else if (event.event == BEEP_MODEL_HISTORY_EVENT_STOP) {
            if (pending) {
                beep_model_span_mark_end(pending, &event);
                if (pending->elapsed_time.tv_sec > 0
                        && pending->elapsed_time.tv_usec > 0) {
                    qEnque(spans, pending);
                } else {
                    beep_model_span_destroy(pending);
                }
                pending = NULL;
            }
        }
    }

    return spans;
}

bool beep_model_spans_has_station(QUEUE spans, json_object* station) {
    qForEach(BeepModelStationSpan*, span, spans) {
        if (json_object_equal(span->station, station)) {
            return true;
        }
    }
    return false;
}

// Return a station
QUEUE beep_model_history_query_lucky() {
    // Convert history to a list of (played_time, station) objects.
    // Choose one at random.
    QUEUE station_spans = beep_model_get_station_spans();

    // Last 5 stations that were played for longer than 10 minutes
    QUEUE long_enough = qMake();
    int count = 0;
    qForEachReverse(BeepModelStationSpan*, span, station_spans) {
        if (count < 5 && span->elapsed_time.tv_sec >= lucky_duration
                && !beep_model_spans_has_station(long_enough, span->station)) {
            qEnque(long_enough, span);
            count++;
        } else {
            beep_model_span_destroy(span);
        }
    }
    qClose(station_spans);

    // Flip it.
    QUEUE in_order = qMake();
    qForEachReverse(BeepModelStationSpan*, span, long_enough) {
        qEnque(in_order, span);
    }
    qClose(long_enough);

    return in_order;
}

void print_history(QUEUE spans) {
    beep_jsonfile_foreach(row, history) {
        printf("history row: %s", json_object_to_json_string(row));
        json_object_put(row);
    }
}

void print_spans(QUEUE spans) {
    qForEach(BeepModelStationSpan*, span, spans) {
        printf("start_time: %ld.%06ld elapsed_time: %ld.%06ld %s\n",
                span->start_time.tv_sec, span->start_time.tv_usec,
                span->elapsed_time.tv_sec, span->elapsed_time.tv_usec,
                json_object_to_json_string(span->station));
    }
}
