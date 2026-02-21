#ifndef _MY_IIC_H
#define _MY_IIC_H

#include "driver/i2c_master.h"

#define GPIO_SDA    8
#define GPIO_SCL    9

/* I2C Bus Configuration */
#define I2C_MASTER_NUM              I2C_NUM_0                   /*!< I2C port number */
#define I2C_MASTER_SDA_IO           GPIO_SDA                    /*!< GPIO number used for I2C master data */
#define I2C_MASTER_SCL_IO           GPIO_SCL                    /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_FREQ_HZ          400000                      /*!< I2C master clock frequency */
#define I2C_MASTER_TIMEOUT_MS       1000                        /*!< I2C timeout in milliseconds */

/**
 * @brief I2C bus handle (global)
 * This handle is shared across all I2C devices on the same bus
 */
extern i2c_master_bus_handle_t i2c_bus_handle;

/**
 * @brief Initialize I2C master bus
 */
void i2c_bus_init(void);

/**
 * @brief Add I2C device to the bus
 *
 * @param dev_addr        Device address (7-bit)
 * @param dev_handle      Pointer to store device handle
 * @param scl_speed_hz    I2C clock frequency for this device
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_device_init(uint8_t dev_addr, i2c_master_dev_handle_t *dev_handle, uint32_t scl_speed_hz);

#endif