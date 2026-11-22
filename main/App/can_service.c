/*
 * can_service.c
 * CAN（TWAI）服务实现
 */

#include "can_service.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "CAN_SERVICE"

#define CAN_RX_QUEUE_LEN            32
#define CAN_TASK_STACK              4096
#define CAN_TASK_PRIO               4       /* 低于 LVGL 任务（6），不抢屏幕刷新 */
#define CAN_TASK_POLL_MS            100
#define CAN_ERR_LOG_INTERVAL_US     (2000 * 1000LL) /* 总线错误日志限频，防止空载刷屏 */
#define CAN_RECOVER_INTERVAL_US     (1000 * 1000LL) /* Bus-Off 自动恢复尝试间隔 */
#define CAN_DEFAULT_TEST_TIMEOUT_MS 500U

typedef struct {
    bool active;
    can_frame_t expected;
    SemaphoreHandle_t done_sem;
} loopback_wait_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static twai_node_handle_t s_node;
static board_can_mode_t s_mode = BOARD_CAN_MODE_NORMAL;
static QueueHandle_t s_rx_queue;
static SemaphoreHandle_t s_tx_mutex;
static TaskHandle_t s_task;

static atomic_bool s_running;
static atomic_bool s_task_exited;
static atomic_bool s_log_rx;

/* ISR 更新的计数与状态标志 */
static atomic_uint_least32_t s_rx_dropped;
static atomic_uint_least32_t s_tx_count;
static atomic_uint_least32_t s_tx_failed;
static atomic_uint_least32_t s_bus_err_count;
static atomic_uint_least32_t s_bus_off_count;
static atomic_uint_least32_t s_bus_err_flags_acc;
static atomic_bool s_last_tx_ok;
static atomic_bool s_state_dirty;
static atomic_int s_last_old_state;
static atomic_int s_last_new_state;

/* 任务更新的统计与回调（受 s_lock 保护） */
static uint32_t s_rx_count;
static int64_t s_last_rx_us;
static can_rx_cb_t s_rx_cb;
static void *s_rx_cb_ctx;
static loopback_wait_t s_loopback_wait;

/* 静态发送缓冲：esp_driver_twai 内部持有 frame/buffer 指针直到发送结束，
 * 放静态区并由 s_tx_mutex 串行化，即使发送超时也不会出现悬空指针。 */
static uint8_t s_tx_data[CAN_FRAME_MAX_LEN];
static twai_frame_t s_tx_frame;

static const char *state_to_str(twai_error_state_t state)
{
    switch (state) {
    case TWAI_ERROR_ACTIVE:
        return "error_active";
    case TWAI_ERROR_WARNING:
        return "error_warning";
    case TWAI_ERROR_PASSIVE:
        return "error_passive";
    case TWAI_ERROR_BUS_OFF:
        return "bus_off";
    default:
        return "unknown";
    }
}

/* ==================== ISR 回调（禁止打印和阻塞） ==================== */

static bool on_rx_done_cb(twai_node_handle_t handle,
                          const twai_rx_done_event_data_t *edata,
                          void *user_ctx)
{
    (void)edata;
    (void)user_ctx;

    uint8_t raw[CAN_FRAME_MAX_LEN] = {0};
    twai_frame_t rx_frame = {
        .buffer = raw,
        .buffer_len = sizeof(raw),
    };

    if (twai_node_receive_from_isr(handle, &rx_frame) != ESP_OK) {
        return false;
    }

    can_frame_t msg = {0};
    msg.id = rx_frame.header.id;
    msg.is_ext = rx_frame.header.ide;
    msg.is_rtr = rx_frame.header.rtr;
    msg.len = (rx_frame.header.dlc > CAN_FRAME_MAX_LEN)
                  ? CAN_FRAME_MAX_LEN
                  : (uint8_t)rx_frame.header.dlc;
    if (!msg.is_rtr && msg.len > 0U) {
        memcpy(msg.data, raw, msg.len);
    }
    msg.rx_time_us = esp_timer_get_time();

    BaseType_t woken = pdFALSE;
    if (s_rx_queue == NULL || xQueueSendFromISR(s_rx_queue, &msg, &woken) != pdTRUE) {
        atomic_fetch_add_explicit(&s_rx_dropped, 1U, memory_order_relaxed);
    }
    return (woken == pdTRUE);
}

