#include <stdio.h>

#include "beep/flags.h"

struct flag_vals {
  int number_of_tacos;
  char* filling;
  long number_of_burritos;
  long long sauces;
};
struct flag_vals flag_test_lib_flags;

static const BeepFlag flags[] = {
    BEEP_FLAG("number_of_tacos", BEEP_FLAG_INT,
            &flag_test_lib_flags.number_of_tacos, NULL, NULL),
    BEEP_FLAG("filling", BEEP_FLAG_STRING,
            &flag_test_lib_flags.filling, NULL, NULL),
    BEEP_FLAG("number-of-burritos", BEEP_FLAG_LONG,
            &flag_test_lib_flags.number_of_burritos, "BURRITOS",
            "how many burritos you have eaten in\n"
            "            your life"),
    BEEP_FLAG("types-of-sauces", BEEP_FLAG_LONGLONG,
            &flag_test_lib_flags.sauces, NULL,
            "sauces you use")
};

BEEP_INIT_FLAGS(flags);

void lib_print_flags(void) {
    printf("Num tacos: %d\nFilling: %s\nNum burritos: %ld\nNum sauce: %lld\n",
        flag_test_lib_flags.number_of_tacos,
        flag_test_lib_flags.filling,
        flag_test_lib_flags.number_of_burritos,
        flag_test_lib_flags.sauces);
}
