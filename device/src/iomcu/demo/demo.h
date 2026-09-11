#ifndef IOMCU_DEMO_H
#define IOMCU_DEMO_H


// I2C protocol defines.
typedef struct {
    uint8_t down_count;
    uint8_t up_count;
} BEEP_BTN_STATE;

typedef struct {
    uint32_t endian;
    uint16_t proto_ver;
    uint16_t board_id;
    uint16_t board_rev;
    uint16_t chip_id;
} BEEP_SYSTEM_INFO;

typedef uint8_t BEEP_AC_BATT_STATE;
typedef uint8_t BEEP_LED_BRIGHTNESS;
typedef uint8_t BEEP_BATTERY_LEVEL;
typedef uint8_t BEEP_MODULE_MODE;


#define I2C_DEMO_ADDR                               (0x14)
#define I2C_REG_READ_ACK                            (0x00)
#define I2C_REG_READ_SYSTEM_INFO                    (0x01)
#define I2C_REG_READ_SYSTEM_STATE                   (0x02)
#define I2C_REG_WRITE_ACK                           (0x80)
#define I2C_REG_WRITE_RESERVED                      (0x81)
#define I2C_REG_WRITE_SYSTEM_LED                    (0x82)
#define I2C_REG_WRITE_BEEP_MODULE_MODE              (0x83)
#define I2C_REG_WRITE_SYNC                          (0xf0)
#define I2C_REG_RESERVED                            (0xff)

// The crc8 init value is the base + register address.
#define I2C_CRC8_BASE_VALUE                         (0xff)

#define I2C_SYS_INFO_SLAVE_ENDIAN                   (0x87654321)
#define I2C_SYS_INFO_PROTOCOL_VERSION               (0x0001)
#define I2C_SYS_INFO_BOARD_ID_NA                    (0x0000)
#define I2C_SYS_INFO_BOARD_ID_DEMO_BOARD_1          (0x0003)
#define I2C_SYS_INFO_CHIP_ID_NA                     (0x0000)
#define I2C_SYS_INFO_CHIP_ID_STM8L152C6             (0x0001)

#define I2C_BEEP_MODULE_MODE_NORMAL                 (0x00)
#define I2C_BEEP_MODULE_MODE_UPDATING               (0x01)
#define I2C_BEEP_MODULE_MODE_SETUP                  (0x02)

#define I2C_SYNC_KEY_0                              (0xba)
#define I2C_SYNC_KEY_1                              (0x5e)
#define I2C_SYNC_KEY_2                              (0xba)
#define I2C_SYNC_KEY_3                              (0x11)

#define I2C_ACK                                     (0xaa)
#define I2C_SEND_ACK_CHECKSUM                       (0xe4)
#define I2C_RECV_ACK_CHECKSUM                       (0x0b)


// Demo board defines.
typedef enum {
    DEMO_BTN_VOL_DOWN = 0,
    DEMO_BTN_VOL_UP,
    DEMO_BTN_BACK,
    DEMO_BTN_PLAY,
    DEMO_BTN_SKIP,
    DEMO_BTN_MAGIC,
    DEMO_BTN_SPARE,
    __DEMO_BTN_COUNT
} DEMO_BTN;

typedef enum {
    BTN_STATE_DOWN = 0,
    BTN_STATE_UP,
    BTN_INVALID = 0xff
} BTN_STATE;

typedef enum {
    DEMO_LED_RED = 0,
    DEMO_LED_RGB_R,
    DEMO_LED_RGB_G,
    DEMO_LED_RGB_B,
    __DEMO_LED_COUNT
} DEMO_LED;

typedef enum {
    PWR_SOURCE_STATE_AC = 0,
    PWR_SOURCE_STATE_BATT
} PWR_SOURCE_STATE;

typedef enum {
    DATA_READY_NO = 0,
    DATA_READY_YES
} DATA_READY;


// These structures are in the same I2C format for communicating with
// the Beep module.  crc and seq are not part system data but it is
// easier to have everything together when using DMA with I2C.
typedef struct {
    uint8_t crc;
    BEEP_SYSTEM_INFO info;
} DEMO_SYSTEM_INFO;

typedef struct {
    uint8_t crc;
    uint8_t seq;
    BEEP_BTN_STATE buttons[__DEMO_BTN_COUNT];
    BEEP_AC_BATT_STATE pwr_source;
} DEMO_SYSTEM_STATE;

typedef struct {
    uint8_t crc;
    uint8_t seq;
    BEEP_LED_BRIGHTNESS led_brightness[__DEMO_LED_COUNT];
} DEMO_LED_STATE;

typedef struct {
    uint8_t crc;
    uint8_t seq;
    BEEP_MODULE_MODE mode;
} DEMO_BEEP_MODULE_MODE;


// Event interrupt callbacks.
void button_state_changed(DEMO_BTN btn, BTN_STATE state);
void pwr_source_state_changed(PWR_SOURCE_STATE state);
void timer_callback(void);


// I2C interrupt callbacks.
const uint8_t *i2c_send_ack(void);
const DEMO_SYSTEM_INFO *i2c_send_system_info(void);
const DEMO_SYSTEM_STATE *i2c_send_system_state(void);
void i2c_recv_ack(const uint8_t *ack);
void i2c_recv_system_led(const DEMO_LED_STATE *led_state);
void i2c_recv_beep_module_mode(const DEMO_BEEP_MODULE_MODE *mode);
void i2c_recv_sync(const uint8_t *key);


// STM hw control/poll functions.
// Set pwm duty cycle for each LED.  % Duty cycle = (value / 255) * 100.
void stm_set_led(DEMO_LED led, uint8_t value);

// Get pwm duty cycle for each LED.
uint8_t stm_get_led(DEMO_LED led);

// Set data ready (interrupt) line to notify Beep module.
void stm_set_data_ready(DATA_READY ready);

// Get state of the data ready (interrupt) line.
DATA_READY stm_get_data_ready(void);

// Get current state of each button.
BTN_STATE stm_get_button_state(DEMO_BTN btn);

// Get current state of the power source switch.
PWR_SOURCE_STATE stm_get_power_source_state(void);

// Set period for timer callback.  Setting a value of 0 will disable
// the callback.  Period (seconds) = deci_seconds * 0.1.
void stm_set_timer_callback_period(uint16_t deci_seconds);


// STM hw functions.
// Initialize basic stm hardware.  This will setup the button/power state
// GPIOs so they can be read before enabling interrupts.  This allows software
// to get the power on state of all buttons (i.e. holding a button while
// plugging in the demo).
void stm_hw_init(void);

// Starts timers, LED output, I2C and enables interrupts.
void stm_hw_start(void);


#endif  // IOMCU_DEMO_H
