/*
 * Wi-Fi page: scans nearby networks and connects to the one the user picks,
 * asking for the password on an on-screen keyboard.
 *
 * Opened from the Wi-Fi icon in the status bar of the other pages. The data
 * comes from net_service; this file only draws and calls it.
 */

#ifndef WIFI_VIEW_H
#define WIFI_VIEW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* Called when the "返回" button is tapped. */
typedef void (*wifi_view_back_cb_t)(void);

/* Builds the page on a new screen and returns that screen (not loaded). */
lv_obj_t *wifi_view_create(wifi_view_back_cb_t on_back);

/* Call right before the page is shown: starts a scan and the refresh timer. */
void wifi_view_enter(void);

/* Call when the page is left: stops the timer and drops any typed password. */
void wifi_view_leave(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_VIEW_H */
