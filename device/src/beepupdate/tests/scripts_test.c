#include <check.h>

#include "beepupdate.h"

// Note: vim will add a new line to the end of files unless using
// :set binary :set noeol.

static char simple_shell_script[] = \
    "# 0:a9c58d3cd2aef5f3c3591419c26b584e\n"
    "echo \"hello world\"\n"
    "echo $#\n"
    "echo $*\n";
static size_t simple_shell_script_size = sizeof(simple_shell_script) - 1;

static char exit_2_shell_script[] = \
    "# 0:d645a34778a38d32a77e9eb83d3370fc\n"
    "echo \"hello world\"\n"
    "echo $#\n"
    "echo $*\n"
    "echo \"exiting with code 2\"\n"
    "exit 2\n";
static size_t exit_2_shell_script_size = sizeof(exit_2_shell_script) - 1;

static char crash_shell_script[] = \
    "# 0:3503b80d1845ebd4b70da0ed24ce4f22\n"
    "echo \"hello world\"\n"
    "echo $#\n"
    "echo $*\n"
    "./beepupdate_test --seg\n"
    "rc=$?\n"
    "echo \"seg fault should have happened\"\n"
    "exit $rc\n";
static size_t crash_shell_script_size = sizeof(crash_shell_script) - 1;

static char sha256_shell_script[] = \
    "# 2:20e645dced3e7ded1e8305af4c5b602c564b95bd3b969497d1ca97ffb1938086\n"
    "echo \"hello world\"\n"
    "echo $#\n"
    "echo $*\n";
static size_t sha256_shell_script_size = sizeof(sha256_shell_script) - 1;

static char simple_lua_script[] = \
    "-- 0:3773a3c3342546321e8b42fe694a4978\n"
    "print(\"hello world\")\n"
    "print(#arg)\n"
    "print(unpack(arg))\n";
static size_t simple_lua_script_size = sizeof(simple_lua_script) - 1;

static char exit_2_lua_script[] = \
    "-- 0:0ff135af7aec26e14a32bcfb4f93dc73\n"
    "print(\"hello world\")\n"
    "print(#arg)\n"
    "print(unpack(arg))\n"
    "os.exit(2)\n";
static size_t exit_2_lua_script_size = sizeof(exit_2_lua_script) - 1;

static char crash_lua_script[] = \
    "-- 0:8128bb2000432b70836d9a1ed37a60ec\n"
    "print(\"hello world\")\n"
    "print(#arg)\n"
    "print(unpack(arg))\n"
    "local rc = os.execute(\"./beepupdate_test --seg\")\n"
    "if rc ~= 0 then\n"
    "    os.exit(1)\n"
    "else\n"
    "    os.exit(0)\n"
    "end\n";
static size_t crash_lua_script_size = sizeof(crash_lua_script) - 1;

static char sha512_lua_script[] = \
    "-- 3:944c9d93a6345761c337a1b467bdbdbca70cee1bc53dbf9e33cedbbab8e8c4200a1e111c1b556314a070f22af466091796f7bf341eeab36b19fa67e3891ff234\n"
    "print(\"hello world\")\n"
    "print(#arg)\n"
    "print(unpack(arg))\n";
static size_t sha512_lua_script_size = sizeof(sha512_lua_script) - 1;

