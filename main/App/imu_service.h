/*
 * imu_service.h
 * 姿态服务 - 负责 MPU6050 的初始化、后台采集、掉线检测和最新数据缓存
 *
 * 数据有两种取法：
 *  - 定时读取：imu_service_get_latest()，适合 UI 这类按自己节奏刷新的模块
 *  - 订阅事件：APP_EVENTS / APP_IMU_DATA_UPDATED，适合需要每一帧数据的模块（如 MQTT 上传）
 *
 * 传感器没接、初始化失败或运行中掉线时，服务进入离线状态并发布 APP_SENSOR_ERROR，
 * 之后在后台定期重新初始化，传感器恢复后自动回到在线状态，整个过程不会重启设备。
 */

#ifndef _IMU_SERVICE_H_
#define _IMU_SERVICE_H_

#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "app_event.h"

typedef enum {
    IMU_STATUS_STARTING = 0,    /* 正在初始化，还没有数据 */
    IMU_STATUS_ONLINE,          /* 数据正常 */
    IMU_STATUS_OFFLINE,         /* 初始化失败或连续读不到数据，后台会自动重试 */
} imu_status_t;

/**
 * @brief 启动姿态服务（创建后台任务，传感器初始化在任务里进行）
 * @param bus MPU6050 所在的 I2C 总线（board_i2c_init 得到）
 * @return ESP_OK；任务创建失败时返回错误。传感器本身的故障不会让这里返回错误
 */
esp_err_t imu_service_start(i2c_master_bus_handle_t bus);

/**
 * @brief 获取传感器当前状态（线程安全）
 */
imu_status_t imu_service_get_status(void);

/**
 * @brief 读取最新的姿态数据（线程安全）
 * @param out 输出的姿态数据
 * @return true=在线且读到数据；false=还在初始化或已离线
 */
bool imu_service_get_latest(app_imu_data_t *out);

#endif /* _IMU_SERVICE_H_ */
