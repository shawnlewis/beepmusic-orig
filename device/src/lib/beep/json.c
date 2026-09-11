#include "debug.h"
#include "json.h"

#include <stdlib.h>
#include <string.h>

int json_object_len(json_object* obj) {
    int i = 0;
    json_object_object_foreach(obj, key, val) {
        (void) key; // supress gcc set but not used warning for key
        i++;
    }
    return i;
}

bool json_object_equal(json_object* obj1, json_object* obj2) {
    //printf("OBJ1: %s\n", json_object_to_json_string(obj1));
    //printf("OBJ2: %s\n", json_object_to_json_string(obj2));
    if (obj1 == obj2) {
        return true;
    }

    int type1 = json_object_get_type(obj1);
    int type2 = json_object_get_type(obj2);
    if (type1 != type2) {
        return false;
    }

    switch (type1) {
    case json_type_null:
        return true;
    case json_type_boolean:
        return json_object_get_boolean(obj1)
                == json_object_get_boolean(obj2);
    case json_type_double:
        return json_object_get_double(obj1)
                == json_object_get_double(obj2);
    case json_type_int:
        return json_object_get_int(obj1)
                == json_object_get_int(obj2);
    case json_type_string:
        return strcmp(
                json_object_get_string(obj1),
                json_object_get_string(obj2)) == 0;
    case json_type_object: {
        if (json_object_len(obj1) != json_object_len(obj2)) {
            return false;
        }
        bool eq = true;
        json_object_object_foreach(obj1, key, val) {
            if (!json_object_equal(val, json_object_object_get(obj2, key))) {
                eq = false;
                break;
            }
        }
        return eq;
    }
    default:
        LOG_ERROR(log_beep_main,
                "json object type not implemented for comparison: %d", type1);
        exit(1);
    }
}
