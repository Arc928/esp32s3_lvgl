/*
 * time_service.h
 * 时间服务 - 设置时区，拿到 IP 后通过 SNTP 对时
 *
 * 对时结果通过 APP_EVENTS 发布：APP_TIME_SYNC_STARTED / APP_TIME_SYNCED / APP_TIME_SYNC_FAILED
 */

#ifndef _TIME_SERVICE_H_
#define _TIME_SERVICE_H_

/**
 * @brief 设置时区并订阅 IP_EVENT_STA_GOT_IP，每次联网后自动对时
 * @note 需在联网（net_service_start）之前调用
 */
void time_service_start(void);

#endif /* _TIME_SERVICE_H_ */