static bool on_tx_done_cb(twai_node_handle_t handle,
                          const twai_tx_done_event_data_t *edata,
                          void *user_ctx)
{
    (void)handle;
    (void)user_ctx;

    bool ok = (edata != NULL) && edata->is_tx_success;
    atomic_store_explicit(&s_last_tx_ok, ok, memory_order_release);
    if (ok) {
        atomic_fetch_add_explicit(&s_tx_count, 1U, memory_order_relaxed);
    } else {
        atomic_fetch_add_explicit(&s_tx_failed, 1U, memory_order_relaxed);
    }
    return false;
}

static bool on_state_change_cb(twai_node_handle_t handle,
                               const twai_state_change_event_data_t *edata,
                               void *user_ctx)
{
    (void)handle;
    (void)user_ctx;

    if (edata == NULL) {
        return false;
    }
    atomic_store_explicit(&s_last_old_state, (int)edata->old_sta, memory_order_relaxed);
    atomic_store_explicit(&s_last_new_state, (int)edata->new_sta, memory_order_relaxed);
    atomic_store_explicit(&s_state_dirty, true, memory_order_release);
    if (edata->new_sta == TWAI_ERROR_BUS_OFF) {
        atomic_fetch_add_explicit(&s_bus_off_count, 1U, memory_order_relaxed);
    }
    return false;
}

static bool on_error_cb(twai_node_handle_t handle,
                        const twai_error_event_data_t *edata,
                        void *user_ctx)
{
    (void)handle;
    (void)user_ctx;

    atomic_fetch_add_explicit(&s_bus_err_count, 1U, memory_order_relaxed);
    if (edata != NULL) {
        atomic_fetch_or_explicit(&s_bus_err_flags_acc, edata->err_flags.val, memory_order_relaxed);
    }
    return false;
}

/* ==================== 后台任务 ==================== */

static void log_rx_frame(const can_frame_t *msg)
{
    char payload[CAN_FRAME_MAX_LEN * 3 + 1];
    size_t pos = 0;

    if (msg->is_rtr) {
        strlcpy(payload, "RTR", sizeof(payload));
    } else if (msg->len == 0U) {
        strlcpy(payload, "(empty)", sizeof(payload));
    } else {
        for (uint8_t i = 0; i < msg->len; i++) {
            int n = snprintf(payload + pos, sizeof(payload) - pos,
                             (i == 0U) ? "%02X" : " %02X", msg->data[i]);
            if (n > 0 && pos + (size_t)n < sizeof(payload)) {
                pos += (size_t)n;
            }
        }
    }

    if (msg->is_ext) {
        ESP_LOGI(TAG, "RX EXT 0x%08lX [%u] %s",
                 (unsigned long)msg->id, (unsigned)msg->len, payload);
    } else {
        ESP_LOGI(TAG, "RX STD 0x%03lX [%u] %s",
                 (unsigned long)msg->id, (unsigned)msg->len, payload);
    }
}

static void check_loopback_match(const can_frame_t *msg)
{
    SemaphoreHandle_t sem_to_give = NULL;

    portENTER_CRITICAL(&s_lock);
    if (s_loopback_wait.active &&
        msg->id == s_loopback_wait.expected.id &&
        msg->is_ext == s_loopback_wait.expected.is_ext &&
        msg->is_rtr == s_loopback_wait.expected.is_rtr &&
        msg->len == s_loopback_wait.expected.len &&
        (msg->is_rtr || memcmp(msg->data, s_loopback_wait.expected.data, msg->len) == 0)) {
        s_loopback_wait.active = false;
        sem_to_give = s_loopback_wait.done_sem;
    }
    portEXIT_CRITICAL(&s_lock);

    if (sem_to_give != NULL) {
        xSemaphoreGive(sem_to_give);
    }
}

