#include <stdlib.h>
#include <check.h>
#include <unistd.h>

#include "beep/debug.h"
#include "beep/json.h"
#include "beep/network.h"
#include "model.h"

#define FILE_PATH "/tmp/model.jsonfile"

void ck_assert_span_station_eq(
        int exp_app_id, int exp_command_id, int exp_station_id,
        BeepModelStationSpan* span) {
    if (!span) {
        ck_assert(false);
    }
    int app_id = json_object_get_int(
            json_object_object_get(span->station, "app"));
    int command_id = json_object_get_int(
            json_object_object_get(span->station, "command"));
    int station_id = json_object_get_int(
            json_object_object_get(
                json_object_object_get(span->station, "args"),
                "station_id"));
    ck_assert(exp_app_id == app_id);
    ck_assert(exp_command_id == command_id);
    ck_assert(exp_station_id == station_id);
}

json_object* get_station_json(int app_id, int command_id, int station_id) {
    json_object* station = json_object_new_object();
    json_object_object_add(station, "app", json_object_new_int(app_id));
    json_object_object_add(station, "command",
            json_object_new_int(command_id));

    json_object* args = json_object_new_object();
    json_object_object_add(args, "station_id",
            json_object_new_int(station_id));

    json_object_object_add(station, "args", args);

    json_object* state = json_object_new_object();
    json_object_object_add(state, "station", station);

    return state;
}

void add_change_station(int app_id, int command_id, int station_id) {
    json_object* state = get_station_json(app_id, command_id, station_id);

    beep_model_history_add(
            BEEP_ROLE_APP, BEEP_SUBROLE_NONE, "station_changed", state);

    json_object_put(state);
}

void add_pause() {
    json_object* state = json_object_new_object();
    json_object_object_add(state,
            "playing", json_object_new_boolean(false));
    beep_model_history_add(
            BEEP_ROLE_APP, BEEP_SUBROLE_NONE, "play_pause", state);
    json_object_put(state);
}

void add_resume() {
    json_object* state = json_object_new_object();
    json_object_object_add(state,
            "playing", json_object_new_boolean(true));
    beep_model_history_add(
            BEEP_ROLE_APP, BEEP_SUBROLE_NONE, "play_pause", state);
    json_object_put(state);
}

START_TEST (test_beep_model_get_station_spans) {
    QUEUE spans;

    ///// resume same station
    beep_jsonfile_remove(FILE_PATH);
    beep_model_history_init(FILE_PATH, 0);
    add_change_station(0, 0, 0);
    add_pause();
    add_resume();
    add_pause();
    spans = beep_model_get_station_spans();

    ck_assert_int_eq(4, beep_jsonfile_num_rows(history));
    ck_assert_int_eq(2, qLen(spans));

    ck_assert_span_station_eq(0, 0, 0, qNth(spans, 0));
    ck_assert_span_station_eq(0, 0, 0, qNth(spans, 1));


    ///// station followed by station
    beep_jsonfile_remove(FILE_PATH);
    beep_model_history_init(FILE_PATH, 0);
    add_change_station(0, 0, 0);
    add_change_station(0, 0, 1);
    add_pause();

    spans = beep_model_get_station_spans();

    ck_assert_int_eq(3, beep_jsonfile_num_rows(history));
    ck_assert_int_eq(2, qLen(spans));

    ck_assert_span_station_eq(0, 0, 0, qNth(spans, 0));
    ck_assert_span_station_eq(0, 0, 1, qNth(spans, 1));


    ///// pause/resume before station
    beep_jsonfile_remove(FILE_PATH);
    beep_model_history_init(FILE_PATH, 0);
    add_pause();
    add_resume();
    add_change_station(0, 0, 0);
    add_pause();

    spans = beep_model_get_station_spans();

    ck_assert_int_eq(4, beep_jsonfile_num_rows(history));
    ck_assert_int_eq(1, qLen(spans));

    ck_assert_span_station_eq(0, 0, 0, qNth(spans, 0));
}
END_TEST

