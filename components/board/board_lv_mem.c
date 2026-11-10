/*
 * LVGL 的堆放在 PSRAM（LV_STDLIB_CUSTOM）
 *
 * 默认的 LV_STDLIB_CLIB 用 malloc()，而 IDF 对 16KB 以内的分配优先给内部 RAM，LVGL 的控件、
 * 样式都是小块，会把内部 RAM 吃掉几十 KB，和 Wi-Fi、MQTT 抢同一块很小的内部 RAM，
 * 结果 MQTT 任务创建失败、Wi-Fi 扫描时 esp_phy_enable 分配失败重启。
 * 这里让 LVGL 的所有分配都走 PSRAM；绘制缓冲、RGB 屏的 bounce buffer 由 esp_lvgl_adapter
 * 用 heap_caps_* 单独放在内部 RAM，不受影响。PSRAM 用完时退回内部 RAM，不会直接分配失败。
 *
 * 板子可以反过来：board_config_<芯片>.h 定义 BOARD_LV_HEAP_INTERNAL_RESERVE 后，LVGL 先用内部 RAM，
 * 内部 RAM 剩余不足这个数（给 Wi-Fi、网络、任务栈和 DMA 留的）就改放 PSRAM。P4 的 Wi-Fi 跑在 C6 上，
 * 内部 RAM 比 S3 宽裕，用这个试内部 RAM 能不能让界面更流畅；S3 不定义，行为不变。
 */

#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include "board_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define LV_HEAP_CAPS        (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_HEAP_CAPS_FALLBACK  (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

#ifdef BOARD_LV_HEAP_INTERNAL_RESERVE

static const char *TAG = "lv_mem";

/* 内部 RAM 剩余（分配之后）不少于 BOARD_LV_HEAP_INTERNAL_RESERVE 才放内部 RAM。
 * 只是检查再分配，两个任务同时分配时可能多占一点，留出的余量比这个误差大得多。 */
static void *internal_first_malloc(size_t size)
{
    void *p = NULL;

    if(heap_caps_get_free_size(LV_HEAP_CAPS_FALLBACK) >= size + BOARD_LV_HEAP_INTERNAL_RESERVE) {
        p = heap_caps_malloc(size, LV_HEAP_CAPS_FALLBACK);
    }
    if(p == NULL) {
        static bool logged;

        if(!logged) {
            logged = true;
            ESP_LOGI(TAG, "internal RAM has %u bytes left (reserve %u), LVGL allocations now go to PSRAM",
                     (unsigned)heap_caps_get_free_size(LV_HEAP_CAPS_FALLBACK),
                     (unsigned)BOARD_LV_HEAP_INTERNAL_RESERVE);
        }
        p = heap_caps_malloc(size, LV_HEAP_CAPS);
    }
    return p;
}

static void *internal_first_realloc(void *p, size_t new_size)
{
    void *q = NULL;

    if(heap_caps_get_free_size(LV_HEAP_CAPS_FALLBACK) >= new_size + BOARD_LV_HEAP_INTERNAL_RESERVE) {
        q = heap_caps_realloc(p, new_size, LV_HEAP_CAPS_FALLBACK);
    }
    return (q != NULL) ? q : heap_caps_realloc(p, new_size, LV_HEAP_CAPS);
}

#endif /* BOARD_LV_HEAP_INTERNAL_RESERVE */

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
#ifdef BOARD_LV_HEAP_INTERNAL_RESERVE
    void *p = internal_first_malloc(size);

    return (p != NULL) ? p : heap_caps_malloc(size, LV_HEAP_CAPS_FALLBACK);
#else
    void *p = heap_caps_malloc(size, LV_HEAP_CAPS);

    return (p != NULL) ? p : heap_caps_malloc(size, LV_HEAP_CAPS_FALLBACK);
#endif
}

void *lv_realloc_core(void *p, size_t new_size)
{
#ifdef BOARD_LV_HEAP_INTERNAL_RESERVE
    void *q = internal_first_realloc(p, new_size);
#else
    void *q = heap_caps_realloc(p, new_size, LV_HEAP_CAPS);
#endif

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