START_TEST(run_shell_script_file_test) {
    FILE *s;
    char *file;
    UpdateCode code;
    int status = -1;

    // Check bad input.
    code = run_shell_script_file(NULL, &status);
    ck_assert_int_eq(UPDATE_BAD_ARG, code);

    // Check 0 size.
    s = tmpfopen("w");
    ck_assert(s != NULL);
    file = tmpfpath(s);
    code = run_shell_script_file(file, &status);
    ck_assert_int_eq(UPDATE_ERR, code);
    tmpfunclose(s);
    bfree(file);

    // Check size < digest size.
    s = tmpfopen("w");
    ck_assert(s != NULL);
    fwrite(simple_shell_script, 1, 30, s);
    fflush(s);
    file = tmpfpath(s);
    code = run_shell_script_file(file, &status);
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);
    tmpfunclose(s);
    bfree(file);

    // Check bad prefix.
    s = tmpfopen("w");
    ck_assert(s != NULL);
    simple_shell_script[0]++;
    fwrite(simple_shell_script, 1, simple_shell_script_size, s);
    simple_shell_script[0]--;
    fflush(s);
    file = tmpfpath(s);
    code = run_shell_script_file(file, &status);
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);
    tmpfunclose(s);
    bfree(file);

    // Check bad digest.
    s = tmpfopen("w");
    ck_assert(s != NULL);
    simple_shell_script[4]++;
    fwrite(simple_shell_script, 1, simple_shell_script_size, s);
    simple_shell_script[4]--;
    fflush(s);
    file = tmpfpath(s);
    code = run_shell_script_file(file, &status);
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);
    tmpfunclose(s);
    bfree(file);

    // Check bad script.
    s = tmpfopen("w");
    ck_assert(s != NULL);
    simple_shell_script[simple_shell_script_size - 1]++;
    fwrite(simple_shell_script, 1, simple_shell_script_size, s);
    simple_shell_script[simple_shell_script_size - 1]--;
    fflush(s);
    file = tmpfpath(s);
    code = run_shell_script_file(file, &status);
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);
    tmpfunclose(s);
    bfree(file);

    //// Check running with status = NULL.
    s = tmpfopen("w");
    ck_assert(s != NULL);
    fwrite(simple_shell_script, 1, simple_shell_script_size, s);
    fflush(s);
    file = tmpfpath(s);
    code = run_shell_script_file(file, NULL);
    ck_assert_int_eq(UPDATE_OK, code);

    code = run_shell_script_file(file, &status);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(status, 0);
    tmpfunclose(s);
    bfree(file);

    s = tmpfopen("w");
    ck_assert(s != NULL);
    fwrite(exit_2_shell_script, 1, exit_2_shell_script_size, s);
    fflush(s);
    file = tmpfpath(s);
    code = run_shell_script_file(file, &status);
    ck_assert_int_eq(UPDATE_SCRIPT_NONZERO, code);
    ck_assert_int_eq(status, 2);
    tmpfunclose(s);
    bfree(file);

    s = tmpfopen("w");
    ck_assert(s != NULL);
    fwrite(crash_shell_script, 1, crash_shell_script_size, s);
    fflush(s);
    file = tmpfpath(s);
    code = run_shell_script_file(file, &status);
    ck_assert_int_eq(UPDATE_SCRIPT_NONZERO, code);
    ck_assert_int_ne(status, 0);
    tmpfunclose(s);
    bfree(file);

    s = tmpfopen("w");
    ck_assert(s != NULL);
    fwrite(sha256_shell_script, 1, sha256_shell_script_size, s);
    fflush(s);
    file = tmpfpath(s);
    code = run_shell_script_file(file, &status);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(status, 0);
    tmpfunclose(s);
    bfree(file);
}
END_TEST

START_TEST(run_shell_script_mem_test) {
    UpdateCode code;
    int status = -1;

    // Check bad input.
    code = run_shell_script_mem(NULL, 1, &status);
    ck_assert_int_eq(UPDATE_BAD_ARG, code);

    // Check 0 size.
    code = run_shell_script_mem(simple_shell_script, 0, &status);
    ck_assert_int_eq(UPDATE_BAD_ARG, code);

    // Check size < digest size.
    code = run_shell_script_mem(simple_shell_script, 30, &status);
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);

    // Check bad prefix.
    simple_shell_script[0]++;
    code = run_shell_script_mem(simple_shell_script, simple_shell_script_size, NULL);
    simple_shell_script[0]--;
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);

    // Check bad digest.
    simple_shell_script[4]++;
    code = run_shell_script_mem(simple_shell_script, simple_shell_script_size, NULL);
    simple_shell_script[4]--;
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);

    // Check bad script.
    simple_shell_script[simple_shell_script_size - 1]++;
    code = run_shell_script_mem(simple_shell_script, simple_shell_script_size, NULL);
    simple_shell_script[simple_shell_script_size - 1]--;
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);

    // Check running with status = NULL.
    code = run_shell_script_mem(simple_shell_script, simple_shell_script_size, NULL);
    ck_assert_int_eq(UPDATE_OK, code);

    code = run_shell_script_mem(simple_shell_script, simple_shell_script_size, &status);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(status, 0);

    code = run_shell_script_mem(exit_2_shell_script, exit_2_shell_script_size, &status);
    ck_assert_int_eq(UPDATE_SCRIPT_NONZERO, code);
    ck_assert_int_eq(status, 2);

    code = run_shell_script_mem(crash_shell_script, crash_shell_script_size, &status);
    ck_assert_int_eq(UPDATE_SCRIPT_NONZERO, code);
    ck_assert_int_ne(status, 0);

    code = run_shell_script_mem(sha256_shell_script, sha256_shell_script_size, &status);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(status, 0);
}
END_TEST