START_TEST (test_beep_model_heartbeat) {
    QUEUE spans;

    ///// start a station and write a heartbeat
    beep_jsonfile_remove(FILE_PATH);
    beep_model_history_init(FILE_PATH, 0);
    add_change_station(0, 0, 0);
    add_change_station(0, 0, 1);

    // Annoying.
    sleep(1);
    beep_model_history_write_heartbeat();
    // This should be discarded because there's no following heartbeat.
    add_change_station(0, 0, 3);
    spans = beep_model_get_station_spans();

    ck_assert_int_eq(3, beep_jsonfile_num_rows(history));
    ck_assert_int_eq(2, qLen(spans));
    ck_assert_span_station_eq(0, 0, 0, qNth(spans, 0));

    // init again, we should pick old data and heartbeat
    beep_model_history_init(FILE_PATH, 0);
    add_change_station(0, 0, 4);
    spans = beep_model_get_station_spans();

    ck_assert_int_eq(5, beep_jsonfile_num_rows(history));
    ck_assert_int_eq(2, qLen(spans));
    ck_assert_span_station_eq(0, 0, 0, qNth(spans, 0));
    ck_assert_span_station_eq(0, 0, 1, qNth(spans, 1));
}
END_TEST

START_TEST (test_beep_model_feeling_lucky) {
    beep_jsonfile_remove(FILE_PATH);
    beep_model_history_init(FILE_PATH, 0);
    add_change_station(0, 0, 0);
    add_change_station(0, 0, 1);
    add_change_station(0, 0, 2);
    add_change_station(0, 0, 3);
    add_change_station(0, 0, 4);
    add_change_station(0, 0, 5);
    add_change_station(0, 0, 7);
    add_change_station(0, 0, 6);
    add_change_station(0, 0, 7);
    add_pause();

    QUEUE spans = beep_model_history_query_lucky();
    ck_assert_span_station_eq(0, 0, 3, qNth(spans, 0));
    ck_assert_span_station_eq(0, 0, 4, qNth(spans, 1));
    ck_assert_span_station_eq(0, 0, 5, qNth(spans, 2));
    ck_assert_span_station_eq(0, 0, 6, qNth(spans, 3));
    ck_assert_span_station_eq(0, 0, 7, qNth(spans, 4));

    // Short
    beep_jsonfile_remove(FILE_PATH);
    beep_model_history_init(FILE_PATH, 0);
    add_change_station(0, 0, 0);
    add_change_station(0, 0, 1);
    add_change_station(0, 0, 2);

    spans = beep_model_history_query_lucky();
    ck_assert_span_station_eq(0, 0, 0, qNth(spans, 0));
    ck_assert_span_station_eq(0, 0, 1, qNth(spans, 1));

    // Real short (0 spans)
    beep_jsonfile_remove(FILE_PATH);
    beep_model_history_init(FILE_PATH, 0);
    add_change_station(0, 0, 0);

    spans = beep_model_history_query_lucky();
    ck_assert_int_eq(0, qLen(spans));
}
END_TEST

