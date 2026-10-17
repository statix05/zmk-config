#pragma once
#define LV_CONF_H
/* Match the monochrome Zephyr display path, using LVGL 9 configuration names. */
#define LV_COLOR_DEPTH 1
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (32 * 1024)
#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 0
#define LV_USE_IMAGE 1
#define LV_USE_LABEL 1
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_MONO 1
#define LV_USE_THEME_BASIC 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_SUPPORT_I1 1
#define LV_DRAW_BUF_STRIDE_ALIGN 1
/* Exercise decoded-image cache invalidation on mutable frame data. */
#define LV_CACHE_DEF_SIZE 4096
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 8
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0
