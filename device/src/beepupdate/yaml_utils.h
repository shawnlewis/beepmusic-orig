#ifndef BEEP_UPDATE_YAML_H
#define BEEP_UPDATE_YAML_H

#include <yaml.h>

typedef enum {
    POK_READY,
    POK_DONE,
    PERR_PARSE,
    PERR_STATE,
    PERR_STREAM_END,
    PERR_SYSTEM,
    PERR_UNEXPECTED_EVENT,
    PERR_YAML
} ParseError;

#define PARSE_MAX_DEPTH 16

typedef struct {
    ParseError p_err;
    int state[PARSE_MAX_DEPTH];
    int depth;
    int count;
    int ignore_depth;
    int err_line;
    union {
        struct {
            yaml_event_type_t recv;
            yaml_event_type_t exp;
        } unexpected;
    } err_data;
} ParseControl;

#define PARSE_CONTROL_CLEANUP(__CTRL__) \
    do { \
        if (__CTRL__) { \
            bfree((__CTRL__)); \
            (__CTRL__) = NULL; \
        } \
    } while (0)

#define PARSE_CONTROL_INIT(__CTRL__, __STATE__) \
    do { \
        (__CTRL__) = (ParseControl *)bmalloc(sizeof(ParseControl)); \
        if (__CTRL__) { \
            (__CTRL__)->p_err = POK_READY; \
            (__CTRL__)->state[0] = (__STATE__); \
        } \
    } while (0)

#define PARSE_DONE(__CTRL__) \
    do { \
        (__CTRL__)->p_err = POK_DONE; \
    } while (0)

#ifndef NPARSE_DUMP_STATE
#define PARSE_DUMP_STATE(__CTRL__, __EVENT__) \
    LOG_DEBUG("state: %d depth: %d event: %s (%d)%s%s", \
            PARSE_CURRENT_STATE((__CTRL__)), \
            (__CTRL__)->depth, \
            yaml_event_str((__EVENT__).type), \
            (__EVENT__).type, \
            ((__EVENT__).type == YAML_SCALAR_EVENT) ? " - " : "", \
            ((__EVENT__).type == YAML_SCALAR_EVENT) ? \
            (const char *)(__EVENT__).data.scalar.value : "")
#else
#define PARSE_DUMP_STATE(__CTRL__, __EVENT__)
#endif

#define PARSE_PUSH_STATE(__CTRL__, __STATE__) \
    do { \
        (__CTRL__)->depth++; \
        if ((__CTRL__)->depth >= PARSE_MAX_DEPTH) { \
            PARSE_SET_ERROR((__CTRL__), PERR_STATE); \
        } else { \
            (__CTRL__)->state[(__CTRL__)->depth] = __STATE__; \
        } \
    } while (0)

#define PARSE_POP_STATE(__CTRL__) \
    do { \
        if ((__CTRL__)->depth == 0) { \
            PARSE_SET_ERROR((__CTRL__), PERR_STATE); \
        } else { \
            (__CTRL__)->depth--; \
        } \
    } while (0)

#define PARSE_CHANGE_STATE(__CTRL__, __STATE__) \
    do { \
        (__CTRL__)->state[(__CTRL__)->depth] = __STATE__; \
    } while (0)

#define PARSE_CURRENT_STATE(__CTRL__) \
    ((__CTRL__)->state[(__CTRL__)->depth])

#define PARSE_SCALAR_EQ(__STR__, __EVENT__) \
    (strcmp((__STR__), (const char *)((__EVENT__).data.scalar.value)) == 0)

#define PARSE_IGNORE_BLOCK(__CTRL__) \
    do { \
        PARSE_PUSH_STATE((__CTRL__), __IGNORE_BLOCK); \
    } while (0)

#define PARSE_UNEXPECTED_EVENT(__CTRL__, __EVENT__, __EXPECTED__) \
    do { \
        (__CTRL__)->p_err = PERR_UNEXPECTED_EVENT; \
        (__CTRL__)->err_line = __LINE__; \
        (__CTRL__)->err_data.unexpected.recv = (__EVENT__).type; \
        (__CTRL__)->err_data.unexpected.exp = (__EXPECTED__); \
    } while (0)

#define PARSE_SET_ERROR(__CTRL__, __ERROR__) \
    do { \
        (__CTRL__)->p_err = __ERROR__; \
        (__CTRL__)->err_line = __LINE__; \
    } while (0)

#define PARSE_IGNORE_CASE(__EVENT__, __CTRL__, __LEVEL__) \
    do { \
        if ((__EVENT__).type == YAML_SEQUENCE_START_EVENT \
                || (__EVENT__).type == YAML_MAPPING_START_EVENT) { \
            ((__CTRL__)->__LEVEL__)++; \
        } else if ((__EVENT__).type == YAML_SEQUENCE_END_EVENT \
                || (__EVENT__).type == YAML_MAPPING_END_EVENT) { \
            ((__CTRL__)->__LEVEL__)--; \
        } \
        if (((__CTRL__)->__LEVEL__) == 0) { \
            PARSE_POP_STATE((__CTRL__)); \
        } \
    } while (0)

// assumes int line == line of err, label done if error.
#define EMIT_EVENT(__EMITTER__, __EVENT__, __TYPE__, __FLUSH__, ...) \
    do { \
        if (!yaml_ ## __TYPE__ ## _event_initialize(__EVENT__, ## __VA_ARGS__) \
                || !yaml_emitter_emit(__EMITTER__, __EVENT__) \
                || (__FLUSH__ && !yaml_emitter_flush(__EMITTER__))) { \
            line = __LINE__; \
            goto done; \
        } \
    } while (0)

void log_parse_error(ParseControl *ctrl);
const char *yaml_event_str(yaml_event_type_t e);
int yaml_null_write_handler(void *data, unsigned char *buffer, size_t size);

#endif  // BEEP_UPDATE_YAML_H
