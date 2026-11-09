/*
 * Home page: photos of the farm machines in a 3 x 3 grid.
 * Tapping a photo selects that machine.
 */

#ifndef GALLERY_VIEW_H
#define GALLERY_VIEW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* Called with the machine index (see machine_list.h) when its photo is tapped. */
typedef void (*gallery_select_cb_t)(uint32_t machine_index);

/* Called when the Wi-Fi icon in the status bar is tapped. */
typedef void (*gallery_wifi_cb_t)(void);

/* Builds the home page on a new screen and returns that screen (not loaded). */
lv_obj_t *gallery_view_create(gallery_select_cb_t on_select, gallery_wifi_cb_t on_wifi);

/* Refreshes the clock. */
void gallery_view_update(void);

/* Marks the card of the machine the detail page shows (the right page of the pager). */
void gallery_view_set_selected(uint32_t machine_index);

#ifdef __cplusplus
}
#endif

#endif /* GALLERY_VIEW_H */
