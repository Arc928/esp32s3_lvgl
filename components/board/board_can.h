/*
 * board_can.h
 * 板载 CAN（TWAI）硬件初始化（引脚和默认波特率见 board_config.h）
 *
 * 只负责硬件路由切换（如 ESP32-S3 通过 CH422G EXIO5 切换 USB/CAN）和创建/销毁 TWAI 节点；
 * 回调注册、使能、收发任务和协议解析由上层服务（can_service）负责。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_twai.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_CAN_MODE_NORMAL = 0,      /* 正常收发模式（接外部 CAN 总线） */
    BOARD_CAN_MODE_LISTEN_ONLY,     /* 只听模式（不发送、不回 ACK，适合无干扰抓包） */
    BOARD_CAN_MODE_LOOPBACK,        /* 自测回环模式（no-ack + self-reception，经板载收发器自发自收） */
} board_can_mode_t;

typedef struct {
    uint32_t bitrate;               /* 波特率（bps），0 表示使用 BOARD_CAN_DEFAULT_BITRATE */
    board_can_mode_t mode;          /* 工作模式 */
    uint32_t tx_queue_depth;        /* 底层发送队列深度，0 表示使用默认值（8）；只听模式下忽略 */
    bool single_shot;               /* false=按 CAN 规范自动重传；true=单次发送不重传 */
} board_can_config_t;

/**
 * @brief 初始化板载 CAN（TWAI）节点（创建后处于 disabled 状态，上层注册回调后再调用 twai_node_enable）
 *
 * @note 在 ESP32-S3-Touch-LCD-4.3 上会先拉高 CH422G 的 BOARD_EXIO_USB_SEL（EXIO5）将 GPIO19/20
 *       切到 CAN 收发器，因此需在 board_exio_init() 之后调用。
 *
 * @param[in]  cfg   可选配置，传 NULL 时使用默认波特率与 NORMAL 模式
 * @param[out] node  创建的 TWAI 节点句柄
 * @return ESP_OK；当前板型未配置 CAN 时返回 ESP_ERR_NOT_SUPPORTED
 */
esp_err_t board_can_init(const board_can_config_t *cfg, twai_node_handle_t *node);

/**
 * @brief 销毁 TWAI 节点并还原板级路由（调用前请先 twai_node_disable）
 */
esp_err_t board_can_deinit(twai_node_handle_t node);

#ifdef __cplusplus
}
#endif
