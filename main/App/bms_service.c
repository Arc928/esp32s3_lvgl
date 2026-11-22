/*
 * bms_service.c
 * BMS CAN 协议解析服务实现
 */

#include "bms_service.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "BMS_SERVICE"

/* 0x180703F4 BYTE3（alarm[2]）的 Bit1 协议标为 NC，不计入 has_alarm */
#define BMS_ALARM3_NC_MASK          (1U << 1)

/* 周期摘要日志间隔（微秒），避免 250 ms 一轮刷屏 */
#define BMS_SUMMARY_LOG_INTERVAL_US (5000 * 1000LL)

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bms_can_data_t s_data;
static bool s_started;
static int64_t s_last_summary_log_us;

static inline uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static bool decode_pack_frame(const can_frame_t *frame, bms_can_data_t *dst)
{
    if (frame->len < 8U) {
        return false;
    }

    const uint8_t *d = frame->data;
    dst->pack_voltage_v = (float)read_be16(&d[0]) * 0.1f;
    dst->pack_current_a = (float)((int32_t)read_be16(&d[2]) - 4000) * 0.1f;
    dst->full_charge_capacity_ah = (float)read_be16(&d[4]) * 0.1f;
    dst->soc_percent = (float)d[6] * 0.4f;
    dst->soh_percent = (float)d[7] * 0.4f;
    dst->valid_frames |= BMS_FRAME_MASK_PACK;
    return true;
}

static bool decode_cells_frame(const can_frame_t *frame, bms_can_data_t *dst)
{
    if (frame->len < 6U) {
        return false;
    }

    const uint8_t *d = frame->data;
    dst->max_cell_voltage_v = (float)read_be16(&d[0]) * 0.001f;
    dst->min_cell_voltage_v = (float)read_be16(&d[2]) * 0.001f;
    dst->max_cell_position = d[4];
    dst->min_cell_position = d[5];
    dst->valid_frames |= BMS_FRAME_MASK_CELLS;
    return true;
}

static bool decode_temp_frame(const can_frame_t *frame, bms_can_data_t *dst)
{
    if (frame->len < 8U) {
        return false;
    }

    const uint8_t *d = frame->data;
    uint8_t relay = d[6];

    dst->max_temperature_c = (float)((int32_t)read_be16(&d[0]) - 400) * 0.1f;
    dst->min_temperature_c = (float)((int32_t)read_be16(&d[2]) - 400) * 0.1f;
    dst->max_temperature_position = d[4];
    dst->min_temperature_position = d[5];

    dst->relay_raw = relay;
    dst->relay_discharge = (relay & BMS_RELAY_BIT_DISCHARGE) != 0U;
    dst->relay_charge = (relay & BMS_RELAY_BIT_CHARGE) != 0U;
    dst->relay_precharge = (relay & BMS_RELAY_BIT_PRECHARGE) != 0U;
    dst->relay_heat = (relay & BMS_RELAY_BIT_HEAT) != 0U;
    dst->relay_discharge_stick = (relay & BMS_RELAY_BIT_DISCHARGE_STICK) != 0U;
    dst->relay_charge_stick = (relay & BMS_RELAY_BIT_CHARGE_STICK) != 0U;
    dst->relay_negative = (relay & BMS_RELAY_BIT_NEGATIVE) != 0U;
    dst->relay_negative_stick = (relay & BMS_RELAY_BIT_NEGATIVE_STICK) != 0U;

    dst->fault_code = d[7];
    dst->valid_frames |= BMS_FRAME_MASK_TEMP;
    return true;
}

static bool decode_alarm_frame(const can_frame_t *frame, bms_can_data_t *dst)
{
    if (frame->len < 5U) {
        return false;
    }

    const uint8_t *d = frame->data;
    memcpy(dst->alarm, d, sizeof(dst->alarm));
    dst->has_alarm = (dst->alarm[0] != 0U) ||
                     (dst->alarm[1] != 0U) ||
                     ((dst->alarm[2] & (uint8_t)~BMS_ALARM3_NC_MASK) != 0U) ||
                     (dst->alarm[3] != 0U) ||
                     (dst->alarm[4] != 0U);
    dst->valid_frames |= BMS_FRAME_MASK_ALARM;
    return true;
}

