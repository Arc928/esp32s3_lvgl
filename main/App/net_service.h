/*
 * net_service.h
 * 网络服务 - Wi-Fi STA 连接
 *
 * Wi-Fi 名称和密码放在 wifi_secrets.h（不提交到 git），
 * 新环境请复制 wifi_secrets.h.example 为 wifi_secrets.h 后填写。
 * 连接状态请订阅 ESP-IDF 自带的 WIFI_EVENT / IP_EVENT（如 IP_EVENT_STA_GOT_IP）。
 */

#ifndef _NET_SERVICE_H_
#define _NET_SERVICE_H_

/**
 * @brief 连接 Wi-Fi（STA 模式），阻塞到连上或重试失败为止
 */
void net_service_start(void);

#endif /* _NET_SERVICE_H_ */
