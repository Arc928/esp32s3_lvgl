/*
 * Data sources for the dashboard.
 *
 * The view only reads these two functions. On the simulator they are served by
 * dashboard_data_mock.c; on the board, set DASHBOARD_DATA_MOCK to 0 and implement
 * them on top of bms_can_get_latest() and the MPU6050 driver.
 */

#ifndef DASHBOARD_DATA_H
#define DASHBOARD_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef DASHBOARD_DATA_MOCK
#define DASHBOARD_DATA_MOCK 1
#endif

/* Field names match bms_can_data_t (see docs/bms_can_display_design.md). */
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

    /* Receiver state */
    uint32_t rx_count;
    uint32_t last_update_ms; /* lv_tick_get() time of the last decoded frame */
} bms_data_t;

typedef struct {
    float pitch_deg;
    float roll_deg;
    float yaw_deg;
} imu_data_t;

/* Downlink MQTT device control command (matches app_device_control_t on ESP32). */
#define MQTT_CMD_DEVICE_MIN         1U
#define MQTT_CMD_DEVICE_MAX         9U
#define MQTT_CMD_OTHER_MAX_LEN      128U
#define MQTT_CMD_PARAMS_MAX_LEN     256U

typedef enum {
    MQTT_CMD_ACTION_START = 0,      /* start: 开始作业 */
    MQTT_CMD_ACTION_STOP,           /* stop: 停止作业 */
    MQTT_CMD_ACTION_RESET,          /* reset: 回停车场 */
    MQTT_CMD_ACTION_MAINTENANCE,    /* maintenance: 维护 */
    MQTT_CMD_ACTION_FORWARD,        /* forward: 向前行驶 */
    MQTT_CMD_ACTION_BACKWARD,       /* backward: 向后行驶 */
    MQTT_CMD_ACTION_LEFT,           /* left: 向左行驶 */
    MQTT_CMD_ACTION_RIGHT,          /* right: 向右行驶 */
    MQTT_CMD_ACTION_OTHER,          /* other: 其他自定义命令 */
} mqtt_cmd_action_t;

typedef struct {
    uint32_t seq;                                   /* Monotonic sequence number (>0 when valid) */
    uint8_t device_id;                              /* 1..9 */
    mqtt_cmd_action_t action;
    int64_t timestamp_ms;                           /* Broker message epoch ms, or 0 if omitted */
    uint32_t received_tick_ms;                      /* lv_tick_get() when received locally */
    char other_cmd_info[MQTT_CMD_OTHER_MAX_LEN];    /* Custom command text when action == OTHER */
    char parameters[MQTT_CMD_PARAMS_MAX_LEN];       /* Compact JSON object string, "{}" when empty */
} mqtt_cmd_data_t;

/* Both return false until the first valid sample has been received. */
bool bms_data_get_latest(bms_data_t *out);
bool imu_data_get_latest(imu_data_t *out);

/* MQTT connection state and latest downlink control command. */
void mqtt_data_init(void);
bool mqtt_data_is_connected(void);
bool mqtt_data_get_latest_cmd(mqtt_cmd_data_t *out);
bool mqtt_data_get_device_cmd(uint8_t device_id, mqtt_cmd_data_t *out);

/* Wi-Fi STA scan and connection interface (backed by net_service on board). */
#define WIFI_DATA_SSID_MAX_LEN      32U
#define WIFI_DATA_PASS_MIN_LEN      8U
#define WIFI_DATA_PASS_MAX_LEN      63U
#define WIFI_DATA_SCAN_MAX_APS      24U

typedef enum {
    WIFI_DATA_OK = 0,
    WIFI_DATA_ERR_INVALID_ARG,
    WIFI_DATA_ERR_NOT_READY,
    WIFI_DATA_ERR_BUSY,
    WIFI_DATA_ERR_NO_MEM,
    WIFI_DATA_ERR_FAIL,
} wifi_data_err_t;

typedef enum {
    WIFI_DATA_AUTH_OPEN = 0,
    WIFI_DATA_AUTH_PSK,
    WIFI_DATA_AUTH_UNSUPPORTED,
} wifi_data_auth_t;

typedef struct {
    char ssid[WIFI_DATA_SSID_MAX_LEN + 1];
    int8_t rssi;
    wifi_data_auth_t auth;
} wifi_data_ap_t;

typedef enum {
    WIFI_DATA_SCAN_IDLE = 0,
    WIFI_DATA_SCAN_RUNNING,
    WIFI_DATA_SCAN_DONE,
    WIFI_DATA_SCAN_FAILED,
} wifi_data_scan_state_t;

typedef enum {
    WIFI_DATA_STATE_DISCONNECTED = 0,
    WIFI_DATA_STATE_CONNECTING,
    WIFI_DATA_STATE_CONNECTED,
    WIFI_DATA_STATE_FAILED,
} wifi_data_state_t;

typedef enum {
    WIFI_DATA_FAIL_NONE = 0,
    WIFI_DATA_FAIL_WRONG_PASSWORD,
    WIFI_DATA_FAIL_NOT_FOUND,
    WIFI_DATA_FAIL_OTHER,
} wifi_data_fail_t;

typedef struct {
    wifi_data_state_t state;
    wifi_data_fail_t fail;
    char ssid[WIFI_DATA_SSID_MAX_LEN + 1];
} wifi_data_status_t;

wifi_data_err_t wifi_data_scan_start(void);
wifi_data_scan_state_t wifi_data_scan_get(wifi_data_ap_t *aps, size_t max, size_t *count);
wifi_data_err_t wifi_data_connect(const char *ssid, const char *password);
void wifi_data_disconnect(void);
void wifi_data_get_status(wifi_data_status_t *out);

#if DASHBOARD_DATA_MOCK
/* Simulator helper: pushes the next sample MQTT command immediately for UI review. */
void mqtt_data_mock_trigger_next(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* DASHBOARD_DATA_H */
