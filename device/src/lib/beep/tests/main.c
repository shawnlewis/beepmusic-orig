#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include <check.h>

#define DECLARE_SUITE(__SUITE__) \
    Suite *__SUITE__ ## _suite(void); \
    void cleanup_ ## __SUITE__ ## _suite(void)

DECLARE_SUITE(beeplib);

Suite *(*suitep[])(void) = {
    beeplib_suite,
};

void (*donep[])(void) = {
    cleanup_beeplib_suite,
};

int main(int argc, char **argv) {
    int config_argc = 1;
    char **config_argv;
    int failed;
    int i;
    bool arg_copy = false;
    bool verbose = false;
    bool no_fork = false;

    // Last argv will already be NULL (important for getopt).
    config_argv = (char **)malloc((argc + 1) * sizeof(char *));
    assert(config_argv);
    config_argv[0] = argv[0];

    for (i = 1; i < argc; i++) {
        if (!arg_copy) {
            if (!strcmp("--verbose", argv[i])) {
                verbose = true;
            } else if (!strcmp("--no-fork", argv[i])) {
                no_fork = true;
            } else if (!strcmp("--seg", argv[i])) {
                char *bad = NULL;
                *bad = 1;
            } else if (!strcmp("args", argv[i])) {
                arg_copy = true;
                continue;
            } else {
                arg_copy = true;
            }
        }
        if (arg_copy) {
            config_argv[config_argc++] = argv[i];
        }
    }

    SRunner *sr = srunner_create(suitep[0]());
    for (i = 1; i < sizeof(suitep)/sizeof(suitep[0]); i++)
        srunner_add_suite(sr, suitep[i]());

    srunner_set_fork_status(sr, no_fork ? CK_NOFORK : CK_FORK);
    srunner_run_all(sr, verbose ? CK_VERBOSE : CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    for (i = 0; i < sizeof(donep)/sizeof(donep[0]); i++)
        donep[i]();

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
