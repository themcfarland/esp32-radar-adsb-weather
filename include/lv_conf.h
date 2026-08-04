#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>
#include <stddef.h>
#include <esp_heap_caps.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/*
 * Keep the LVGL heap out of the ESP32-S3 internal DRAM. A static 256 kB
 * LVGL pool overflows dram0 together with the RGB display driver. The board
 * has 8 MB OPI PSRAM, so LVGL allocations are routed there with an internal
 * RAM fallback for small allocations if PSRAM is temporarily fragmented.
 */
static inline void *lvgl_psram_malloc(size_t size) {
  void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!ptr) ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return ptr;
}

static inline void *lvgl_psram_realloc(void *ptr, size_t size) {
  void *next = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!next) next = heap_caps_realloc(ptr, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return next;
}

#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <esp_heap_caps.h>
#define LV_MEM_CUSTOM_ALLOC lvgl_psram_malloc
#define LV_MEM_CUSTOM_FREE heap_caps_free
#define LV_MEM_CUSTOM_REALLOC lvgl_psram_realloc
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#define LV_DPI_DEF 130

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_LABEL 1
#define LV_USE_IMG 1
#define LV_USE_CANVAS 1
#define LV_USE_BTN 1
#define LV_USE_BAR 1
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

#endif
