#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "beeplib.h"
#include "debug.h"
#include "flags.h"

#define BEEP_FLAG_GETOPT_VAL                        (INT_MAX)

static struct BeepFlagsItem* flags_list;
static struct BeepFlagsItem* current_flags_item = NULL;


void beep_flags_register(
        const BeepFlag* flags, struct BeepFlagsItem* flags_item,
        int num_flags) {
    flags_item->flags = flags;
    flags_item->num_flags = num_flags;
    flags_item->next = NULL;

    if (!current_flags_item) {
        flags_list = flags_item;
        current_flags_item = flags_item;
    } else {
        current_flags_item->next = flags_item;
        current_flags_item = current_flags_item->next;
    }
}

static int count_flags(struct BeepFlagsItem* flags_list) {
    int count = 0;
    for (;flags_list; flags_list = flags_list->next) {
        count += flags_list->num_flags;
    }
    return count;
}

static void flags_to_options(struct BeepFlagsItem* flags_list,
                             struct option* long_options) {
    int i = 0;
    int f;

    for (;flags_list; flags_list = flags_list->next) {
        for (f = 0; f < flags_list->num_flags; f++) {
            const BeepFlag *flag = &flags_list->flags[f];

            switch (flag->type) {
            case BEEP_FLAG_INT:
            case BEEP_FLAG_LONG:
            case BEEP_FLAG_LONGLONG:
            case BEEP_FLAG_CHAR:
            case BEEP_FLAG_STRING:
                long_options[i].name = flag->name;
                long_options[i].has_arg = required_argument;
                long_options[i].flag = NULL;
                long_options[i].val = BEEP_FLAG_GETOPT_VAL;
                break;
            case BEEP_FLAG_BOOL:
                long_options[i].name = flag->name;
                long_options[i].has_arg = no_argument;
                long_options[i].flag = NULL;
                long_options[i].val = BEEP_FLAG_GETOPT_VAL;
                break;
            default:
                LOG_DEBUG(log_beep_main,
                        "FLAGS: Invalid flag type: %s: %d", flag->name,
                        flag->type);
                exit(1);
            }
            i++;
        }
    }
}

static const BeepFlag* get_flag(struct BeepFlagsItem* flags_list,
                                const char* flag_name) {
    int f;

    for (;flags_list; flags_list = flags_list->next) {
        for (f = 0; f < flags_list->num_flags; f++) {
            const BeepFlag *flag = &flags_list->flags[f];
            if (!strcmp(flag_name, flag->name)) {
                return flag;
            }
        }
    }
    LOG_DEBUG(log_beep_main, "FLAGS: Programming error no flag named: %s",
            flag_name);
    exit(1);
}

const BeepFlag* beep_flags_get(const char* flag_name) {
    return get_flag(flags_list, flag_name);
}

void beep_flags_dump(void) {
    int f;

    for (;flags_list; flags_list = flags_list->next) {
        for (f = 0; f < flags_list->num_flags; f++) {
            const BeepFlag *flag = &flags_list->flags[f];
            const char *meta = flag->meta;
            char optstr[30];

            if (!meta) {
                switch (flag->type) {
                case BEEP_FLAG_CHAR:
                    meta = "CHAR";
                    break;

                case BEEP_FLAG_STRING:
                    meta = "STRING";
                    break;

                case BEEP_FLAG_BOOL:
                    break;  // leave NULL.

                default:
                    meta = "INTEGER";
                    break;
                }
            }

            snprintf(optstr, sizeof(optstr), "[%s%s%s]",
                    flag->name,
                    meta ? "=" : "",
                    meta ? meta : "");
            printf("  --%-30s%s%s",
                    optstr,
                    flag->help ? flag->help : "",
                    flag->help ? " " : "");
            switch (flag->type) {
            case BEEP_FLAG_INT:
                printf("(default: %d)\n", *((int*)flag->val));
                break;

            case BEEP_FLAG_LONG:
                printf("(default: %ld)\n", *((long int*)flag->val));
                break;

            case BEEP_FLAG_LONGLONG:
                printf("(default: %lld)\n", *((long long int*)flag->val));
                break;

            case BEEP_FLAG_CHAR:
                printf("(default: %c)\n", *((char*)flag->val));
                break;

            case BEEP_FLAG_STRING:
                printf("(default: %s)\n", *((char**)flag->val));
                break;

            case BEEP_FLAG_BOOL:
                printf("\n");
                break;

            default:
                printf("(invalid)\n");
                exit(1);
                break;
            }
        }
    }
}

