/*
 * Top-layer modal dialog for downlink MQTT device control commands.
 *
 * Lives on lv_layer_top() so incoming commands appear over any active screen
 * (gallery, machine detail, or Wi-Fi settings) without allocating per popup.
 */

#ifndef MQTT_CMD_DIALOG_H
#define MQTT_CMD_DIALOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "dashboard_data.h"

/* Called with 0-based machine index when the user taps "查看设备". */
typedef void (*mqtt_cmd_open_machine_cb_t)(uint32_t machine_index);

/* Builds the hidden dialog once on lv_layer_top(). */
void mqtt_cmd_dialog_init(mqtt_cmd_open_machine_cb_t on_open_machine);

/* Shows (or updates) the dialog for an incoming/latest command.
 * Set on_same_machine to true when that machine's detail page is already active. */
void mqtt_cmd_dialog_show(const mqtt_cmd_data_t *cmd, bool on_same_machine);

/* Shows an empty-state dialog when no command has arrived yet. */
void mqtt_cmd_dialog_show_empty(bool mqtt_connected);

/* Hides the dialog. */
void mqtt_cmd_dialog_hide(void);

bool mqtt_cmd_dialog_is_open(void);

/* Sequence number of the last command shown in the dialog (0 if none yet). */
uint32_t mqtt_cmd_dialog_last_seq(void);

/* Refreshes relative reception age ("刚刚" / "5 秒前") and connection state. */
void mqtt_cmd_dialog_refresh(bool mqtt_connected);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_CMD_DIALOG_H */