START_TEST(run_lua_script_file_test) {
    FILE *s;
    char *file;
    UpdateCode code;
    int status = -1;

    // Check bad input.
    code = run_lua_script_file(NULL, &status);
    ck_assert_int_eq(UPDATE_BAD_ARG, code);

    // Check 0 size.
    s = tmpfopen("w");
    ck_assert(s != NULL);
    file = tmpfpath(s);
    code = run_lua_script_file(file, &status);
    ck_assert_int_eq(UPDATE_ERR, code);
    tmpfunclose(s);
    bfree(file);

    // Check size < digest size.
    s = tmpfopen("w");
    ck_assert(s != NULL);
    fwrite(simple_lua_script, 1, 30, s);
    fflush(s);
    file = tmpfpath(s);
    code = run_lua_script_file(file, &status);
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);
    tmpfunclose(s);
    bfree(file);

    // Check bad prefix.
    s = tmpfopen("w");
    ck_assert(s != NULL);
    simple_lua_script[0]++;
    fwrite(simple_lua_script, 1, simple_lua_script_size, s);
    simple_lua_script[0]--;
    fflush(s);
    file = tmpfpath(s);
    code = run_lua_script_file(file, &status);
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);
    tmpfunclose(s);
    bfree(file);

    // Check bad digest.
    s = tmpfopen("w");
    ck_assert(s != NULL);
    simple_lua_script[5]++;
    fwrite(simple_lua_script, 1, simple_lua_script_size, s);
    simple_lua_script[5]--;
    fflush(s);
    file = tmpfpath(s);
    code = run_lua_script_file(file, &status);
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);
    tmpfunclose(s);
    bfree(file);

    // Check bad script.
    s = tmpfopen("w");
    ck_assert(s != NULL);
    simple_lua_script[simple_lua_script_size - 1]++;
    fwrite(simple_lua_script, 1, simple_lua_script_size, s);
    simple_lua_script[simple_lua_script_size - 1]--;
    fflush(s);
    file = tmpfpath(s);
    code = run_lua_script_file(file, &status);
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);
    tmpfunclose(s);
    bfree(file);

    //// Check running with status = NULL.
    s = tmpfopen("w");
    ck_assert(s != NULL);
    fwrite(simple_lua_script, 1, simple_lua_script_size, s);
    fflush(s);
    file = tmpfpath(s);
    code = run_lua_script_file(file, NULL);
    ck_assert_int_eq(UPDATE_OK, code);

    code = run_lua_script_file(file, &status);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(status, 0);
    tmpfunclose(s);
    bfree(file);

    s = tmpfopen("w");
    ck_assert(s != NULL);
    fwrite(exit_2_lua_script, 1, exit_2_lua_script_size, s);
    fflush(s);
    file = tmpfpath(s);
    code = run_lua_script_file(file, &status);
    ck_assert_int_eq(UPDATE_SCRIPT_NONZERO, code);
    ck_assert_int_eq(status, 2);
    tmpfunclose(s);
    bfree(file);

    s = tmpfopen("w");
    ck_assert(s != NULL);
    fwrite(crash_lua_script, 1, crash_lua_script_size, s);
    fflush(s);
    file = tmpfpath(s);
    code = run_lua_script_file(file, &status);
    ck_assert_int_eq(UPDATE_SCRIPT_NONZERO, code);
    ck_assert_int_ne(status, 0);
    tmpfunclose(s);
    bfree(file);

    s = tmpfopen("w");
    ck_assert(s != NULL);
    fwrite(sha512_lua_script, 1, sha512_lua_script_size, s);
    fflush(s);
    file = tmpfpath(s);
    code = run_lua_script_file(file, &status);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(status, 0);
    tmpfunclose(s);
    bfree(file);
}
END_TEST

START_TEST(run_lua_script_mem_test) {
    UpdateCode code;
    int status = -1;

    // Check bad input.
    code = run_lua_script_mem(NULL, 1, &status);
    ck_assert_int_eq(UPDATE_BAD_ARG, code);

    // Check 0 size.
    code = run_lua_script_mem(simple_lua_script, 0, &status);
    ck_assert_int_eq(UPDATE_BAD_ARG, code);

    // Check size < digest size.
    code = run_lua_script_mem(simple_lua_script, 30, &status);
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);

    // Check bad prefix.
    simple_lua_script[0]++;
    code = run_lua_script_mem(simple_lua_script, simple_lua_script_size, NULL);
    simple_lua_script[0]--;
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);

    // Check bad digest.
    simple_lua_script[5]++;
    code = run_lua_script_mem(simple_lua_script, simple_lua_script_size, NULL);
    simple_lua_script[5]--;
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);

    // Check bad script.
    simple_lua_script[simple_lua_script_size - 1]++;
    code = run_lua_script_mem(simple_lua_script, simple_lua_script_size, NULL);
    simple_lua_script[simple_lua_script_size - 1]--;
    ck_assert_int_eq(UPDATE_DIG_FAIL, code);

    // Check running with status = NULL.
    code = run_lua_script_mem(simple_lua_script, simple_lua_script_size, NULL);
    ck_assert_int_eq(UPDATE_OK, code);

    code = run_lua_script_mem(simple_lua_script, simple_lua_script_size, &status);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(status, 0);

    code = run_lua_script_mem(exit_2_lua_script, exit_2_lua_script_size, &status);
    ck_assert_int_eq(UPDATE_SCRIPT_NONZERO, code);
    ck_assert_int_eq(status, 2);

    code = run_lua_script_mem(crash_lua_script, crash_lua_script_size, &status);
    ck_assert_int_eq(UPDATE_SCRIPT_NONZERO, code);
    ck_assert_int_ne(status, 0);

    code = run_lua_script_mem(sha512_lua_script, sha512_lua_script_size, &status);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(status, 0);
}
END_TEST


Suite *scripts_suite(void) {
    Suite *s = suite_create("scripts");

    TCase *tc_core = tcase_create("core");
    tcase_add_test(tc_core, run_shell_script_file_test);
    tcase_add_test(tc_core, run_shell_script_mem_test);
    tcase_add_test(tc_core, run_lua_script_file_test);
    tcase_add_test(tc_core, run_lua_script_mem_test);
    suite_add_tcase(s, tc_core);

    return s;
}

void cleanup_scripts_suite(void) {
}
