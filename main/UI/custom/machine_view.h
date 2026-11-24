/*
 * Machine detail page (800 x 480 base, scaled):
 * machine photo on the left half, 8 MQTT control commands on the right half.
 */

#ifndef MACHINE_VIEW_H
#define MACHINE_VIEW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "dashboard_data.h"

/* Called when the user taps "返回". Swipes are handled in custom.c. */
typedef void (*machine_view_back_cb_t)(void);

/* Called when the Wi-Fi icon in the status bar is tapped. */
typedef void (*machine_view_wifi_cb_t)(void);

/* Called when the user taps the status-bar MQTT indicator pill (may be NULL). */
typedef void (*machine_view_mqtt_cb_t)(void);

/* Builds the detail page on a new screen and returns that screen (not loaded). */
lv_obj_t *machine_view_create(machine_view_back_cb_t on_back, machine_view_wifi_cb_t on_wifi,
                              machine_view_mqtt_cb_t on_mqtt);

/* Shows the photo and name of a machine (index from machine_list.h). */
void machine_view_set_machine(uint32_t machine_index);

/*
 * Displays the active MQTT command on the right-half 8-command grid.
 * Pass NULL to clear any active highlight back to the idle state.
 * Set animate to true for an elastic pop-out magnification when a new command arrives.
 */
void machine_view_set_cmd(const mqtt_cmd_data_t *cmd, bool animate);

/* Refreshes the clock, MQTT pill and relative timestamp of the active command. */
void machine_view_update(const bms_data_t *bms, const imu_data_t *imu, bool mqtt_connected);

#ifdef __cplusplus
}
#endif

#endif /* MACHINE_VIEW_H */
