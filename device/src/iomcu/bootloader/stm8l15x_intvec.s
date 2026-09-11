// Vector 0 is the reset vector which points to the entry point of the
// bootloader.  The other vectors will all jump to the interrupt vector
// table of the app located after the bootloader.  The table below will
// actually be write protected with the rest of the bootloader so the
// intermediate jump is required even though it has a small performance
// penalty.

// A different option is to always load the interrupt table at a fixed
// point in ram.  This allows the use of interrupts in the bootloader
// but will be more complex and come at a greater performance penalty
// (execution out of ram costs more cycles).

    SECTION `.intvec`:CONST
    PUBLIC  __intvec
    EXTERN  __iar_program_start

#include "bootloader.h"

app_intvec MACRO
    DC8     0x82        // INT
    DC24    APP_BASE_DC24 + (4 * \1)
    ENDM

__intvec:
    DC8     0x82
    DC24    __iar_program_start
    app_intvec 1
    app_intvec 2
    app_intvec 3
    app_intvec 4
    app_intvec 5
    app_intvec 6
    app_intvec 7
    app_intvec 8
    app_intvec 9
    app_intvec 10
    app_intvec 11
    app_intvec 12
    app_intvec 13
    app_intvec 14
    app_intvec 15
    app_intvec 16
    app_intvec 17
    app_intvec 18
    app_intvec 19
    app_intvec 20
    app_intvec 21
    app_intvec 22
    app_intvec 23
    app_intvec 24
    app_intvec 25
    app_intvec 26
    app_intvec 27
    app_intvec 28
    app_intvec 29
    app_intvec 30
    app_intvec 31

    END
