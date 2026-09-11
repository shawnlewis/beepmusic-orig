#include <stdlib.h>
#include <check.h>

#include "beep/debug.h"
#include "jsonfile.h"


#define FILE_PATH "/tmp/beepjson"

START_TEST (test_beep_jsonfile_metadata) {
    int res;
    beep_jsonfile_remove(FILE_PATH);
    BeepJsonFile* f = beep_jsonfile_init(FILE_PATH);

    ck_assert_int_eq(-1, beep_jsonfile_read_metadata_int(f, "test key", &res));

    beep_jsonfile_write_metadata_int(f, "test key", 925125);
    ck_assert_int_eq(0, beep_jsonfile_read_metadata_int(f, "test key", &res));
    ck_assert_int_eq(925125, res);

    ck_assert_int_eq(-1,
            beep_jsonfile_read_metadata_int(f, "not present", &res));

    beep_jsonfile_write_metadata_int(f, "2nd key", 4);
    ck_assert_int_eq(0, beep_jsonfile_read_metadata_int(f, "2nd key", &res));
    ck_assert_int_eq(4, res);

    beep_jsonfile_write_metadata_int(f, "test key", 8);
    ck_assert_int_eq(0, beep_jsonfile_read_metadata_int(f, "test key", &res));
    ck_assert_int_eq(8, res);
}
END_TEST

START_TEST (test_beep_jsonfile_rows) {
    beep_jsonfile_remove(FILE_PATH);
    BeepJsonFile* f = beep_jsonfile_init(FILE_PATH);
    int i;

    i = 0;
    beep_jsonfile_foreach(row, f) {
        i++;
    }
    ck_assert_int_eq(0, i);

    json_object* row;

    row = json_object_new_object();
    json_object_object_add(row, "row_val", json_object_new_int(125));
    beep_jsonfile_add_row(f, row);

    i = 0;
    beep_jsonfile_foreach(row, f) {
        i++;
    }
    ck_assert_int_eq(1, i);

    row = json_object_new_object();
    json_object_object_add(row, "row_val", json_object_new_int(1));
    json_object_object_add(row, "row_val2", json_object_new_int(13));
    beep_jsonfile_add_row(f, row);

    i = 0;
    beep_jsonfile_foreach(row, f) {
        if (i == 0) {
            ck_assert_int_eq(125,
                    json_object_get_int(
                        json_object_object_get(row, "row_val")));
        } else if (i == 1) {
            ck_assert_int_eq(1,
                    json_object_get_int(
                        json_object_object_get(row, "row_val")));
            ck_assert_int_eq(13,
                    json_object_get_int(
                        json_object_object_get(row, "row_val2")));
        }
        i++;
    }
    ck_assert_int_eq(2, i);
}
END_TEST

Suite * jsonfile_suite (void) {
    Suite *s = suite_create ("Jsonfile");

    /* Core test case */
    TCase *tc_core = tcase_create ("Core");
    tcase_add_test (tc_core, test_beep_jsonfile_metadata);
    tcase_add_test (tc_core, test_beep_jsonfile_rows);
    suite_add_tcase (s, tc_core);

    return s;
}

int main (void) {
    log_beep_main = LOG_CATEGORY_GET("jsonfile_test");
    log_category_set_priority (log_beep_main, LOG_PRIORITY_DEBUG);

    int number_failed;
    Suite *s = jsonfile_suite ();
    SRunner *sr = srunner_create (s);
    srunner_run_all (sr, CK_NORMAL);
    number_failed = srunner_ntests_failed (sr);
    srunner_free (sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
