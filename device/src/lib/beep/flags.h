#ifndef BEEP_FLAGS_H
#define BEEP_FLAGS_H

typedef enum {
    BEEP_FLAG_INT,
    BEEP_FLAG_LONG,
    BEEP_FLAG_LONGLONG,
    BEEP_FLAG_CHAR,
    BEEP_FLAG_STRING,
    BEEP_FLAG_BOOL
} BeepFlagType;

typedef struct {
    const char* name;
    const char* help;
    const char* meta;
    void* val;
    BeepFlagType type;
} BeepFlag;

struct BeepFlagsItem {
    const BeepFlag* flags;
    int num_flags;
    struct BeepFlagsItem* next;
};

#ifndef NHELP
#define BEEP_FLAG(NAME, TYPE, VAL, META, HELP) \
    {NAME, HELP, META, VAL, TYPE}
#else
#define BEEP_FLAG(NAME, TYPE, VAL, META, HELP) \
    {NAME, NULL, NULL, VAL, TYPE}
#endif

#define BEEP_INIT_FLAGS(FLAGS) \
    static struct BeepFlagsItem FLAGS ## _item; \
    \
    __attribute__((constructor)) \
    static void beep_init_ ## FLAGS (void) { \
        beep_flags_register(FLAGS, & FLAGS ## _item, \
                            sizeof(FLAGS) / sizeof(BeepFlag)); \
    }

// Don't call directly, use BEEP_INIT_FLAGS instead.
void beep_flags_register(
        const BeepFlag* flags, struct BeepFlagsItem* flags_item,
        int num_flags);

const BeepFlag* beep_flags_get(const char* flag_name);
void beep_flags_dump(void);
void beep_flags_init(int argc, char** argv);
void beep_flags_init_with_flags(int argc, char** argv, const BeepFlag* flags,
        int num_flags);

#endif  // BEEP_FLAGS_H