static void on_can_rx(const can_frame_t *frame, void *user_ctx)
{
    (void)user_ctx;
    (void)bms_service_feed_frame(frame);
}

bool bms_service_feed_frame(const can_frame_t *frame)
{
    if (frame == NULL || !frame->is_ext || frame->is_rtr) {
        return false;
    }

    if (frame->id != BMS_CAN_ID_PACK &&
        frame->id != BMS_CAN_ID_CELLS &&
        frame->id != BMS_CAN_ID_TEMP &&
        frame->id != BMS_CAN_ID_ALARM) {
        return false;
    }

    int64_t now_us = (frame->rx_time_us > 0) ? frame->rx_time_us : esp_timer_get_time();

    bool first_frame = false;
    bool fault_changed = false;
    bool alarm_changed = false;
    bool relay_changed = false;
    uint8_t prev_fault = 0;
    uint8_t prev_relay = 0;
    bool decoded = false;
    bms_can_data_t snapshot;

    portENTER_CRITICAL(&s_lock);
    bool had_temp = (s_data.valid_frames & BMS_FRAME_MASK_TEMP) != 0U;
    bool had_alarm = (s_data.valid_frames & BMS_FRAME_MASK_ALARM) != 0U;
    prev_fault = s_data.fault_code;
    prev_relay = s_data.relay_raw;
    bool prev_has_alarm = s_data.has_alarm;

    switch (frame->id) {
    case BMS_CAN_ID_PACK:
        decoded = decode_pack_frame(frame, &s_data);
        break;
    case BMS_CAN_ID_CELLS:
        decoded = decode_cells_frame(frame, &s_data);
        break;
    case BMS_CAN_ID_TEMP:
        decoded = decode_temp_frame(frame, &s_data);
        if (decoded && had_temp) {
            fault_changed = (s_data.fault_code != prev_fault);
            relay_changed = (s_data.relay_raw != prev_relay);
        } else if (decoded && s_data.fault_code != 0U) {
            fault_changed = true;
        }
        break;
    case BMS_CAN_ID_ALARM:
        decoded = decode_alarm_frame(frame, &s_data);
        if (decoded && had_alarm) {
            alarm_changed = (s_data.has_alarm != prev_has_alarm);
        } else if (decoded && s_data.has_alarm) {
            alarm_changed = true;
        }
        break;
    default:
        break;
    }

    if (decoded) {
        first_frame = (s_data.rx_count == 0U);
        s_data.rx_count++;
        s_data.last_update_us = now_us;
        snapshot = s_data;
    }
    portEXIT_CRITICAL(&s_lock);

    if (!decoded) {
        ESP_LOGW(TAG, "ignored short BMS frame 0x%08lX (len=%u)",
                 (unsigned long)frame->id, (unsigned)frame->len);
        return false;
    }

    if (first_frame) {
        ESP_LOGI(TAG, "BMS CAN online (first frame 0x%08lX)", (unsigned long)frame->id);
    }
    if (fault_changed) {
        ESP_LOGW(TAG, "BMS fault code: 0x%02X -> 0x%02X (%s)",
                 (unsigned)prev_fault, (unsigned)snapshot.fault_code,
                 bms_fault_code_to_str(snapshot.fault_code));
    }
    if (relay_changed) {
        ESP_LOGI(TAG, "BMS relay: 0x%02X -> 0x%02X (chg=%u dsg=%u pre=%u heat=%u neg=%u stick=%u/%u/%u)",
                 (unsigned)prev_relay, (unsigned)snapshot.relay_raw,
                 (unsigned)snapshot.relay_charge, (unsigned)snapshot.relay_discharge,
                 (unsigned)snapshot.relay_precharge, (unsigned)snapshot.relay_heat,
                 (unsigned)snapshot.relay_negative,
                 (unsigned)snapshot.relay_charge_stick, (unsigned)snapshot.relay_discharge_stick,
                 (unsigned)snapshot.relay_negative_stick);
    }
    if (alarm_changed) {
        ESP_LOGW(TAG, "BMS alarm %s: [%02X %02X %02X %02X %02X]",
                 snapshot.has_alarm ? "ACTIVE" : "CLEARED",
                 (unsigned)snapshot.alarm[0], (unsigned)snapshot.alarm[1],
                 (unsigned)snapshot.alarm[2], (unsigned)snapshot.alarm[3],
                 (unsigned)snapshot.alarm[4]);
    }

    if ((now_us - s_last_summary_log_us) >= BMS_SUMMARY_LOG_INTERVAL_US) {
        s_last_summary_log_us = now_us;
        ESP_LOGI(TAG, "BMS V=%.1fV I=%.1fA SOC=%.1f%% SOH=%.1f%% Cell=%.3f~%.3fV Temp=%.1f~%.1fC Relay=0x%02X Fault=0x%02X Alarm=%u (rx=%lu)",
                 snapshot.pack_voltage_v, snapshot.pack_current_a,
                 snapshot.soc_percent, snapshot.soh_percent,
                 snapshot.min_cell_voltage_v, snapshot.max_cell_voltage_v,
                 snapshot.min_temperature_c, snapshot.max_temperature_c,
                 (unsigned)snapshot.relay_raw, (unsigned)snapshot.fault_code,
                 (unsigned)snapshot.has_alarm, (unsigned long)snapshot.rx_count);
    }

    return true;
}

