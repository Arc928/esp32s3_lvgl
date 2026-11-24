/*
 * ui_board_config.h
 * 板端界面平台参数桥接：由 main/CMakeLists.txt 通过 UI_PLATFORM_CONFIG_H 注入到 ui_common.h，
 * 使 main/UI/custom/ 不直接依赖 board_config.h，保持平台无关与桌面模拟器复用能力。
 */

#pragma once

#include "board_config.h"

#define UI_SCREEN_W         BOARD_LCD_H_RES
#define UI_SCREEN_H         BOARD_LCD_V_RES
#define UI_PUSH_TRANSITION  BOARD_UI_PUSH_TRANSITION