static void flags_usage(struct BeepFlagsItem* flags_list) {
    printf("Usage:\n");
    beep_flags_dump();
}

static void flags_type_error(struct BeepFlagsItem* flags_list,
        const BeepFlag* flag) __attribute__((noreturn));

static void flags_type_error(struct BeepFlagsItem* flags_list,
        const BeepFlag* flag) {
    const char* typestr;
    switch (flag->type) {
    case BEEP_FLAG_INT: typestr = "int"; break;
    case BEEP_FLAG_LONG: typestr = "long"; break;
    case BEEP_FLAG_LONGLONG: typestr = "long long"; break;
    case BEEP_FLAG_CHAR: typestr = "char"; break;
    case BEEP_FLAG_STRING: typestr = "string"; break;
    case BEEP_FLAG_BOOL: typestr = "bool"; break;
    default: typestr = "unknown"; break;
    }
    LOG_ERROR(log_beep_main, "flag %s is not %s", flag->name, typestr);
    flags_usage(flags_list);
    exit(1);
}

void beep_flags_init(int argc, char** argv) {
    const BeepFlag* flag;
    int num_flags = count_flags(flags_list);
    int options_size = (num_flags + 2) * sizeof(struct option);
    struct option* long_options;
    int c;
    int option_index = 0;
    optind = 1;

    long_options = malloc(options_size);
    memset(long_options, 0, options_size);
    long_options[0].name = "help";
    long_options[0].has_arg = no_argument;
    long_options[0].val = 'h';

    flags_to_options(flags_list, long_options + 1);

    while (1) {
        c = getopt_long(argc, argv, "h", long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'h':
            flags_usage(flags_list);
            exit(0);
            break;

        case BEEP_FLAG_GETOPT_VAL:
            flag = get_flag(flags_list, long_options[option_index].name);
            LOG_DEBUG(log_beep_main,
                    "FLAGS: Got flag %s, type: %d, provided value: %s",
                    flag->name, flag->type, optarg);

            switch (flag->type) {
            case BEEP_FLAG_INT:
                if (!beep_strtoi(optarg, (int*)flag->val)) {
                    flags_type_error(flags_list, flag);
                }
                break;

            case BEEP_FLAG_LONG:
                if (!beep_strtol(optarg, (long*)flag->val)) {
                    flags_type_error(flags_list, flag);
                }
                break;

            case BEEP_FLAG_LONGLONG:
                if (!beep_strtoll(optarg, (long long*)flag->val)) {
                    flags_type_error(flags_list, flag);
                }
                break;

            case BEEP_FLAG_CHAR:
                if (!optarg) {
                    flags_type_error(flags_list, flag);
                }
                *((char*)flag->val) = *optarg;
                break;

            case BEEP_FLAG_STRING:
                if (!optarg) {
                    flags_type_error(flags_list, flag);
                }
                *((char**)flag->val) = strdup(optarg);
                break;

            case BEEP_FLAG_BOOL:
                *((bool*)flag->val) = true;
                break;

            default:
                LOG_DEBUG(log_beep_main,
                        "FLAGS: Error invalid flag type: %s: %d",
                        flag->name, flag->type);
                exit(1);
            }
            break;
        case '?':
            flags_usage(flags_list);
            exit(1);
        default:
            LOG_DEBUG(log_beep_main,
                    "FLAGS: Programming error: Unhandled argument");
            break;
        }
    }

    free(long_options);
}

void beep_flags_init_with_flags(int argc, char** argv, const BeepFlag* flags,
        int num_flags) {
    struct BeepFlagsItem *tail = current_flags_item;
    struct BeepFlagsItem flags_item;

    beep_flags_register(flags, &flags_item, num_flags);
    beep_flags_init(argc, argv);

    // Remove tail (this flags_item).
    current_flags_item = tail;
    current_flags_item->next = NULL;
}
