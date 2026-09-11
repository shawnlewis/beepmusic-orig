#include <stdbool.h>
#include <stdio.h>

#include "beep/debug.h"
#include "beep/flags.h"

struct flag_vals {
  int dog_count;
  bool run_away;
};

// With default values;
struct flag_vals flag_test_main_flags = {2, 0};

static const BeepFlag flags[] = {
    BEEP_FLAG("dog_count", BEEP_FLAG_INT, &flag_test_main_flags.dog_count,
            "DOGS", "count some dogs"),
    BEEP_FLAG("run_away", BEEP_FLAG_BOOL, &flag_test_main_flags.run_away, NULL, NULL)
};

BEEP_INIT_FLAGS(flags);

extern void lib_print_flags(void);

void main_print_flags(void) {
    printf("Dog count: %d\nRun away?: %d\n",
        flag_test_main_flags.dog_count,
        flag_test_main_flags.run_away);
}

int main(int argc, char** argv) {
    log_beep_main = LOG_CATEGORY_GET("flags_test_main");
    log_category_set_priority (log_beep_main, LOG_PRIORITY_DEBUG);

    int horses = 1;
    bool happy_horse = false;
    BeepFlag rt_flags[2] = {
        BEEP_FLAG("horses", BEEP_FLAG_INT, &horses, "HORSES",
                "Pancho Villa's happy horses"),
        BEEP_FLAG("happy_horse", BEEP_FLAG_BOOL, &happy_horse, NULL,
                "Is his horse smiling")
    };

    beep_flags_init_with_flags(argc, argv, rt_flags,
            sizeof(rt_flags)/sizeof(BeepFlag));

    lib_print_flags();
    main_print_flags();

    printf("horses: %d\n", horses);
    printf("happy_horse: %d\n", happy_horse);
    if (!happy_horse)
        printf("WRONG: Pancho Villa's horse is always smiling!\n");

    return 0;
}
