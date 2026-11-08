/*
 * CH422G 不是寄存器式的 I2C 设备：每个功能占一个 I2C 地址，往这个地址写 1 个字节即可。
 *   0x24 系统参数，bit0=1 时 EXIO0~7 为推挽输出
 *   0x38 EXIO0~7 的输出电平
 * 输出电平读不回来，所以在这里留一份当前值，改单个脚时在它上面改。
 */

#include "board_exio.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "board_config.h"

#define TAG "board_exio"

#define CH422G_ADDR_SYSTEM          0x24
#define CH422G_ADDR_OUTPUT          0x38
#define CH422G_SYSTEM_IO_OUTPUT     0x01
#define CH422G_SPEED_HZ             400000
#define CH422G_TIMEOUT_MS           100

static i2c_master_dev_handle_t s_system_dev;
static i2c_master_dev_handle_t s_output_dev;
static SemaphoreHandle_t s_lock;
static uint8_t s_output;

static esp_err_t write_byte(i2c_master_dev_handle_t dev, uint8_t value)
{
    return i2c_master_transmit(dev, &value, 1, CH422G_TIMEOUT_MS);
}

static esp_err_t add_device(i2c_master_bus_handle_t bus, uint16_t addr, i2c_master_dev_handle_t *dev)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = CH422G_SPEED_HZ,
    };
    return i2c_master_bus_add_device(bus, &cfg, dev);
}

esp_err_t board_exio_init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_lock != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(add_device(bus, CH422G_ADDR_SYSTEM, &s_system_dev), TAG, "failed to add CH422G");
    ESP_RETURN_ON_ERROR(add_device(bus, CH422G_ADDR_OUTPUT, &s_output_dev), TAG, "failed to add CH422G");

    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "no mem for lock");

    /*
     * 先写好输出电平再打开输出，免得屏幕复位脚、背光脚在打开输出的瞬间被拉低；
     * 打开后再写一次，确保电平生效。
     */
    s_output = BOARD_EXIO_DEFAULT_OUTPUT;
    ESP_RETURN_ON_ERROR(write_byte(s_output_dev, s_output), TAG, "CH422G not responding");
    ESP_RETURN_ON_ERROR(write_byte(s_system_dev, CH422G_SYSTEM_IO_OUTPUT), TAG, "failed to enable outputs");
    ESP_RETURN_ON_ERROR(write_byte(s_output_dev, s_output), TAG, "failed to set outputs");

    return ESP_OK;
}

esp_err_t board_exio_set(uint8_t pin, bool level)
{
    if (pin > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t value = level ? (uint8_t)(s_output | (1U << pin)) : (uint8_t)(s_output & ~(1U << pin));
    esp_err_t ret = write_byte(s_output_dev, value);
    if (ret == ESP_OK) {
        s_output = value;
    }
    xSemaphoreGive(s_lock);

    return ret;
}
