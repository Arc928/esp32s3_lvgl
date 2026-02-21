#include "iic_init.h"
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

/* Global I2C bus handle - shared across all devices on the bus */
i2c_master_bus_handle_t i2c_bus_handle = NULL;

/**
 * @brief Initialize I2C master bus
 *
 * This function initializes the I2C master bus with the configured parameters.
 * It should be called once before adding any I2C devices to the bus.
 *
 * @return ESP_OK on success, error code otherwise
 */
void i2c_bus_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus_handle));
}

/**
 * @brief Add I2C device to the bus
 *
 * This function adds an I2C device to the initialized bus.
 * Call i2c_bus_add_device() for each device you want to use.
 *
 * @param dev_addr        Device address (7-bit)
 * @param dev_handle      Pointer to store device handle
 * @param scl_speed_hz    I2C clock frequency for this device
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_device_init(uint8_t dev_addr, i2c_master_dev_handle_t *dev_handle, uint32_t scl_speed_hz)
{
    if (i2c_bus_handle == NULL) {
        return ESP_ERR_INVALID_STATE;  /* Bus not initialized */
    }
    if (dev_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = scl_speed_hz,
    };

    return i2c_master_bus_add_device(i2c_bus_handle, &dev_config, dev_handle);
}