/*
 * 界面数据源（板上实现）
 *
 * custom/ 里的界面代码来自 lvgl_bms_ui 模拟器项目，只通过 dashboard_data.h 的两个函数取数据。
 * 模拟器用 dashboard_data_mock.c 的假数据；板上在 main/CMakeLists.txt 里定义
 * DASHBOARD_DATA_MOCK=0，改由这里从各服务读取。
 *
 * 两个函数都在 LVGL 定时器里调用，服务的取数接口是线程安全的。
 */

#include "dashboard_data.h"

#if !DASHBOARD_DATA_MOCK

#include "imu_service.h"

/*
 * 本项目还没有 BMS CAN 接收服务，先一直返回 false，界面显示“CAN 离线”。
 * 接入后在这里把最新一帧拷到 out，字段名与 bms_can_data_t 一致，
 * last_update_ms 要用 lv_tick_get() 的时间基准（界面靠它判断 3 秒离线）。
 */
bool bms_data_get_latest(bms_data_t *out)
{
    (void)out;
    return false;
}

/* 传感器还在初始化或已离线时返回 false，界面显示 "--" */
bool imu_data_get_latest(imu_data_t *out)
{
    app_imu_data_t data;

    if (!imu_service_get_latest(&data)) {
        return false;
    }
    out->pitch_deg = data.pitch;
    out->roll_deg = data.roll;
    out->yaw_deg = data.yaw;
    return true;
}

#endif /* !DASHBOARD_DATA_MOCK */
