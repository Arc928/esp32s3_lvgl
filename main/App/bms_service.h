/*
 * bms_service.h
 * BMS CAN 协议解析服务 - 挂接在 can_service 上，解析 BMS 周期广播的电池包报文并缓存最新状态
 *
 * 协议依据：《CAN 通讯协议 V1.0（第 4 版）》（500 kbps，CAN 2.0B 29 位扩展帧，大端序，250 ms 周期）：
 *  - 0x180700F4（信息 1）：总电压(0.1V)、总电流((raw-4000)*0.1A)、满充容量(0.1Ah)、SOC(0.4%)、SOH(0.4%)
 *  - 0x180701F4（信息 2）：最大/最小单体电压(1mV)、最大/最小单体位置
 *  - 0x180702F4（信息 3）：最高/最低温度((raw-400)*0.1℃)、温度位置、继电器状态位图、故障代码
 *  - 0x180703F4（信息 4）：5 字节电池包告警位图
 *
 * UI 通过 dashboard_data_board.c 调用 bms_service_get_latest() 读取最新数据（线程安全）。
 */

#ifndef _BMS_SERVICE_H_
#define _BMS_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "can_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/* BMS 电池包信息扩展帧 ID */
#define BMS_CAN_ID_PACK         0x180700F4UL
#define BMS_CAN_ID_CELLS        0x180701F4UL
#define BMS_CAN_ID_TEMP         0x180702F4UL
#define BMS_CAN_ID_ALARM        0x180703F4UL

/* 分帧接收掩码（用于记录已收到过哪些帧） */
#define BMS_FRAME_MASK_PACK     (1U << 0)
#define BMS_FRAME_MASK_CELLS    (1U << 1)
#define BMS_FRAME_MASK_TEMP     (1U << 2)
#define BMS_FRAME_MASK_ALARM    (1U << 3)
#define BMS_FRAME_MASK_ALL      (BMS_FRAME_MASK_PACK | BMS_FRAME_MASK_CELLS | \
                                 BMS_FRAME_MASK_TEMP | BMS_FRAME_MASK_ALARM)

/* 继电器状态位（0x180702F4 BYTE7） */
#define BMS_RELAY_BIT_DISCHARGE         (1U << 0)
#define BMS_RELAY_BIT_CHARGE            (1U << 1)
#define BMS_RELAY_BIT_PRECHARGE         (1U << 2)
#define BMS_RELAY_BIT_HEAT              (1U << 3)
#define BMS_RELAY_BIT_DISCHARGE_STICK   (1U << 4)
#define BMS_RELAY_BIT_CHARGE_STICK      (1U << 5)
#define BMS_RELAY_BIT_NEGATIVE          (1U << 6)
#define BMS_RELAY_BIT_NEGATIVE_STICK    (1U << 7)

typedef struct {
    /* 0x180700F4: pack */
    float pack_voltage_v;
    float pack_current_a;
    float soc_percent;
    float soh_percent;
    float full_charge_capacity_ah;

    /* 0x180701F4: cells */
    float max_cell_voltage_v;
    float min_cell_voltage_v;
    uint8_t max_cell_position;
    uint8_t min_cell_position;

    /* 0x180702F4: temperature, relays, fault */
    float max_temperature_c;
    float min_temperature_c;
    uint8_t max_temperature_position;
    uint8_t min_temperature_position;
    bool relay_charge;
    bool relay_discharge;
    bool relay_precharge;
    bool relay_heat;
    bool relay_negative;
    bool relay_charge_stick;
    bool relay_discharge_stick;
    bool relay_negative_stick;
    uint8_t relay_raw;
    uint8_t fault_code;

    /* 0x180703F4: alarms */
    bool has_alarm;
    uint8_t alarm[5];

    /* 接收状态 */
    uint8_t valid_frames;       /* BMS_FRAME_MASK_* 按位或 */
    uint32_t rx_count;          /* 累计解包成功的 BMS 帧数 */
    int64_t last_update_us;     /* 最近一次收到有效 BMS 帧的时间（esp_timer_get_time，微秒） */
} bms_can_data_t;

/**
 * @brief 启动 BMS 解析服务（清零缓存并向 can_service 注册收帧回调）
 * @note 需在 can_service_start() 之后调用
 */
esp_err_t bms_service_start(void);

/**
 * @brief 停止 BMS 解析服务（从 can_service 注销回调）
 */
void bms_service_stop(void);

/**
 * @brief 喂入一帧 CAN 报文进行 BMS 协议解包（线程安全，供 can_service 回调或测试调用）
 * @return true=属于 0x180700F4~0x180703F4 且长度合法并已更新缓存；false=非目标帧或长度不足
 */
bool bms_service_feed_frame(const can_frame_t *frame);

/**
 * @brief 读取最新缓存的 BMS 数据（线程安全）
 * @param[out] out 输出缓存快照
 * @return true=自启动以来至少收到过一帧有效 BMS 数据；false=尚未收到过数据或参数无效
 */
bool bms_service_get_latest(bms_can_data_t *out);

/**
 * @brief 将 BMS 故障码（0x180702F4 BYTE8）转为可读说明字符串
 */
const char *bms_fault_code_to_str(uint8_t fault_code);

#ifdef __cplusplus
}
#endif

#endif /* _BMS_SERVICE_H_ */