// TODO: move this to a file.
const char* history_data = "\
{ \"time_s\": 1367875680, \"time_us\": 617522, \"event_type\": 0, \"event_data\": { \"app\": 1, \"command\": 5, \"args\": { \"station_id\": \"85572670766892182\" } } }\n\
{ \"time_s\": 1367875678, \"time_us\": 0, \"event_type\": 3, \"event_data\": null }\n\
{ \"time_s\": 1367875788, \"time_us\": 438821, \"event_type\": 0, \"event_data\": { \"app\": 1, \"command\": 5, \"args\": { \"station_id\": \"52680561378641046\" } } }\n\
{ \"time_s\": 1367875772, \"time_us\": 0, \"event_type\": 3, \"event_data\": null }\n\
{ \"time_s\": 1367875989, \"time_us\": 956028, \"event_type\": 0, \"event_data\": { \"app\": 1, \"command\": 5, \"args\": { \"station_id\": \"85572670766892182\" } } }\n\
{ \"time_s\": 1367877065, \"time_us\": 0, \"event_type\": 3, \"event_data\": null }\n\
{ \"time_s\": 1367953654, \"time_us\": 371118, \"event_type\": 0, \"event_data\": { \"app\": 1, \"command\": 5, \"args\": { \"station_id\": \"85572670766892182\" } } }\n\
{ \"time_s\": 1367954185, \"time_us\": 0, \"event_type\": 3, \"event_data\": null }\n\
{ \"time_s\": 1367954727, \"time_us\": 356376, \"event_type\": 0, \"event_data\": { \"app\": 1, \"command\": 5, \"args\": { \"station_id\": \"77277697348912278\" } } }\n\
{ \"time_s\": 1367954723, \"time_us\": 0, \"event_type\": 3, \"event_data\": null }\n\
{ \"time_s\": 1367967998, \"time_us\": 333539, \"event_type\": 0, \"event_data\": { \"app\": 1, \"command\": 5, \"args\": { \"station_id\": \"85572670766892182\" } } }\n\
{ \"time_s\": 1367968013, \"time_us\": 473097, \"event_type\": 0, \"event_data\": { \"app\": 1, \"command\": 5, \"args\": { \"station_id\": \"85572670766892182\" } } }\n\
{ \"time_s\": 1367968044, \"time_us\": 920744, \"event_type\": 0, \"event_data\": { \"app\": 1, \"command\": 5, \"args\": { \"station_id\": \"85572670766892182\" } } }\n\
{ \"time_s\": 1367968533, \"time_us\": 0, \"event_type\": 3, \"event_data\": null }\n\
{ \"time_s\": 1367969023, \"time_us\": 0, \"event_type\": 3, \"event_data\": null }\n\
{ \"time_s\": 1367969035, \"time_us\": 861409, \"event_type\": 0, \"event_data\": { \"app\": 1, \"command\": 5, \"args\": { \"station_id\": \"28269062484837526\" } } }\n\
{ \"time_s\": 1367969090, \"time_us\": 318333, \"event_type\": 0, \"event_data\": { \"app\": 1, \"command\": 5, \"args\": { \"station_id\": \"85572670766892182\" } } }\n\
{ \"time_s\": 1367969106, \"time_us\": 237016, \"event_type\": 0, \"event_data\": { \"app\": 1, \"command\": 5, \"args\": { \"station_id\": \"85572670766892182\" } } }\n\
{ \"time_s\": 1367969113, \"time_us\": 998503, \"event_type\": 0, \"event_data\": { \"app\": 1, \"command\": 5, \"args\": { \"station_id\": \"28269062484837526\" } } }\n\
{ \"time_s\": 1367969167, \"time_us\": 960508, \"event_type\": 0, \"event_data\": { \"app\": 1, \"command\": 5, \"args\": { \"station_id\": \"28269062484837526\" } } }\n";

// This test doesn't check anything
START_TEST (test_beep_model_feeling_lucky_file) {
    beep_jsonfile_remove(FILE_PATH);

    FILE* f = fopen(FILE_PATH, "w");
    fwrite(history_data, strlen(history_data), 1, f);
    fclose(f);

    beep_model_history_init(FILE_PATH, 0);
    beep_model_history_write_heartbeat();

    //QUEUE spans = beep_model_history_query_lucky();
    QUEUE spans = beep_model_get_station_spans();
    print_spans(spans);
}
END_TEST

// TODO: move this to its own test.
START_TEST (test_json_object_equal) {
    json_object* station1;
    json_object* station2;
    station1 = json_object_new_string("Hello");
    station2 = json_object_new_string("Hello");
    ck_assert(json_object_equal(station1, station2));

    station2 = json_object_new_string("Hello2");
    ck_assert(!json_object_equal(station1, station2));

    station1 = get_station_json(1, 2, 3);
    station2 = get_station_json(1, 2, 3);
    ck_assert(json_object_equal(station1, station2));

    station1 = get_station_json(1, 2, 3);
    station2 = get_station_json(1, 2, 4);
    ck_assert(!json_object_equal(station1, station2));

    station1 = get_station_json(1, 2, 3);
    station2 = get_station_json(5, 2, 3);
    ck_assert(!json_object_equal(station1, station2));
}
END_TEST

Suite * model_suite (void) {
    Suite *s = suite_create ("Model");

    /* Core test case */
    TCase *tc_core = tcase_create ("Core");
    tcase_add_test (tc_core, test_beep_model_get_station_spans);
    tcase_add_test (tc_core, test_beep_model_heartbeat);
    tcase_add_test (tc_core, test_beep_model_feeling_lucky);
    tcase_add_test (tc_core, test_beep_model_feeling_lucky_file);
    tcase_add_test (tc_core, test_json_object_equal);
    suite_add_tcase (s, tc_core);

    return s;
}

int main (void) {
    log_beep_main = LOG_CATEGORY_GET("model_test");
    log_category_set_priority (log_beep_main, LOG_PRIORITY_DEBUG);

    int number_failed;
    Suite *s = model_suite ();
    SRunner *sr = srunner_create (s);
    srunner_run_all (sr, CK_NORMAL);
    number_failed = srunner_ntests_failed (sr);
    srunner_free (sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
