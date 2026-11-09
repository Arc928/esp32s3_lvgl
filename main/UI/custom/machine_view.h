/*
 * Machine detail page (800 x 480): BMS on the left half, other machine
 * information on the right half.
 */

#ifndef MACHINE_VIEW_H
#define MACHINE_VIEW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gui_guider.h"
#include "dashboard_data.h"

/* Called when the user taps "返回". Swipes are handled in custom.c. */
typedef void (*machine_view_back_cb_t)(void);

/* Called when the Wi-Fi icon in the status bar is tapped. */
typedef void (*machine_view_wifi_cb_t)(void);

/*
 * Builds the page on ui->screen. Keeps the GUI Guider status bar, clock,
 * Wi-Fi icon and LED, and replaces the GUI Guider IMU panel.
 */
void machine_view_create(lv_ui *ui, machine_view_back_cb_t on_back, machine_view_wifi_cb_t on_wifi);

/* Shows the photo and name of a machine (index from machine_list.h). */
void machine_view_set_machine(uint32_t machine_index);

/* Pass NULL for data that has not been received yet. */
void machine_view_update(const bms_data_t *bms, const imu_data_t *imu);

#ifdef __cplusplus
}
#endif

#endif /* MACHINE_VIEW_H */
