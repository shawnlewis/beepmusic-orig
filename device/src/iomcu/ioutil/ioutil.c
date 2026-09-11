#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ioutil.h"
#include "iomcu_checksum.h"
#include "bootloader_proto.h"
#include "app_controller_proto.h"


#define DEFAULT_I2C_BUS                             (0)
#define DEFAULT_BTLDR_START_KEY                     \
    { \
        I2C_BTLDR_START_KEY_3, \
        I2C_BTLDR_START_KEY_2, \
        I2C_BTLDR_START_KEY_1, \
        I2C_BTLDR_START_KEY_0 \
    }
#define DEFAULT_APP_RESTART_KEY                     \
    { \
        I2C_CONTROLLER_RESTART_KEY_3, \
        I2C_CONTROLLER_RESTART_KEY_2, \
        I2C_CONTROLLER_RESTART_KEY_1, \
        I2C_CONTROLLER_RESTART_KEY_0 \
    }

#define I2C_APP_ADDR                                (I2C_CONTROLLER_ADDR)
#define I2C_ADDR_MIN                                (0x03)
#define I2C_ADDR_MAX                                (0x77)


enum {
    IOUTIL_CMD_NONE = 0,
    IOUTIL_CMD_WRITE_FW,
    IOUTIL_CMD_READ_FW,
    IOUTIL_CMD_READ_DATA,
    IOUTIL_CMD_ERASE_DATA,
    IOUTIL_CMD_READ_EEPROM,
    IOUTIL_CMD_ERASE_EEPROM,
    IOUTIL_CMD_STATUS,
    IOUTIL_CMD_INFO,
    IOUTIL_CMD_AUTOBOOT,
    IOUTIL_CMD_STOP,
    IOUTIL_CMD_START
};

struct cmd_str_pair {
    int cmd;
    const char *cmd_str;
    const char *desc;
    bool require_path;
};

static const struct cmd_str_pair cmd_table[] = {
    { IOUTIL_CMD_WRITE_FW,      "write-fw",
            "Write firmware from path (requires option APPVER)", true },
    { IOUTIL_CMD_READ_FW,       "read-fw",
            "Read firmware to path", true },
    { IOUTIL_CMD_READ_DATA,     "read-data",
            "Read entire data flash to path", true },
    { IOUTIL_CMD_ERASE_DATA,    "erase-data",
            "Erase entire data flash", false },
    { IOUTIL_CMD_READ_EEPROM,   "read-eeprom",
            "Read entire eeprom to path", true },
    { IOUTIL_CMD_ERASE_EEPROM,  "erase-eeprom",
            "Erase entire eeprom", false },
    { IOUTIL_CMD_STATUS,        "status",
            "Last command status (will clear status)", false },
    { IOUTIL_CMD_INFO,          "info",
            "Dump device info", false },
    { IOUTIL_CMD_AUTOBOOT,      "autoboot",
            "Set autoboot status (requires option AUTOBOOT)", false},
    { IOUTIL_CMD_STOP,          "stop",
            "Stop app", false },
    { IOUTIL_CMD_START,         "start",
            "Start app", false }
};

enum {
    OPT_APP_ADDR = 256,
    OPT_APP_RESTART_KEY,
    OPT_BTLDR_ADDR,
    OPT_BTLDR_START_KEY,
    OPT_I2C_BUS,
    OPT_IGNORE_CHKSUM,
    OPT_VERBOSE,
    OPT_NEW_STOP
};

