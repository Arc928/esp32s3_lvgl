/*
 * The farm machines shown on the home page, in display order.
 */

#ifndef MACHINE_LIST_H
#define MACHINE_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

#define MACHINE_COUNT 9U

typedef struct {
    const lv_image_dsc_t *image;        /* gallery size: fits 248 x 106 */
    const lv_image_dsc_t *detail_image; /* detail page size: fits 340 x 260 */
    const char *name;
} machine_info_t;

/* index: 0 .. MACHINE_COUNT - 1. Returns NULL when out of range. */
const machine_info_t *machine_get(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* MACHINE_LIST_H */
