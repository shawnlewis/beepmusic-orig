#include <stdio.h>
#include <stdlib.h>

#include "beeplib.h"
#include "beep/debug.h"

typedef struct {
    int a;
    char b;
} Val;

void vector_print(BeepStaticVector* v) {
    for (int i=0; i<v->num_elements; i++) {
        Val* val = beep_vector_index(v, i);
        LOG_DEBUG(log_beep_main, "%d %c", val->a, val->b);
    }
    LOG_DEBUG(log_beep_main, "\n");
}

void beep_str_replace_one_test(char* target, char* find, char* replace) {
    char* res = beep_str_replace_one(target, find, replace);
    if (res == NULL) {
        LOG_DEBUG(log_beep_main, "Result was NULL");
    } else {
        LOG_DEBUG(log_beep_main, "Result: %s", res);
        free(res);
    }
}

int main(int argc, char** argv) {
    BeepStaticVector* v = beep_new_vector(10, sizeof(Val));
    Val val = {
        .a = 0,
        .b = 'a'
    };
    val.a = 0; beep_vector_append(v, &val);
    val.a = 1; beep_vector_append(v, &val);
    val.a = 2; beep_vector_append(v, &val);
    val.a = 3; beep_vector_append(v, &val);
    val.a = 4; beep_vector_append(v, &val);
    val.a = 5; beep_vector_append(v, &val);
    val.a = 6; beep_vector_append(v, &val);
    val.a = 7; beep_vector_append(v, &val);
    val.a = 8; beep_vector_append(v, &val);
    val.a = 9; beep_vector_append(v, &val);
    val.a = 10; beep_vector_append(v, &val);

    vector_print(v);
    beep_vector_pop(v, 3);
    vector_print(v);
    beep_vector_pop(v, 6);
    beep_vector_pop(v, 6);
    vector_print(v);
    beep_vector_pop(v, 6);
    beep_vector_pop(v, 6);
    beep_vector_pop(v, 6);
    vector_print(v);
    beep_vector_pop(v, 0);
    beep_vector_pop(v, 0);
    beep_vector_pop(v, 0);
    beep_vector_pop(v, 0);
    beep_vector_pop(v, 0);
    vector_print(v);
    beep_vector_pop(v, 0);
    vector_print(v);
    beep_vector_pop(v, 0);
    beep_vector_pop(v, 0);
    beep_vector_pop(v, 0);


    beep_str_replace_one_test(
            "beginning middle end", "beginning", "pre");
    beep_str_replace_one_test(
            "beginning middle end", "end", "post");
    beep_str_replace_one_test(
            "beginning middle end", "g middle e", "***");
    beep_str_replace_one_test(
            "can't find me", "where are you", "gotcha!");

    LOG_DEBUG(log_beep_main, "%s", beep_slurp("./upnpweb/beep.xml"));

    return EXIT_SUCCESS;
}
