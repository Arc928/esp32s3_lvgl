#include "board_can.h"

#include <stddef.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_twai_onchip.h"
#include "board_config.h"
#if BOARD_HAS_EXIO
#include "board_exio.h"
#endif

#define TAG "board_can"

#define BOARD_CAN_DEFAULT_TX_QUEUE_DEPTH    8

esp_err_t board_can_init(const board_can_config_t *cfg, twai_node_handle_t *node)
{
    if (node == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *node = NULL;

#if !BOARD_HAS_CAN
    (void)cfg;
    ESP_LOGW(TAG, "CAN not configured on this board");
    return ESP_ERR_NOT_SUPPORTED;
#else
    board_can_config_t effective = {
        .bitrate = BOARD_CAN_DEFAULT_BITRATE,
        .mode = BOARD_CAN_MODE_NORMAL,
        .tx_queue_depth = BOARD_CAN_DEFAULT_TX_QUEUE_DEPTH,
        .single_shot = false,
    };
    if (cfg != NULL) {
        if (cfg->bitrate != 0U) {
            effective.bitrate = cfg->bitrate;
        }
        effective.mode = cfg->mode;
        if (cfg->tx_queue_depth != 0U) {
            effective.tx_queue_depth = cfg->tx_queue_depth;
        }
        effective.single_shot = cfg->single_shot;
    }

    if (effective.mode != BOARD_CAN_MODE_NORMAL &&
        effective.mode != BOARD_CAN_MODE_LISTEN_ONLY &&
        effective.mode != BOARD_CAN_MODE_LOOPBACK) {
        return ESP_ERR_INVALID_ARG;
    }

    bool exio_switched = false;
#if BOARD_HAS_EXIO && defined(BOARD_EXIO_USB_SEL)
    /* 将 GPIO19/20 从原生 USB 切到板载 CAN 收发器（EXIO5 高电平=CAN） */
    ESP_RETURN_ON_ERROR(board_exio_set(BOARD_EXIO_USB_SEL, true),
                        TAG, "failed to switch EXIO%d to CAN mode (is CH422G initialized?)",
                        BOARD_EXIO_USB_SEL);
    exio_switched = true;
#endif

    bool listen_only = (effective.mode == BOARD_CAN_MODE_LISTEN_ONLY);
    bool loopback = (effective.mode == BOARD_CAN_MODE_LOOPBACK);

    twai_onchip_node_config_t node_cfg = {
        .io_cfg = {
            .tx = BOARD_CAN_PIN_TX,
            .rx = BOARD_CAN_PIN_RX,
            /* 未使用的可选引脚必须显式设为 GPIO_NUM_NC，避免默认 0 误占 GPIO0（LCD_DATA6） */
            .quanta_clk_out = GPIO_NUM_NC,
            .bus_off_indicator = GPIO_NUM_NC,
        },
        .clk_src = 0,
        .bit_timing = {
            .bitrate = effective.bitrate,
        },
        .fail_retry_cnt = effective.single_shot ? 0 : -1,
        .tx_queue_depth = listen_only ? 0 : effective.tx_queue_depth,
        .flags = {
            .enable_self_test = loopback,
            .enable_loopback = loopback,
            .enable_listen_only = listen_only,
        },
    };

    esp_err_t ret = twai_new_node_onchip(&node_cfg, node);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to create TWAI node (%s)", esp_err_to_name(ret));
#if BOARD_HAS_EXIO && defined(BOARD_EXIO_USB_SEL)
        if (exio_switched) {
            (void)board_exio_set(BOARD_EXIO_USB_SEL, false);
        }
#else
        (void)exio_switched;
#endif
        return ret;
    }

    ESP_LOGI(TAG, "TWAI node ready (TX=GPIO%d, RX=GPIO%d, bitrate=%lu, mode=%s)",
             (int)BOARD_CAN_PIN_TX, (int)BOARD_CAN_PIN_RX,
             (unsigned long)effective.bitrate,
             loopback ? "loopback" : (listen_only ? "listen_only" : "normal"));
    return ESP_OK;
#endif
}

esp_err_t board_can_deinit(twai_node_handle_t node)
{
#if !BOARD_HAS_CAN
    (void)node;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (node == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = twai_node_delete(node);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to delete TWAI node (%s)", esp_err_to_name(ret));
        return ret;
    }

#if BOARD_HAS_EXIO && defined(BOARD_EXIO_USB_SEL)
    /* 释放 CAN 后把 GPIO19/20 切回默认 USB 模式 */
    esp_err_t exio_ret = board_exio_set(BOARD_EXIO_USB_SEL, false);
    if (exio_ret != ESP_OK) {
        ESP_LOGW(TAG, "failed to restore EXIO%d to USB mode (%s)",
                 BOARD_EXIO_USB_SEL, esp_err_to_name(exio_ret));
    }
#endif

    return ESP_OK;
#endif
}
