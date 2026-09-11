#include "stm8l15x.h"

#include "bootloader.h"


// These are close replacements for the stdperiph functions.  The
// data/program flash does not allow read-while-write (RWW) so the
// write operations must be executed from memory.  The stdperiph flash
// functions do this but use 32 bit math.  The IAR compiler uses intrinsic
// functions to do this which are not set to be copied into memory.  Without
// the source for these the is no easy way to set those functions to execute
// out of memory.

// These functions will do the 32 bit math while executing out of flash but
// write the data while executing out of memory.

// Note: EEPROM does allow RWW.
__ramfunc void flash_programblock_mem(uint8_t prog_mode_mask,
        uint32_t startaddress, uint8_t *buf) {
    uint16_t count = 0;

    FLASH->CR2 |= prog_mode_mask;

    // Copy data bytes from RAM to FLASH memory
    for (count = 0; count < FLASH_BLOCK_SIZE; count++)
    {
        *((PointerAttr uint8_t*) (MemoryAddressCast)startaddress + count) =
                ((uint8_t)(buf[count]));
    }
}

void flash_programblock(uint16_t block, FLASH_MemType_TypeDef memtype,
        FLASH_ProgramMode_TypeDef prog_mode, uint8_t *buf) {
    uint8_t prog_mode_mask;
    uint32_t startaddress = 0;

    // Check parameters
    assert_param(IS_FLASH_MEMORY_TYPE(memtype));
    assert_param(IS_FLASH_PROGRAM_MODE(FLASH_ProgMode));
    if (memtype == FLASH_MemType_Program)
    {
        assert_param(IS_FLASH_PROGRAM_BLOCK_NUMBER(block));
        startaddress = FLASH_PROGRAM_START_PHYSICAL_ADDRESS;
    }
    else
    {
        assert_param(IS_FLASH_DATA_EEPROM_BLOCK_NUMBER(block));
        startaddress = FLASH_DATA_EEPROM_START_PHYSICAL_ADDRESS;
    }

    // Point to the first block address
    startaddress = startaddress + ((uint32_t)block * FLASH_BLOCK_SIZE);

    // Selection of Standard or Fast programming mode
    if (prog_mode == FLASH_ProgramMode_Standard)
    {
        // Standard programming mode
        prog_mode_mask = FLASH_CR2_PRG;
    }
    else
    {
        // Fast programming mode
        prog_mode_mask = FLASH_CR2_FPRG;
    }

    flash_programblock_mem(prog_mode_mask, startaddress, buf);
}

__ramfunc void flash_eraseblock_mem(uint32_t startaddress) {
#if defined (STM8L15X_MD) \
    || defined (STM8L15X_MDP) \
    || defined (STM8L15X_LD) \
    || defined (STM8L05X_LD_VL) \
    || defined (STM8L05X_MD_VL) \
    || defined (STM8AL31_L_MD)
    uint32_t PointerAttr *pwFlash =
            (PointerAttr uint32_t *)(uint16_t)(startaddress);
#elif defined (STM8L15X_HD) || defined (STM8L05X_HD_VL)
    uint8_t PointerAttr *pwFlash =
            (PointerAttr uint8_t *)(uint32_t)(startaddress);
#endif

    // Enable erase block mode
    FLASH->CR2 |= FLASH_CR2_ERASE;

#if defined (STM8L15X_MD) \
    || defined (STM8L15X_MDP) \
    || defined (STM8L15X_LD) \
    || defined (STM8L05X_LD_VL) \
    || defined (STM8L05X_MD_VL) \
    || defined (STM8AL31_L_MD)
    *pwFlash = (uint32_t)0;
#elif defined (STM8L15X_HD) || defined (STM8L05X_HD_VL)
    *pwFlash = (uint8_t)0;
    *(pwFlash + 1) = (uint8_t)0;
    *(pwFlash + 2) = (uint8_t)0;
    *(pwFlash + 3) = (uint8_t)0;
#endif
}

void flash_eraseblock(uint16_t block, FLASH_MemType_TypeDef memtype) {
    uint32_t startaddress = 0;

    // Check parameters
    assert_param(IS_FLASH_MEMORY_TYPE(memtype));
    if (memtype == FLASH_MemType_Program)
    {
        assert_param(IS_FLASH_PROGRAM_BLOCK_NUMBER(block));
        startaddress = FLASH_PROGRAM_START_PHYSICAL_ADDRESS;
    }
    else
    {
        assert_param(IS_FLASH_DATA_EEPROM_BLOCK_NUMBER(block));
        startaddress = FLASH_DATA_EEPROM_START_PHYSICAL_ADDRESS;
    }

    startaddress = startaddress + ((uint32_t)block * FLASH_BLOCK_SIZE);

    flash_eraseblock_mem(startaddress);
}