static void handle_bus_health(int64_t *last_err_log_us, int64_t *last_recover_us)
{
    int64_t now_us = esp_timer_get_time();

    if (atomic_exchange_explicit(&s_state_dirty, false, memory_order_acq_rel)) {
        twai_error_state_t old_sta = (twai_error_state_t)atomic_load_explicit(&s_last_old_state, memory_order_relaxed);
        twai_error_state_t new_sta = (twai_error_state_t)atomic_load_explicit(&s_last_new_state, memory_order_relaxed);
        ESP_LOGW(TAG, "state changed: %s -> %s", state_to_str(old_sta), state_to_str(new_sta));
    }

    uint32_t flags = atomic_exchange_explicit(&s_bus_err_flags_acc, 0U, memory_order_relaxed);
    if (flags != 0U && (now_us - *last_err_log_us) >= CAN_ERR_LOG_INTERVAL_US) {
        twai_error_flags_t ef = { .val = flags };
        twai_node_status_t status = {0};
        (void)twai_node_get_info(s_node, &status, NULL);
        ESP_LOGW(TAG, "bus error (total=%lu, flags=0x%lX: ack=%u bit=%u stuff=%u form=%u arb=%u, TEC=%u REC=%u)",
                 (unsigned long)atomic_load_explicit(&s_bus_err_count, memory_order_relaxed),
                 (unsigned long)flags,
                 (unsigned)ef.ack_err, (unsigned)ef.bit_err, (unsigned)ef.stuff_err,
                 (unsigned)ef.form_err, (unsigned)ef.arb_lost,
                 (unsigned)status.tx_error_count, (unsigned)status.rx_error_count);
        *last_err_log_us = now_us;
    }

    twai_node_status_t status = {0};
    if (twai_node_get_info(s_node, &status, NULL) == ESP_OK &&
        status.state == TWAI_ERROR_BUS_OFF &&
        (now_us - *last_recover_us) >= CAN_RECOVER_INTERVAL_US) {
        esp_err_t err = twai_node_recover(s_node);
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "bus-off detected, recovery initiated");
        } else {
            ESP_LOGE(TAG, "bus-off recovery failed (%s)", esp_err_to_name(err));
        }
        *last_recover_us = now_us;
    }
}

static void can_task(void *arg)
{
    (void)arg;
    int64_t last_err_log_us = 0;
    int64_t last_recover_us = 0;
    can_frame_t msg;

    while (atomic_load_explicit(&s_running, memory_order_acquire)) {
        if (xQueueReceive(s_rx_queue, &msg, pdMS_TO_TICKS(CAN_TASK_POLL_MS)) == pdTRUE) {
            can_rx_cb_t cb;
            void *cb_ctx;

            portENTER_CRITICAL(&s_lock);
            s_rx_count++;
            s_last_rx_us = msg.rx_time_us;
            cb = s_rx_cb;
            cb_ctx = s_rx_cb_ctx;
            portEXIT_CRITICAL(&s_lock);

            if (atomic_load_explicit(&s_log_rx, memory_order_relaxed)) {
                log_rx_frame(&msg);
            }

            check_loopback_match(&msg);

            if (cb != NULL) {
                cb(&msg, cb_ctx);
            }
        }

        handle_bus_health(&last_err_log_us, &last_recover_us);
    }

    atomic_store_explicit(&s_task_exited, true, memory_order_release);
    vTaskDelete(NULL);
}

/* ==================== 公开接口 ==================== */

esp_err_t can_service_start(const can_service_config_t *cfg)
{
    if (s_node != NULL || s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    can_service_config_t effective = {
        .bitrate = 0,
        .mode = BOARD_CAN_MODE_NORMAL,
        .log_rx = true,
    };
    if (cfg != NULL) {
        effective = *cfg;
    }

    /* 重置状态和计数 */
    atomic_store(&s_rx_dropped, 0U);
    atomic_store(&s_tx_count, 0U);
    atomic_store(&s_tx_failed, 0U);
    atomic_store(&s_bus_err_count, 0U);
    atomic_store(&s_bus_off_count, 0U);
    atomic_store(&s_bus_err_flags_acc, 0U);
    atomic_store(&s_last_tx_ok, false);
    atomic_store(&s_state_dirty, false);
    atomic_store(&s_log_rx, effective.log_rx);
    atomic_store(&s_task_exited, false);

    portENTER_CRITICAL(&s_lock);
    s_rx_count = 0U;
    s_last_rx_us = 0;
    s_loopback_wait.active = false;
    s_loopback_wait.done_sem = NULL;
    portEXIT_CRITICAL(&s_lock);

    esp_err_t ret = ESP_OK;

    s_rx_queue = xQueueCreate(CAN_RX_QUEUE_LEN, sizeof(can_frame_t));
    ESP_GOTO_ON_FALSE(s_rx_queue != NULL, ESP_ERR_NO_MEM, err, TAG, "no mem for rx queue");

    s_tx_mutex = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(s_tx_mutex != NULL, ESP_ERR_NO_MEM, err, TAG, "no mem for tx mutex");

    board_can_config_t board_cfg = {
        .bitrate = effective.bitrate,
        .mode = effective.mode,
        .tx_queue_depth = 8,
        .single_shot = false,
    };
    ESP_GOTO_ON_ERROR(board_can_init(&board_cfg, &s_node), err, TAG, "board_can_init failed");
    s_mode = effective.mode;

    twai_event_callbacks_t cbs = {
        .on_tx_done = on_tx_done_cb,
        .on_rx_done = on_rx_done_cb,
        .on_state_change = on_state_change_cb,
        .on_error = on_error_cb,
    };
    ESP_GOTO_ON_ERROR(twai_node_register_event_callbacks(s_node, &cbs, NULL),
                      err, TAG, "register callbacks failed");

    ESP_GOTO_ON_ERROR(twai_node_enable(s_node), err, TAG, "enable TWAI node failed");

    atomic_store(&s_running, true);
    if (xTaskCreate(can_task, "can_task", CAN_TASK_STACK, NULL, CAN_TASK_PRIO, &s_task) != pdPASS) {
        atomic_store(&s_running, false);
        (void)twai_node_disable(s_node);
        ret = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "failed to create can_task");
        goto err;
    }

    ESP_LOGI(TAG, "CAN service started");
    return ESP_OK;

err:
    if (s_node != NULL) {
        (void)board_can_deinit(s_node);
        s_node = NULL;
    }
    if (s_tx_mutex != NULL) {
        vSemaphoreDelete(s_tx_mutex);
        s_tx_mutex = NULL;
    }
    if (s_rx_queue != NULL) {
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
    }
    return ret;
}

