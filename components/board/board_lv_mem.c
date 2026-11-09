/*
 * LVGL 的堆放在 PSRAM（LV_STDLIB_CUSTOM）
 *
 * 默认的 LV_STDLIB_CLIB 用 malloc()，而 IDF 对 16KB 以内的分配优先给内部 RAM，LVGL 的控件、
 * 样式都是小块，会把内部 RAM 吃掉几十 KB，和 Wi-Fi、MQTT 抢同一块很小的内部 RAM，
 * 结果 MQTT 任务创建失败、Wi-Fi 扫描时 esp_phy_enable 分配失败重启。
 * 这里让 LVGL 的所有分配都走 PSRAM；绘制缓冲、RGB 屏的 bounce buffer 由 esp_lvgl_adapter
 * 用 heap_caps_* 单独放在内部 RAM，不受影响。PSRAM 用完时退回内部 RAM，不会直接分配失败。
 */

#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include "esp_heap_caps.h"

#define LV_HEAP_CAPS        (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_HEAP_CAPS_FALLBACK  (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

void lv_mem_init(void)
{
    /* 用 IDF 的堆，没有要初始化的 */
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;    /* 不支持 */
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void *lv_malloc_core(size_t size)
{
    void *p = heap_caps_malloc(size, LV_HEAP_CAPS);

    return (p != NULL) ? p : heap_caps_malloc(size, LV_HEAP_CAPS_FALLBACK);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    void *q = heap_caps_realloc(p, new_size, LV_HEAP_CAPS);

    return (q != NULL) ? q : heap_caps_realloc(p, new_size, LV_HEAP_CAPS_FALLBACK);
}

void lv_free_core(void *p)
{
    heap_caps_free(p);  /* 内部 RAM 和 PSRAM 的块都能释放 */
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    multi_heap_info_t info;

    heap_caps_get_info(&info, LV_HEAP_CAPS);
    mon_p->total_size = info.total_allocated_bytes + info.total_free_bytes;
    mon_p->free_size = info.total_free_bytes;
    mon_p->free_biggest_size = info.largest_free_block;
    mon_p->free_cnt = info.free_blocks;
    mon_p->used_cnt = info.allocated_blocks;
    mon_p->max_used = mon_p->total_size - info.minimum_free_bytes;
    mon_p->used_pct = (mon_p->total_size > 0) ? (uint8_t)(100U * info.total_allocated_bytes / mon_p->total_size) : 0;
    mon_p->frag_pct = (info.total_free_bytes > 0)
                      ? (uint8_t)(100U - 100U * info.largest_free_block / info.total_free_bytes) : 0;
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;    /* 和 LV_STDLIB_CLIB 一样不做完整性检查 */
}

#endif /* LV_STDLIB_CUSTOM */