static const struct option ioutil_long_opts[] = {
    {"app-addr", required_argument, NULL, OPT_APP_ADDR},
    {"restart-key", required_argument, NULL, OPT_APP_RESTART_KEY},
    {"btldr-addr", required_argument, NULL, OPT_BTLDR_ADDR},
    {"start-key", required_argument, NULL, OPT_BTLDR_START_KEY},
    {"bus", required_argument, NULL, OPT_I2C_BUS},
    {"no-chksum", no_argument, NULL, OPT_IGNORE_CHKSUM},
    {"verbose", no_argument, NULL, OPT_VERBOSE},
    {"new-stop", no_argument, NULL, OPT_NEW_STOP},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

bool en_dprintf;

unsigned int i2c_bus = DEFAULT_I2C_BUS;
bool swap_endian = false;

uint8_t app_addr = I2C_APP_ADDR;
uint8_t app_restart_key[4] = DEFAULT_APP_RESTART_KEY;
uint8_t btldr_addr = I2C_BTLDR_ADDR;
uint8_t btldr_start_key[4] = DEFAULT_BTLDR_START_KEY;
bool ignore_chksum;
bool new_stop;
int ioutil_cmd = IOUTIL_CMD_NONE;
char *ioutil_path = NULL;
uint16_t write_fw_app_ver;
bool set_autoboot;


static const char *hex_str(uint8_t *buf, size_t len) {
    static const char hex_char[] = "0123456789abcdef";
    static char str[65];
    char *p = str;
    assert((len * 2) < sizeof(str));
    while (len--) {
        *p++ = hex_char[*buf >> 4];
        *p++ = hex_char[*buf++ & 0xf];
    }
    *p = '\0';
    return str;
}

static void ioutil_usage_exit(const char *name, int status) {
    printf("Usage: %s [options] <command> [path] [APPVER] [AUTOBOOT]\n\n", name);
#ifndef NHELP
    int i;

    printf("  -h, --help            Show this information.\n");
    printf("  --app-addr=ADDR       Application i2c address (default: 0x%02x)\n",
            I2C_APP_ADDR);
    printf("  --restart-key=KEY     Application restart key (default: 0x%s)\n",
            hex_str(app_restart_key, sizeof(app_restart_key)));
    printf("  --btldr-addr=ADDR     Bootloader i2c address (default: 0x%02x)\n",
            I2C_BTLDR_ADDR);
    printf("  --start-key=KEY       Bootloader start key (default: 0x%s)\n",
            hex_str(btldr_start_key, sizeof(btldr_start_key)));
    printf("  --bus=BUS             I2C bus (default: %d)\n",
            DEFAULT_I2C_BUS);
    printf("  --new-stop            Use new command for stopping bootloader\n");        
    printf("  --no-chksum           Ignore checksum failure\n");
    printf("  --verbose             Be verbosy\n");
    printf("\nAvailable commands:\n");

    for (i = 0; i < sizeof(cmd_table)/sizeof(struct cmd_str_pair); i++) {
        printf("  %-22s%s\n", cmd_table[i].cmd_str, cmd_table[i].desc);
    }

    printf("\nADDR must be between 0x%02x-0x%02x.\n", I2C_ADDR_MIN,
            I2C_ADDR_MAX);
    printf("KEY must be 4 bytes unsigned\n");
    printf("BUS must be >= 0\n");
    printf("APPVER must be 2 bytes unsigned\n");
    printf("AUTOBOOT must be 0 or 1\n\n");
#endif
    exit(status);
}

static void process_cmd_args(int argc, char **argv) {
    int c;
    int opt_index;
    int i;
    bool require_path = false;

    while ((c = getopt_long(argc, argv, "", ioutil_long_opts, &opt_index))
            != -1) {
        switch (c) {

        case OPT_APP_ADDR:
        case OPT_BTLDR_ADDR: {
            char *ccheck;
            long int val = strtol(optarg, &ccheck, 0);

            if (val < I2C_ADDR_MIN || val > I2C_ADDR_MAX || *ccheck != '\0') {
                printf("ADDR must be between 0x%02x-0x%02x.\n",
                        I2C_ADDR_MIN, I2C_ADDR_MAX);
                ioutil_usage_exit(argv[0], 3);
            }

            if (c == OPT_APP_ADDR)
                app_addr = (uint8_t)val;
            else
                btldr_addr = (uint8_t)val;

            break;
        }

        case OPT_APP_RESTART_KEY:
        case OPT_BTLDR_START_KEY: {
            char *ccheck;
            long long int val = strtoll(optarg, &ccheck, 0);
            uint8_t *key;

            if (val < 0 || val > 0xffffffff || *ccheck != '\0') {
                printf("KEY must be 4 bytes unsigned.\n\n");
                ioutil_usage_exit(argv[0], 3);
            }
            if (c == OPT_APP_RESTART_KEY)
                key = app_restart_key;
            else
                key = btldr_start_key;
            *key++ = (val >> 24) & 0xff;
            *key++ = (val >> 16) & 0xff;
            *key++ = (val >>  8) & 0xff;
            *key = val & 0xff;

            break;
        }

        case OPT_I2C_BUS: {
            char *ccheck;
            long int val = strtol(optarg, &ccheck, 0);

            if (val < 0 || *ccheck != '\0')
                ioutil_usage_exit(argv[0], 3);
            i2c_bus = val;
            break;
        }

        case OPT_IGNORE_CHKSUM:
            ignore_chksum = true;
            break;

        case OPT_VERBOSE:
            en_dprintf = true;
            break;
        
        case OPT_NEW_STOP:
            new_stop = true;
            break;

        case 'h':
            ioutil_usage_exit(argv[0], 0);
            break;

        default:
            ioutil_usage_exit(argv[0], 3);
            break;
        }
    }

    if (optind < argc) {
        for (i = 0; i < sizeof(cmd_table)/sizeof(struct cmd_str_pair); i++) {
            if (!strcmp(cmd_table[i].cmd_str, argv[optind])) {
                ioutil_cmd = cmd_table[i].cmd;
                require_path = cmd_table[i].require_path;
                break;
            }
        }
    }

    if (ioutil_cmd == IOUTIL_CMD_NONE)
        ioutil_usage_exit(argv[0], 3);

    // Commands that need a path.
    if (require_path) {
        if ((optind + 1) < argc) {
            ioutil_path = strdup(argv[optind + 1]);
            assert(ioutil_path);
        }

        if (require_path && !ioutil_path) {
            printf("Command %s requires a path.\n\n", argv[optind]);
            ioutil_usage_exit(argv[0], 3);
        }
    }

    // Special options required by write-fw or autoboot.
    if (ioutil_cmd == IOUTIL_CMD_WRITE_FW) {
        if ((optind + 2) < argc) {
            char *ccheck;
            long int val = strtol(argv[optind + 2], &ccheck, 0);

            if (val < 0 || val > 0xffff || *ccheck != '\0') {
                printf("APPVER must be 2 bytes unsigned.\n\n");
                ioutil_usage_exit(argv[0], 3);
            }
            write_fw_app_ver = (uint16_t)val;
        } else {
            printf("Command write-fw requires APPVER option.\n\n");
            ioutil_usage_exit(argv[0], 3);
        }
    } else if (ioutil_cmd == IOUTIL_CMD_AUTOBOOT) {
        if ((optind + 1) < argc) {
            char *ccheck;
            long int val = strtol(argv[optind + 1], &ccheck, 0);

            if (*ccheck != '\0') {
                printf("Option AUTOBOOT must be 0 or 1.\n\n");
                ioutil_usage_exit(argv[0], 3);
            }
            set_autoboot = !!val;
        } else {
            printf("Command autoboot requires AUTOBOOT option.\n\n");
            ioutil_usage_exit(argv[0], 3);
        }
    }

    if (en_dprintf) {
        DPRINTF("Params:\n");
        DPRINTF("  app i2c addr: 0x%02x\n", app_addr);
        DPRINTF("  app restart key: 0x%s\n", hex_str(app_restart_key,
                sizeof(app_restart_key)));
        DPRINTF("  btldr i2c addr: 0x%02x\n", btldr_addr);
        DPRINTF("  btldr start key 0x%s\n", hex_str(btldr_start_key,
                sizeof(btldr_start_key)));
        DPRINTF("  i2c bus: %d\n", i2c_bus);
        DPRINTF("  ignore checksum: %d\n", ignore_chksum);
        DPRINTF("  cmd: %s\n", argv[optind]);
        if (ioutil_path)
            DPRINTF("  path: %s\n", ioutil_path);
        if (ioutil_cmd == IOUTIL_CMD_WRITE_FW)
            DPRINTF("  app version: 0x%04x\n", write_fw_app_ver);
        if (ioutil_cmd == IOUTIL_CMD_AUTOBOOT)
            DPRINTF("  set autoboot: %d\n", set_autoboot);
        DPRINTF("\n");
    }
}

int ioutil_reg_read(int bus, int addr, uint8_t regaddr, uint8_t *buf,
        int len) {
    int ret = 127;
    uint8_t *tbuf = malloc(sizeof(uint8_t) * (len + 1));
    if (tbuf) {
        ret = i2c_reg_read(bus, addr, regaddr, tbuf, len + 1);
        if (!ret) {
            if (i2c_message_verify(tbuf, regaddr, len + 1) || ignore_chksum) {
                memcpy(buf, tbuf, len);
            } else {
                printf("Invalid checksum for reg: 0x%02x.  "
                        "Expected: 0x%02x Recieved: 0x%02x\n",
                        regaddr, i2c_message_checksum(tbuf, regaddr, len),
                        tbuf[len]);
                ret = 2;
            }
        }
        free(tbuf);
    }
    DPRINTF("rd: %02x:%02x:%02x len: %3d ret: %d\n", bus, addr, regaddr, len,
            ret);
    return ret;
}

int ioutil_reg_write(int bus, int addr, uint8_t regaddr, uint8_t *buf,
        int len) {
    int ret = 127;
    uint8_t *tbuf = malloc(sizeof(uint8_t) * (len + 1));
    if (tbuf) {
        memcpy(tbuf, buf, len);
        tbuf[len] = i2c_message_checksum(tbuf, regaddr, len);
        ret = i2c_reg_write(bus, addr, regaddr, tbuf, len + 1);
        free(tbuf);
    }
    DPRINTF("wr: %02x:%02x:%02x len: %3d ret: %d\n", bus, addr, regaddr, len,
            ret);
    return ret;
}

static const char *status_str(uint8_t status) {
    switch (status) {
    case I2C_BTLDR_STATE_OK:
        return "ok";
    case I2C_BTLDR_STATE_OVERFLOW:
        return "overflow";
    case I2C_BTLDR_STATE_UNDERFLOW:
        return "underflow";
    case I2C_BTLDR_STATE_INVALID_REG:
        return "invalid regaddr";
    case I2C_BTLDR_STATE_INVALID_I2C_SIZE:
        return "invalid i2c size";
    case I2C_BTLDR_STATE_BAD_KEY:
        return "bad key";
    case I2C_BTLDR_STATE_INVALID_BLOCK_SIZE:
        return "invalid block size";
    case I2C_BTLDR_STATE_INVALID_ADDR:
        return "invalid addr";
    case I2C_BTLDR_STATE_ACCESS_DENIED:
        return "access denied";
    case I2C_BTLDR_STATE_BUSY:
        return "busy";
    case I2C_BTLDR_STATE_FLASH_INTERRUPTED:
        return "flash operation interrupted";
    case I2C_BTLDR_STATE_COMM_ERROR:
        return "comm error";
    case I2C_BTLDR_STATE_CHECKSUM_FAILED:
        return "checksum failed";
    case I2C_BTLDR_STATE_DATA_NOT_READY:
        return "data not ready";
    case I2C_BTLDR_STATE_FLASH_NOT_READY:
        return "flash not ready";
    case I2C_BTLDR_STATE_APP_NOT_READY:
        return "app not ready";
    default:
        return "unknown";
    }
}

static const char *board_id_str(uint16_t board_id) {
    switch (board_id) {
    case I2C_DEV_INFO_BRD_ID_NA:
        return "NA";
    case I2C_DEV_INFO_BRD_ID_B2_ENC:
        return "beeptwo encoder";
    case I2C_DEV_INFO_BRD_ID_B3_MAIN:
        return "beep3 main";
    case I2C_DEV_INFO_BRD_ID_DEMO_BOARD_1:
        return "demo board 1";
    default:
        return "unknown";
    }
}

static const char *chip_id_str(uint16_t chip_id) {
    switch (chip_id) {
    case I2C_DEV_INFO_CHIP_ID_NA:
        return "NA";
    case I2C_DEV_INFO_CHIP_ID_STM8L152C6:
        return "stm8l152c6";
    case I2C_DEV_INFO_CHIP_ID_STM8L152C8:
        return "stm8l152c8";
    case I2C_DEV_INFO_CHIP_ID_STM8L151G6:
        return "stm8l151g6";
    default:
        return "unknown";
    }
}

static const char *app_status_ready_str(uint8_t status) {
    status &= I2C_BTLDR_APP_STATUS_READY_MASK;

    switch (status) {
    case I2C_BTLDR_APP_STATUS_READY_NA:
        return "NA";
    case I2C_BTLDR_APP_STATUS_READY_READY:
        return "ready";
    case I2C_BTLDR_APP_STATUS_READY_CRC_FAILED:
        return "crc failed";
    case I2C_BTLDR_APP_STATUS_READY_NO_RESET:
        return "invalid reset vector";
    default:
        return "unknown";
    }
}

static const char *time_str(uint16_t time) {
    static char str[8];
    snprintf(str, sizeof(str), "%u%s", time & I2C_BTLDR_TIME_NUM_MASK,
            ((time & I2C_BTLDR_TIME_UNIT_MASK)
            == I2C_BTLDR_TIME_UNIT_US) ? "us" : "ms");
    return str;
}

// These rd/wr functions will be useful if byte swaps are needed.
static inline int rd_btldr_info(BtldrInfo *p) {
    return ioutil_reg_read(i2c_bus, btldr_addr, I2C_BTLDR_REG_BTLDR_INFO,
            (uint8_t *)p, I2C_BTLDR_REG_BTLDR_INFO_SIZE);
}

static inline int rd_btldr_status(BtldrStatus *p) {
    // Set COMM_ERROR if status could not be read or checksum is invalid.
    int ret = ioutil_reg_read(i2c_bus, btldr_addr, I2C_BTLDR_REG_STATUS,
            (uint8_t *)p, I2C_BTLDR_REG_STATUS_SIZE);
    if (ret)
        p->status = I2C_BTLDR_STATE_COMM_ERROR;
    return ret;
}

static inline int rd_btldr_dev_info(uint16_t proto_ver,
        BtldrDeviceInfoAll *p) {
    switch (proto_ver) {
    case 1:
        return ioutil_reg_read(i2c_bus, btldr_addr, I2C_BTLDR_REG_DEV_INFO,
                (uint8_t *)p, I2C_BTLDR_REG_DEV_INFO_SIZE_V1);
        break;

    default:
        return ioutil_reg_read(i2c_bus, btldr_addr, I2C_BTLDR_REG_DEV_INFO,
                (uint8_t *)p, I2C_BTLDR_REG_DEV_INFO_SIZE);
        break;
    }
}

static inline int rd_btldr_flash_info(BtldrFlashInfo *p) {
    return ioutil_reg_read(i2c_bus, btldr_addr, I2C_BTLDR_REG_FLASH_INFO,
            (uint8_t *)p, I2C_BTLDR_REG_FLASH_INFO_SIZE);
}

static inline int rd_btldr_app_info(BtldrAppInfo *p) {
    return ioutil_reg_read(i2c_bus, btldr_addr, I2C_BTLDR_REG_APP_INFO,
            (uint8_t *)p, I2C_BTLDR_REG_APP_INFO_SIZE);
}

static inline int wr_btldr_flash_addr(BtldrFlashRWAddr *p, uint8_t regaddr) {
    return ioutil_reg_write(i2c_bus, btldr_addr, regaddr,
            (uint8_t *)p, I2C_BTLDR_REG_FLASH_RW_ADDR_SIZE);
}

static inline int wr_btldr_app_info_set(BtldrAppInfoSet *p) {
    return ioutil_reg_write(i2c_bus, btldr_addr, I2C_BTLDR_REG_APP_INFO_SET,
            (uint8_t *)p, I2C_BTLDR_REG_APP_INFO_SET_SIZE);
}

static void ioutil_sleep(uint16_t delay) {
    unsigned int sec;
    unsigned int usec;
    uint16_t d = delay;

    if ((d & I2C_BTLDR_TIME_UNIT_MASK) == I2C_BTLDR_TIME_UNIT_MS) {
        d &= I2C_BTLDR_TIME_NUM_MASK;
        sec = d / 1000;
        d -= (sec * 1000);
        usec = d * 1000;
    } else {
        sec = 0;
        usec = d;
    }

    DPRINTF("delay: 0x%04x sec: %u usec: %u\n", delay, sec, usec);

    if (sec)
        sleep(sec);
    if (usec)
        usleep(usec);
}

static int ioutil_status(void) {
    BtldrStatus status = {};
    int ret = rd_btldr_status(&status);
    printf("Bootloader status: %d (%s)\n", status.status,
            status_str(status.status));
    return ret;
}

static int ioutil_info(BtldrStatus *status, BtldrInfo *info) {
    BtldrDeviceInfoAll dev_info = {};
    BtldrFlashInfo flash_info = {};
    BtldrAppInfo app_info = {};
    int ret;

    // Keep gathering infomation until an error occurs.
    ret = rd_btldr_dev_info(info->proto_ver, &dev_info);
    ret = ret ? ret : rd_btldr_flash_info(&flash_info);
    ret = ret ? ret : rd_btldr_app_info(&app_info);

    printf("Bootloader status: %d (%s)\n", status->status,
            status_str(status->status));

    printf("Bootloader info:\n");
    printf("  endian: 0x%08x\n", info->endian);
    printf("  proto ver: %u (0x%04x)\n", info->proto_ver, info->proto_ver);
    printf("  btldr ver: %u (0x%04x)\n", info->btldr_ver, info->btldr_ver);

    printf("Device info:\n");
    printf("  board_id: 0x%04x (%s)\n", dev_info.v1.board_id,
            board_id_str(dev_info.v1.board_id));
    printf("  board_rev: %u (0x%04x)\n", dev_info.v1.board_rev,
            dev_info.v1.board_rev);
    printf("  chip_id: 0x%04x (%s)\n", dev_info.v1.chip_id,
            chip_id_str(dev_info.v1.chip_id));
    printf("  chip_serial: %s\n",
            hex_str(dev_info.v1.chip_serial, sizeof(dev_info.v1.chip_serial)));
    if (info->proto_ver > 1) {
        printf("  option_bytes: %s\n",
                hex_str(dev_info.v2.option_bytes,
                sizeof(dev_info.v2.option_bytes)));
    }

    printf("Flash info:\n");
    printf("  eeprom base: 0x%08x\n", flash_info.eeprom_base);
    printf("  eeprom size: 0x%08x\n", flash_info.eeprom_size);
    printf("  data base: 0x%08x\n", flash_info.data_base);
    printf("  data size: 0x%08x\n", flash_info.data_size);
    printf("  eeprom block size: 0x%04x\n", flash_info.eeprom_blocksize);
    printf("  data block size: 0x%04x\n", flash_info.data_blocksize);
    printf("  eeprom read time: %s (0x%04x)\n",
            time_str(flash_info.eeprom_read_time),
            flash_info.eeprom_read_time);
    printf("  eeprom write time: %s (0x%04x)\n",
            time_str(flash_info.eeprom_write_time),
            flash_info.eeprom_write_time);
    printf("  data read time: %s (0x%04x)\n",
            time_str(flash_info.data_read_time),
            flash_info.data_read_time);
    printf("  data write time: %s (0x%04x)\n",
            time_str(flash_info.data_write_time),
            flash_info.data_write_time);

    printf("App info:\n");
    printf("  size: 0x%08x\n", app_info.size);
    printf("  crc16: 0x%04x\n", app_info.crc16);
    printf("  app version: 0x%04x\n", app_info.app_ver);
    printf("  info set time: %s (0x%04x)\n",
            time_str(app_info.app_info_set_time),
            app_info.app_info_set_time);
    printf("  status: 0x%02x (%s%s)\n", app_info.status,
            (app_info.status & I2C_BTLDR_APP_STATUS_AUTOBOOT_MASK)
            ? "autoboot, " : "",
            app_status_ready_str(app_info.status));

    printf("\n");

    return ret;
}

static int ioutil_set_autoboot(void) {
    BtldrAppInfo app_info = {};
    BtldrAppInfoSet app_info_set = {};
    int ret = rd_btldr_app_info(&app_info);

    if (!ret) {
        app_info_set.size = app_info.size;
        app_info_set.crc16 = app_info.crc16;
        app_info_set.app_ver = app_info.app_ver;
        app_info_set.autoboot = set_autoboot
                ? I2C_BTLDR_APP_STATUS_AUTOBOOT_YES : 0;
        printf("Writing app info "
                "(size: 0x%08x crc16: 0x%04x ver: 0x%04x autoboot: 0x%02x)...\n",
                app_info_set.size, app_info_set.crc16, app_info_set.app_ver,
                app_info_set.autoboot);
        ret = wr_btldr_app_info_set(&app_info_set);
        if (ret) {
            printf("Error writing app info\n");
        } else {
            ioutil_sleep(app_info.app_info_set_time);
        }
    }

    return ret;
}

static int ioutil_start(void) {
    return ioutil_reg_write(i2c_bus, btldr_addr, I2C_BTLDR_REG_START,
            btldr_start_key, I2C_BTLDR_REG_START_SIZE);
}

static int ioutil_wait(void) {
    return ioutil_reg_write(i2c_bus, btldr_addr, I2C_BTLDR_REG_WAIT,
            btldr_start_key, I2C_BTLDR_REG_START_SIZE);
}

static int ioutil_stop(void) {
    int start_count = 50;
    int ret = ioutil_reg_write(i2c_bus, app_addr, I2C_CONTROLLER_REG_RESTART,
            app_restart_key, I2C_CONTROLLER_REG_RESTART_SIZE);

    if (ret) {
        printf("Could not stop app.\n");
        return ret;
    }

    // The bootloader will wait for the start key for 1s.  If an app is loaded
    // the bootloader will first calculate the crc16 for the app readiness
    // which can take up to 1.75s on the stm8l15x.  The initial crc16
    // calculation is done before i2c setup, so retry the start key until
    // it success or timeout.
    usleep(500000);
    
    do {
        if(new_stop == true) {
          ret = ioutil_wait();
        } else {
          ret = ioutil_start();
        }
        usleep(100000);
    } while (ret && --start_count);

    if (ret) {
        printf("Could not start bootloader.\n");
    }

    return ret;
}

#define IOUTIL_EEPROM_FLASH         (0)
#define IOUTIL_DATA_FLASH           (1)

// See bootloader_proto.h for flash instructions.
static int ioutil_write_flash(uint32_t write_addr, uint8_t *buf, size_t len,
        int flash_type, uint16_t delay) {
    BtldrFlashRWAddr flash_addr;
    BtldrStatus status;
    int ret;
    uint8_t regaddr_addr = (flash_type == IOUTIL_EEPROM_FLASH) ?
            I2C_BTLDR_REG_EEPROM_WRITE_ADDR :
            I2C_BTLDR_REG_DATA_WRITE_ADDR;
    uint8_t regaddr_write = (flash_type == IOUTIL_EEPROM_FLASH) ?
            I2C_BTLDR_REG_EEPROM_WRITE :
            I2C_BTLDR_REG_DATA_WRITE;

    flash_addr.addr = write_addr;

    ret = wr_btldr_flash_addr(&flash_addr, regaddr_addr);

    if (!ret)
        ret = rd_btldr_status(&status);

    if (!ret) {
        ret = (status.status == I2C_BTLDR_STATE_OK) ? 0 : 1;
        if (ret) {
            printf("Setting write address returned bootloader status: "
                    "%d (%s).\n", status.status, status_str(status.status));
        }
    }

    if (!ret)
        ret = ioutil_reg_write(i2c_bus, btldr_addr, regaddr_write, buf, len);

    if (!ret) {
        ioutil_sleep(delay);
        ret = rd_btldr_status(&status);
    }

    if (!ret) {
        ret = (status.status == I2C_BTLDR_STATE_OK) ? 0 : 1;
        if (ret) {
            printf("Writing data returned bootloader status: %d (%s).\n",
                    status.status, status_str(status.status));
        }
    }

    return ret;
}

// See bootloader_proto.h for flash instructions.
static int ioutil_read_flash(uint32_t read_addr, uint8_t *buf, size_t len,
        int flash_type, uint16_t delay) {
    BtldrFlashRWAddr flash_addr;
    BtldrStatus status;
    int ret;
    uint8_t regaddr_addr = (flash_type == IOUTIL_EEPROM_FLASH) ?
            I2C_BTLDR_REG_EEPROM_READ_ADDR :
            I2C_BTLDR_REG_DATA_READ_ADDR;
    uint8_t regaddr_read = (flash_type == IOUTIL_EEPROM_FLASH) ?
            I2C_BTLDR_REG_EEPROM_READ :
            I2C_BTLDR_REG_DATA_READ;

    flash_addr.addr = read_addr;

    ret = wr_btldr_flash_addr(&flash_addr, regaddr_addr);

    if (!ret) {
        ret = rd_btldr_status(&status);
        if (ret) {
            printf("Setting read address returned bootloader status: "
                    "%d (%s).\n", status.status, status_str(status.status));
        }
    }

    if (!ret)
        ret = (status.status == I2C_BTLDR_STATE_OK) ? 0 : 1;

    if (!ret) {
        ioutil_sleep(delay);
        ret = ioutil_reg_read(i2c_bus, btldr_addr, regaddr_read, buf, len);
    }

    return ret;
}

#define ROUND_UP_ADDR(__ADDR__, __BS__)             \
    (((__ADDR__ + __BS__ - 1) / __BS__) * __BS__)

#define IOUTIL_RDWR_READ                            (true)
#define IOUTIL_RDWR_WRITE                           (false)

// Returns how many bytes read or written.
static uint32_t ioutil_rdwr_flash_block(uint32_t base, uint32_t size,
        uint32_t blocksize, int flash_type, uint16_t delay, uint8_t *buf,
        bool rd) {
    static const char *boundary_error = "Error: %s %s 0x%08x is not on a "
            "block boundary (block size: 0x%04x)\n";
    const char *type_str = (flash_type == IOUTIL_EEPROM_FLASH)
            ? "eeprom" : "data";
#ifndef NDPRINTF
    const char *rdwr_str = rd ? "read" : "write";
#endif
    uint32_t left = size;
    int ret = 0;

    if (base != ROUND_UP_ADDR(base, blocksize)) {
        printf(boundary_error, type_str, "base address", base, blocksize);
        return 0;
    }
    if (left != ROUND_UP_ADDR(left, blocksize)) {
        printf(boundary_error, type_str, "size", size, blocksize);
        return 0;
    }

    for (; left; base += blocksize, buf += blocksize,
            left -= blocksize) {
        DPRINTF("%s addr: 0x%08x size: 0x%08x left: 0x%08x\n", rdwr_str,
                base, blocksize, left - blocksize);
        if (rd) {
            ret = ioutil_read_flash(base, buf, blocksize, flash_type, delay);
        } else {
            ret = ioutil_write_flash(base, buf, blocksize, flash_type, delay);
        }

        // If an read/write error occurred break out before left is
        // decremented.
        if (ret)
            break;
    }

    return size - left;
}

static uint32_t ioutil_erase_flash(uint32_t base, uint32_t size,
        uint32_t blocksize, int flash_type, uint16_t delay) {
    uint8_t *buf = malloc(sizeof(uint8_t) * size);

    if (buf) {
        memset(buf, 0, size);
        size = ioutil_rdwr_flash_block(base, size, blocksize,
                flash_type, delay, buf, IOUTIL_RDWR_WRITE);
        free(buf);
    }

    return size;
}

static int ioutil_write_fw(void) {
    BtldrFlashInfo flash_info = {};
    BtldrAppInfo app_info = {};
    BtldrAppInfoSet app_info_set = {};
    uint8_t *buf = NULL;
    FILE *stream = NULL;
    long fsize;
    uint32_t rsize;
    uint32_t bsize;
    uint32_t wsize;
    int ret;

    assert(ioutil_path);

    ret = rd_btldr_flash_info(&flash_info);
    ret = ret ? ret : rd_btldr_app_info(&app_info);

    if (!ret) {
        stream = fopen(ioutil_path, "r");
        if (stream) {
            fseek(stream, 0, SEEK_END);
            fsize = ftell(stream);
            fseek(stream, 0, SEEK_SET);
        } else {
            printf("Error opening %s: %s\n", ioutil_path, strerror(errno));
            ret = 1;
        }
    }

    if (stream) {
        // Get buffer size in whole blocks.
        bsize = ROUND_UP_ADDR(fsize, flash_info.data_blocksize);
        if (bsize > flash_info.data_size) {
            printf("Firmware size 0x%08x is larger than data size 0x%08x\n",
                    bsize, flash_info.data_blocksize);
            ret = 1;
        } else {
            DPRINTF("Padding firmware size: 0x%08lx to 0x%08x\n", fsize, bsize);
            // Allocate twice the buffer size for verify.
            buf = malloc(sizeof(uint8_t) * bsize * 2);
        }
    }

    if (buf) {
        memset(buf, 0, bsize * 2);
        rsize = (uint32_t)fread(buf, 1, fsize, stream);
        if (rsize != fsize) {
            printf("Error reading from %s\n", ioutil_path);
            ret = 1;
        }
    } else {
        ret = 1;
    }

    if (buf && !ret) {
        printf("Uploading firmware...\n");
        wsize = ioutil_rdwr_flash_block(flash_info.data_base,
                bsize, flash_info.data_blocksize,
                IOUTIL_DATA_FLASH, flash_info.data_write_time, buf,
                IOUTIL_RDWR_WRITE);
        if (wsize != bsize) {
            printf("Error writing data: firmware size 0x%08x, "
                    "written 0x%08x\n", bsize, wsize);
            ret = 1;
        }
    }

    if (!ret) {
        printf("Verifying firmware...\n");
        rsize = ioutil_rdwr_flash_block(flash_info.data_base,
                bsize, flash_info.data_blocksize,
                IOUTIL_DATA_FLASH, flash_info.data_read_time, buf + bsize,
                IOUTIL_RDWR_READ);
        if (rsize != bsize) {
            printf("Error reading data: firmware size 0x%08x, "
                    "read 0x%08x\n", bsize, rsize);
            ret = 1;
        }
    }

    if (!ret) {
        if (memcmp(buf, buf + bsize, bsize)) {
            ret = 1;
        }
        printf("Verify %s!\n", ret ? "failed" : "passed");
    }


    if (!ret) {
        app_info_set.size = bsize;
        app_info_set.crc16 = crc16(IOMCU_CRC16_INIT, buf, bsize);
        app_info_set.app_ver = write_fw_app_ver;
        app_info_set.autoboot = app_info.status
                & I2C_BTLDR_APP_STATUS_AUTOBOOT_MASK;
        printf("Writing app info "
                "(size: 0x%08x crc16: 0x%04x ver: 0x%04x autoboot: 0x%02x)...\n",
                app_info_set.size, app_info_set.crc16, app_info_set.app_ver,
                app_info_set.autoboot);
        ret = wr_btldr_app_info_set(&app_info_set);
        if (ret) {
            printf("Error writing app info\n");
        } else {
            ioutil_sleep(app_info.app_info_set_time);
        }
    }

    if (!ret) {
        printf("Verifying app info...\n");
        ret = rd_btldr_app_info(&app_info);
        if (ret
                || app_info.size != app_info_set.size
                || app_info.crc16 != app_info_set.crc16
                || app_info.app_ver != app_info_set.app_ver
                || (app_info.status & I2C_BTLDR_APP_STATUS_AUTOBOOT_MASK)
                != (app_info_set.autoboot & I2C_BTLDR_APP_STATUS_AUTOBOOT_MASK)) {
            ret = 1;
        }
        printf("Verify %s!\n", ret ? "failed" : "passed");
    }

    if (buf)
        free(buf);

    if (stream)
        fclose(stream);

    return ret;
}

static int ioutil_read_fw(void) {
    BtldrFlashInfo flash_info = {};
    BtldrAppInfo app_info = {};
    uint8_t *buf = NULL;
    FILE *stream = NULL;
    size_t wsize;
    uint32_t rsize;
    int ret;
    uint16_t crc;

    assert(ioutil_path);

    ret = rd_btldr_flash_info(&flash_info);
    ret = ret ? ret : rd_btldr_app_info(&app_info);

    if (!ret) {
        if (app_info.size != ROUND_UP_ADDR(app_info.size,
                    flash_info.data_blocksize)) {
            printf("Warning app size 0x%08x is not on a block boundary.\n",
                    app_info.size);
            app_info.size = ROUND_UP_ADDR(app_info.size,
                    flash_info.data_blocksize);
            printf("Changing app size to 0x%08x\n", app_info.size);
        }

        if (app_info.size > flash_info.data_size) {
            printf("Warning app size 0x%08x is larger than data size 0x%08x\n",
                    app_info.size, flash_info.data_size);
            app_info.size = flash_info.data_size;
            printf("Changing app size to 0x%08x\n", app_info.size);
        }
    }

    if (!ret) {
        buf = malloc(sizeof(uint8_t) * app_info.size);
        if (!buf)
            ret = 1;
    }

    if (!ret) {
        printf("Downloading firmware...\n");
        rsize = ioutil_rdwr_flash_block(flash_info.data_base,
                app_info.size, flash_info.data_blocksize,
                IOUTIL_DATA_FLASH, flash_info.data_read_time, buf,
                IOUTIL_RDWR_READ);
        if (rsize != app_info.size) {
            printf("Error reading data: firmware size 0x%08x, "
                    "read 0x%08x\n", app_info.size, rsize);
            ret = 1;
        }
    }

    if (!ret) {
        crc = crc16(IOMCU_CRC16_INIT, buf, app_info.size);
        if (app_info.crc16 != crc) {
            printf("Warning crc16 mismatch app_info: 0x%04x "
                    "downloaded: 0x%04x\n",
                    app_info.crc16, crc);
        }
    }

    if (!ret) {
        stream = fopen(ioutil_path, "w");
        if (stream) {
            wsize = fwrite(buf, 1, app_info.size, stream);
            if (wsize != app_info.size) {
                printf("Error writing to %s\n", ioutil_path);
                ret = 1;
            }
        } else {
            printf("Error opening %s: %s\n", ioutil_path, strerror(errno));
            ret = 1;
        }
    }

    if (buf)
        free(buf);

    if (stream)
        fclose(stream);

    return ret;
}

static int ioutil_read_data(void) {
    BtldrFlashInfo flash_info = {};
    uint8_t *buf = NULL;
    size_t wsize;
    uint32_t rsize = 0;
    int ret = rd_btldr_flash_info(&flash_info);
    FILE *stream = NULL;

    assert(ioutil_path);

    if (!ret) {
        buf = malloc(sizeof(uint8_t) * flash_info.data_size);
    }

    if (buf) {
        printf("Downloading data flash...\n");
        rsize = ioutil_rdwr_flash_block(flash_info.data_base,
                flash_info.data_size, flash_info.data_blocksize,
                IOUTIL_DATA_FLASH, flash_info.data_read_time, buf,
                IOUTIL_RDWR_READ);
    }

    if (buf && rsize == flash_info.data_size) {
        stream = fopen(ioutil_path, "w");
        if (stream) {
            wsize = fwrite(buf, 1, rsize, stream);
            if (wsize != rsize) {
                printf("Error writing to %s\n", ioutil_path);
                ret = 1;
            }
        } else {
            printf("Error opening %s: %s\n", ioutil_path, strerror(errno));
            ret = 1;
        }
    } else {
        printf("Error reading data: total size 0x%08x, read 0x%08x\n",
            flash_info.data_size, rsize);
        ret = ret ? ret : 1;
    }

    if (buf)
        free(buf);

    if (stream)
        fclose(stream);

    return ret;
}

static int ioutil_erase_data(void) {
    BtldrFlashInfo flash_info = {};
    uint32_t esize;
    int ret = rd_btldr_flash_info(&flash_info);

    if (!ret) {
        printf("Erasing data...\n");
        esize = ioutil_erase_flash(flash_info.data_base,
                flash_info.data_size, flash_info.data_blocksize,
                IOUTIL_DATA_FLASH, flash_info.data_write_time);
        if (esize != flash_info.data_size) {
            printf("Error erasing data: total size 0x%08x, erased 0x%08x\n",
                flash_info.data_size, esize);
            ret = 1;
        }
    }

    return ret;
}

static int ioutil_read_eeprom(void) {
    BtldrFlashInfo flash_info = {};
    uint8_t *buf = NULL;
    size_t wsize;
    uint32_t rsize = 0;
    int ret = rd_btldr_flash_info(&flash_info);
    FILE *stream = NULL;

    if (!ret) {
        buf = malloc(sizeof(uint8_t) * flash_info.data_size);
    }

    if (buf) {
        printf("Downloading eeprom flash...\n");
        rsize = ioutil_rdwr_flash_block(flash_info.eeprom_base,
                flash_info.eeprom_size, flash_info.eeprom_blocksize,
                IOUTIL_EEPROM_FLASH, flash_info.eeprom_read_time, buf,
                IOUTIL_RDWR_READ);
    }

    if (buf && rsize == flash_info.eeprom_size) {
        stream = fopen(ioutil_path, "w");
        if (stream) {
            wsize = fwrite(buf, 1, rsize, stream);
            if (wsize != rsize) {
                printf("Error writing to %s\n", ioutil_path);
                ret = 1;
            }
        } else {
            printf("Error opening %s: %s\n", ioutil_path, strerror(errno));
            ret = 1;
        }
    } else {
        printf("Error reading eeprom: total size 0x%08x, read 0x%08x\n",
            flash_info.eeprom_size, rsize);
        ret = ret ? ret : 1;
    }

    if (buf)
        free(buf);

    if (stream)
        fclose(stream);

    return ret;
}

static int ioutil_erase_eeprom(void) {
    BtldrFlashInfo flash_info = {};
    uint32_t esize;
    int ret = rd_btldr_flash_info(&flash_info);

    if (!ret) {
        printf("Erasing eeprom...\n");
        esize = ioutil_erase_flash(flash_info.eeprom_base,
                flash_info.eeprom_size, flash_info.eeprom_blocksize,
                IOUTIL_DATA_FLASH, flash_info.eeprom_write_time);
        if (esize != flash_info.eeprom_size) {
            printf("Error erasing eeprom: total size 0x%08x, read 0x%08x\n",
                flash_info.eeprom_size, esize);
            ret = 1;
        }
    }

    return ret;
}

int main(int argc, char **argv) {
    int ret = 1;

    process_cmd_args(argc, argv);

    // Do start, stop and status commands without doing any detection first.
    if (ioutil_cmd == IOUTIL_CMD_STOP) {
        ret = ioutil_stop();
    } else if (ioutil_cmd == IOUTIL_CMD_START) {
        ret = ioutil_start();
    } else if (ioutil_cmd == IOUTIL_CMD_STATUS) {
        ret = ioutil_status();
    } else {
        BtldrInfo info = {};
        BtldrStatus status = {};

        // Get status first before any other command can clear it.
        ret = rd_btldr_status(&status);
        // Get bootloader information for any commands that need it.
        ret = ret ? ret : rd_btldr_info(&info);

        if (ret) {
            printf("Could not detect bootloader\n");
        } else if (!ret && (info.proto_ver > I2C_BTLDR_INFO_PROTO_VERSION)) {
            printf("Unsupported protocol version: 0x%04x > 0x%04x\n",
                info.proto_ver, I2C_BTLDR_INFO_PROTO_VERSION);
            ret = 1;
        } else {
            switch (ioutil_cmd) {
            case IOUTIL_CMD_WRITE_FW:
                ret = ioutil_write_fw();
                break;

            case IOUTIL_CMD_READ_FW:
                ret = ioutil_read_fw();
                break;

            case IOUTIL_CMD_READ_DATA:
                ret = ioutil_read_data();
                break;

            case IOUTIL_CMD_ERASE_DATA:
                ret = ioutil_erase_data();
                break;

            case IOUTIL_CMD_READ_EEPROM:
                ret = ioutil_read_eeprom();
                break;

            case IOUTIL_CMD_ERASE_EEPROM:
                ret = ioutil_erase_eeprom();
                break;

            case IOUTIL_CMD_INFO:
                ret = ioutil_info(&status, &info);
                break;

            case IOUTIL_CMD_AUTOBOOT:
                ret = ioutil_set_autoboot();
                break;

            case IOUTIL_CMD_STOP:
                ret = ioutil_stop();
                break;

            case IOUTIL_CMD_START:
                ret = ioutil_start();
                break;

            default:
                abort();
                break;
            }
        }
    }

    if (ioutil_path)
        free(ioutil_path);

    printf("%s: command %d returned code %d\n", ret ? "Error" : "Success",
            ioutil_cmd, ret);

    return ret;
}