esp_err_t can_service_stop(void)
{
    if (s_node == NULL && s_task == NULL) {
        return ESP_OK;
    }

    atomic_store_explicit(&s_running, false, memory_order_release);

    if (s_task != NULL) {
        /* 最多等待 500 ms 让后台任务自然退出 */
        for (int i = 0; i < 50 && !atomic_load_explicit(&s_task_exited, memory_order_acquire); i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        s_task = NULL;
    }

    if (s_tx_mutex != NULL) {
        xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    }

    if (s_node != NULL) {
        /* 若已处于 BUS_OFF，disable 会返回 ESP_ERR_INVALID_STATE，可继续 delete */
        esp_err_t dis_err = twai_node_disable(s_node);
        if (dis_err != ESP_OK && dis_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "twai_node_disable returned %s", esp_err_to_name(dis_err));
        }
        (void)board_can_deinit(s_node);
        s_node = NULL;
    }

    if (s_tx_mutex != NULL) {
        xSemaphoreGive(s_tx_mutex);
        vSemaphoreDelete(s_tx_mutex);
        s_tx_mutex = NULL;
    }
    if (s_rx_queue != NULL) {
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
    }

    ESP_LOGI(TAG, "CAN service stopped");
    return ESP_OK;
}

esp_err_t can_service_send(const can_frame_t *frame, uint32_t timeout_ms)
{
    if (frame == NULL || frame->len > CAN_FRAME_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t max_id = frame->is_ext ? TWAI_EXT_ID_MASK : TWAI_STD_ID_MASK;
    if (frame->id > max_id) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!atomic_load_explicit(&s_running, memory_order_acquire) || s_node == NULL || s_tx_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_mode == BOARD_CAN_MODE_LISTEN_ONLY) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    int tx_timeout_ms = (timeout_ms > (uint32_t)INT_MAX) ? INT_MAX : (int)timeout_ms;

    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    if (!atomic_load_explicit(&s_running, memory_order_acquire) || s_node == NULL) {
        ret = ESP_ERR_INVALID_STATE;
        goto out;
    }

    /* 先确认上一次（可能超时未完的）发送已释放静态缓冲 */
    ret = twai_node_transmit_wait_all_done(s_node, tx_timeout_ms);
    if (ret != ESP_OK) {
        goto out;
    }

    memset(&s_tx_frame, 0, sizeof(s_tx_frame));
    s_tx_frame.header.id = frame->id;
    s_tx_frame.header.dlc = frame->len;
    s_tx_frame.header.ide = frame->is_ext;
    s_tx_frame.header.rtr = frame->is_rtr;
    if (!frame->is_rtr && frame->len > 0U) {
        memcpy(s_tx_data, frame->data, frame->len);
        s_tx_frame.buffer = s_tx_data;
        s_tx_frame.buffer_len = frame->len;
    } else {
        s_tx_frame.buffer = NULL;
        s_tx_frame.buffer_len = 0;
    }

    atomic_store_explicit(&s_last_tx_ok, false, memory_order_release);

    ret = twai_node_transmit(s_node, &s_tx_frame, tx_timeout_ms);
    if (ret != ESP_OK) {
        goto out;
    }

    ret = twai_node_transmit_wait_all_done(s_node, tx_timeout_ms);
    if (ret == ESP_OK && !atomic_load_explicit(&s_last_tx_ok, memory_order_acquire)) {
        ret = ESP_FAIL;
    }

out:
    xSemaphoreGive(s_tx_mutex);
    return ret;
}

