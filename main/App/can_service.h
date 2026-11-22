/*
 * can_service.h
 * CAN（TWAI）服务 - 负责板载 CAN 节点的启停、后台收帧分发、线程安全发送与 Bus-Off 自动恢复
 *
 * 收发链路：
 *  - 接收：TWAI on_rx_done ISR -> FreeRTOS 接收队列 -> 后台 can_task
 *          可打印收帧日志（便于底层打通与总线抓包），也可通过 can_service_set_rx_callback()
 *          挂载协议解析回调（如后续的 BMS CAN 帧解析）
 *  - 发送：can_service_send() 内部加锁并等待发送完成，线程安全
 *  - 自测：以 BOARD_CAN_MODE_LOOPBACK 启动后可调用 can_service_run_loopback_test()，
 *          经板载 CAN 收发器自发自收验证标准帧与扩展帧链路
 */

#ifndef _CAN_SERVICE_H_
#define _CAN_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "board_can.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_FRAME_MAX_LEN   8U

typedef struct {
    uint32_t id;                        /* 11 位标准 ID 或 29 位扩展 ID */
    uint8_t  len;                       /* 有效字节数 0~8 */
    uint8_t  data[CAN_FRAME_MAX_LEN];   /* 数据载荷 */
    bool     is_ext;                    /* true=29 位扩展帧，false=11 位标准帧 */
    bool     is_rtr;                    /* true=远程帧，false=数据帧 */
    int64_t  rx_time_us;                /* 接收时间戳（esp_timer_get_time，发送时忽略） */
} can_frame_t;

typedef struct {
    uint32_t bitrate;                   /* 波特率（bps），0 表示使用 BOARD_CAN_DEFAULT_BITRATE */
    board_can_mode_t mode;              /* NORMAL / LISTEN_ONLY / LOOPBACK */
    bool     log_rx;                    /* true=后台任务打印每帧接收日志 */
} can_service_config_t;

typedef struct {
    twai_error_state_t state;           /* 控制器错误状态（ACTIVE/WARNING/PASSIVE/BUS_OFF） */
    uint32_t rx_count;                  /* 累计接收帧数 */
    uint32_t rx_dropped;                /* 接收队列满导致丢弃的帧数 */
    uint32_t tx_count;                  /* 累计发送成功帧数 */
    uint32_t tx_failed;                 /* 累计发送失败帧数 */
    uint32_t bus_err_count;             /* 累计总线错误次数 */
    uint32_t bus_off_count;             /* 累计进入 Bus-Off 次数 */
    uint16_t tx_err_cnt;                /* 控制器发送错误计数 TEC */
    uint16_t rx_err_cnt;                /* 控制器接收错误计数 REC */
    int64_t  last_rx_us;                /* 最近一次收帧时间戳（us），0 表示尚未收到帧 */
} can_stats_t;

/**
 * @brief 帧接收回调类型（在 can_task 上下文中调用，不要阻塞过久）
 */
typedef void (*can_rx_cb_t)(const can_frame_t *frame, void *user_ctx);

/**
 * @brief 启动 CAN 服务（初始化板级 CAN 硬件、使能节点并启动后台接收任务）
 * @param cfg 可选配置，传 NULL 时使用默认波特率、NORMAL 模式并开启收帧日志
 * @return ESP_OK；当前板型未配置 CAN 时返回 ESP_ERR_NOT_SUPPORTED
 */
esp_err_t can_service_start(const can_service_config_t *cfg);

/**
 * @brief 停止 CAN 服务并释放 TWAI 节点与相关资源
 */
esp_err_t can_service_stop(void);

/**
 * @brief 发送一帧 CAN 报文（线程安全，等待底层发送完成）
 * @param frame      待发送的帧
 * @param timeout_ms 超时时间（毫秒）
 * @return ESP_OK=发送成功；ESP_ERR_TIMEOUT=超时；ESP_FAIL=总线未确认/发送失败
 */
esp_err_t can_service_send(const can_frame_t *frame, uint32_t timeout_ms);

/**
 * @brief 注册或清除上层收帧回调（线程安全，传 NULL 清除）
 */
void can_service_set_rx_callback(can_rx_cb_t cb, void *user_ctx);

/**
 * @brief 动态开关收帧日志打印（线程安全）
 */
void can_service_set_rx_log(bool enable);

/**
 * @brief 获取 CAN 服务运行统计与控制器状态（线程安全）
 */
esp_err_t can_service_get_stats(can_stats_t *out);

/**
 * @brief 执行自发自收测试（发送并校验 1 帧标准帧 + 1 帧扩展帧）
 * @note 仅在以 BOARD_CAN_MODE_LOOPBACK 模式启动服务时可用
 * @param timeout_ms 单帧等待超时（毫秒），传 0 使用默认值（500 ms）
 * @return ESP_OK=两帧均收发一致
 */
esp_err_t can_service_run_loopback_test(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* _CAN_SERVICE_H_ */
