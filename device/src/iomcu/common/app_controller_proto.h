#ifndef IOMCU_APP_CONTROLLER_PROTO_H
#define IOMCU_APP_CONTROLLER_PROTO_H


#include "iomcu_shared.h"

#define I2C_CONTROLLER_ADDR                         (0x23)

#define I2C_CONTROLLER_REG_RESTART                  (0x81)
#define I2C_CONTROLLER_REG_RESV                     (0xff)

#define I2C_CONTROLLER_REG_VAR_SIZE                 (0xff)
#define I2C_CONTROLLER_REG_RESTART_SIZE             (IOMCU_KEY_SIZE)

#define I2C_CONTROLLER_RESTART_KEY_0                (IOMCU_COMMON_KEY_0)
#define I2C_CONTROLLER_RESTART_KEY_1                (IOMCU_COMMON_KEY_1)
#define I2C_CONTROLLER_RESTART_KEY_2                (IOMCU_COMMON_KEY_2)
#define I2C_CONTROLLER_RESTART_KEY_3                (IOMCU_COMMON_KEY_3)
#define I2C_CONTROLLER_RESTART_KEY_CHECKSUM         (IOMCU_COMMON_KEY_CRC8_CHECKSUM)


typedef IomcuKey ControllerRestart;


#endif  // IOMCU_APP_CONTROLLER_PROTO_H