esp_err_t bms_service_start(void)
{
    portENTER_CRITICAL(&s_lock);
    memset(&s_data, 0, sizeof(s_data));
    s_started = true;
    portEXIT_CRITICAL(&s_lock);

    s_last_summary_log_us = 0;
    can_service_set_rx_callback(on_can_rx, NULL);

    ESP_LOGI(TAG, "BMS CAN parser started (listening 0x%08lX..0x%08lX)",
             (unsigned long)BMS_CAN_ID_PACK, (unsigned long)BMS_CAN_ID_ALARM);
    return ESP_OK;
}

void bms_service_stop(void)
{
    if (!s_started) {
        return;
    }

    can_service_set_rx_callback(NULL, NULL);

    portENTER_CRITICAL(&s_lock);
    s_started = false;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "BMS CAN parser stopped");
}

bool bms_service_get_latest(bms_can_data_t *out)
{
    if (out == NULL) {
        return false;
    }

    bool valid = false;
    portENTER_CRITICAL(&s_lock);
    if (s_data.rx_count > 0U) {
        *out = s_data;
        valid = true;
    }
    portEXIT_CRITICAL(&s_lock);

    return valid;
}

const char *bms_fault_code_to_str(uint8_t fault_code)
{
    switch (fault_code) {
    case 0:   return "正常";
    /* 一级严重 */
    case 1:   return "单体过压异常";
    case 2:   return "单体过压";
    case 3:   return "总压过压";
    case 4:   return "单体欠压";
    case 5:   return "总压欠压";
    case 6:   return "单体欠压异常";
    case 7:   return "充电高温";
    case 8:   return "充电低温";
    case 9:   return "放电高温";
    case 10:  return "放电低温";
    case 11:  return "单体压差大";
    case 12:  return "电芯温差大";
    case 13:  return "充电过流";
    case 14:  return "放电过流1级";
    case 15:  return "放电过流2级";
    case 16:  return "充电继电器状态异常";
    case 17:  return "放电继电器状态异常";
    case 18:  return "总负继电器状态异常";
    case 19:  return "加热继电器状态异常";
    case 21:  return "加热器异常";
    /* 二级重要 */
    case 49:  return "电量高告警";
    case 50:  return "电量低告警";
    case 51:  return "单体高告警";
    case 52:  return "总压高告警";
    case 53:  return "单体低告警";
    case 54:  return "总压低告警";
    case 55:  return "充电高温告警";
    case 56:  return "充电低温告警";
    case 57:  return "放电高温告警";
    case 58:  return "放电低温告警";
    case 59:  return "压差大告警";
    case 60:  return "温差大告警";
    case 61:  return "充电电流告警";
    case 62:  return "放电电流告警";
    case 100: return "通信超时";
    default:  return "未知故障码";
    }
}