void can_service_set_rx_callback(can_rx_cb_t cb, void *user_ctx)
{
    portENTER_CRITICAL(&s_lock);
    s_rx_cb = cb;
    s_rx_cb_ctx = user_ctx;
    portEXIT_CRITICAL(&s_lock);
}

void can_service_set_rx_log(bool enable)
{
    atomic_store_explicit(&s_log_rx, enable, memory_order_relaxed);
}

esp_err_t can_service_get_stats(can_stats_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_node == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(out, 0, sizeof(*out));

    twai_node_status_t status = {0};
    esp_err_t ret = twai_node_get_info(s_node, &status, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    out->state = status.state;
    out->tx_err_cnt = status.tx_error_count;
    out->rx_err_cnt = status.rx_error_count;
    out->rx_dropped = atomic_load_explicit(&s_rx_dropped, memory_order_relaxed);
    out->tx_count = atomic_load_explicit(&s_tx_count, memory_order_relaxed);
    out->tx_failed = atomic_load_explicit(&s_tx_failed, memory_order_relaxed);
    out->bus_err_count = atomic_load_explicit(&s_bus_err_count, memory_order_relaxed);
    out->bus_off_count = atomic_load_explicit(&s_bus_off_count, memory_order_relaxed);

    portENTER_CRITICAL(&s_lock);
    out->rx_count = s_rx_count;
    out->last_rx_us = s_last_rx_us;
    portEXIT_CRITICAL(&s_lock);

    return ESP_OK;
}

static esp_err_t run_one_loopback_frame(const can_frame_t *frame,
                                        SemaphoreHandle_t sem,
                                        uint32_t timeout_ms)
{
    /* 清空可能残留的信号量并在发送前挂载匹配条件，防止竞态漏帧 */
    (void)xSemaphoreTake(sem, 0);
    portENTER_CRITICAL(&s_lock);
    s_loopback_wait.expected = *frame;
    s_loopback_wait.done_sem = sem;
    s_loopback_wait.active = true;
    portEXIT_CRITICAL(&s_lock);

    esp_err_t ret = can_service_send(frame, timeout_ms);
    if (ret == ESP_OK) {
        if (xSemaphoreTake(sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            ret = ESP_ERR_TIMEOUT;
        }
    }

    portENTER_CRITICAL(&s_lock);
    s_loopback_wait.active = false;
    s_loopback_wait.done_sem = NULL;
    portEXIT_CRITICAL(&s_lock);

    return ret;
}

esp_err_t can_service_run_loopback_test(uint32_t timeout_ms)
{
    if (!atomic_load_explicit(&s_running, memory_order_acquire) || s_node == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_mode != BOARD_CAN_MODE_LOOPBACK) {
        ESP_LOGE(TAG, "loopback test requires BOARD_CAN_MODE_LOOPBACK");
        return ESP_ERR_INVALID_STATE;
    }
    if (timeout_ms == 0U) {
        timeout_ms = CAN_DEFAULT_TEST_TIMEOUT_MS;
    }

    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    if (sem == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const can_frame_t std_frame = {
        .id = 0x123U,
        .len = 8U,
        .data = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 },
        .is_ext = false,
        .is_rtr = false,
    };
    const can_frame_t ext_frame = {
        .id = 0x180700F4U,
        .len = 8U,
        .data = { 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x08 },
        .is_ext = true,
        .is_rtr = false,
    };

    esp_err_t ret = run_one_loopback_frame(&std_frame, sem, timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "loopback self-test FAILED on STD frame 0x%03lX (%s)",
                 (unsigned long)std_frame.id, esp_err_to_name(ret));
        vSemaphoreDelete(sem);
        return ret;
    }

    ret = run_one_loopback_frame(&ext_frame, sem, timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "loopback self-test FAILED on EXT frame 0x%08lX (%s)",
                 (unsigned long)ext_frame.id, esp_err_to_name(ret));
        vSemaphoreDelete(sem);
        return ret;
    }

    vSemaphoreDelete(sem);
    ESP_LOGI(TAG, "CAN loopback self-test PASSED (STD 0x%03lX + EXT 0x%08lX)",
             (unsigned long)std_frame.id, (unsigned long)ext_frame.id);
    return ESP_OK;
}
