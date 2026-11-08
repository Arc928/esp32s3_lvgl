/*
 * board_exio.h
 * CH422G IO 扩展芯片：屏幕复位、背光、触摸复位、SD 片选、USB/CAN 切换都接在它的 EXIO 脚上
 * （脚位见 board_config.h 的 BOARD_EXIO_*）
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/**
 * @brief 初始化 CH422G，把 EXIO0~7 设为输出并输出 BOARD_EXIO_DEFAULT_OUTPUT
 * @param bus CH422G 所在的 I2C 总线（board_i2c_init 得到）
 */
esp_err_t board_exio_init(i2c_master_bus_handle_t bus);

/**
 * @brief 设置一个 EXIO 脚的电平，其他脚保持不变（线程安全）
 * @param pin   EXIO 脚编号 0~7，用 BOARD_EXIO_* 宏
 * @param level true=高，false=低
 */
esp_err_t board_exio_set(uint8_t pin, bool level);
