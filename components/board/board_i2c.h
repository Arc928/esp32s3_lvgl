/*
 * board_i2c.h
 * 板载 I2C 总线（引脚见 board_config.h）
 *
 * 只负责创建总线；挂在总线上的设备由各自的驱动用拿到的总线句柄自行添加，
 * 各设备的通信速率也由驱动自己设置。
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

/**
 * @brief 初始化 I2C 主机总线
 * @param[out] bus 总线句柄，传给需要用 I2C 的驱动
 */
esp_err_t board_i2c_init(i2c_master_bus_handle_t *bus);
