/**
 * lv_conf.h for AgriGuard Edge — LVGL v9 + ESP32-S3 + ST7735 128x160 (RGB565)
 * Enabled via -DLV_CONF_INCLUDE_SIMPLE=1
 *
 * Derived from sketchbook/DeskBuddy/include/lv_conf.h with two power changes:
 *   LV_DEF_REFR_PERIOD 30 -> 200   (we need ~5 Hz, not 33 FPS)
 *   LV_USE_ANIMATION    1 -> 0     (values update once a minute; animating is waste)
 *
 * Re-enable LV_USE_ANIMATION 1 if you want a smooth sweep on the probe arc.
 */
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: RGB565 matches ST7735 + TFT_eSPI */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* Memory: 64KB LVGL heap — plenty for 4 screens, keep it in internal RAM.
 * Do NOT move LVGL draw buffers to PSRAM: SPI DMA out of octal PSRAM is slow
 * and fussy on the S3. Buffers are 160*32*2 = 10 KB each, so internal is free. */
#define LV_MEM_SIZE (64U * 1024U)

/* Tick: fed from Arduino millis() via lv_tick_set_cb() in main.cpp
 * (LVGL v9 removed LV_TICK_CUSTOM) */

/* Default refresh period: 200 ms. A sensor node sampling every 60 s has no
 * business rendering at 33 FPS, and LVGL only redraws dirty regions anyway. */
#define LV_DEF_REFR_PERIOD 200
#define LV_DPI_DEF 130

/* Logging over Serial */
#define LV_USE_LOG 1
#if LV_USE_LOG
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 0
#endif

/* OS / HAL */
#define LV_USE_OS LV_OS_NONE

/* Draw */
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_COMPLEX 1

/* Widgets we use (leave everything else at LVGL defaults — do NOT disable).
 * lv_bar is default-enabled and is what draws the conservation stack. */
#define LV_USE_LABEL 1
#define LV_USE_ARC 1

/* Fonts: LIVE needs a 28pt moisture readout; CONSERVATION needs 28 + 12 */
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_12

/* Animations OFF (power). Set to 1 to re-enable smooth widget transitions. */
#define LV_USE_ANIMATION 0
#define LV_USE_SHADOW 0

/* Float support ON — required for %f in lv_label_set_text_fmt(). LVGL 9
 * defaults it OFF, and its builtin printf then compiles out case 'f':
 * a "%f ... %s" format desyncs varargs, the %s reads the double bits
 * (0.0f) as a pointer, and the board Guru-panics in lv_strnlen(NULL).
 * Diagnosed on hardware via addr2line. S3 has no FPU; cost is irrelevant
 * at one label update per evaluation. */
#define LV_USE_FLOAT 1

/* Decoders we don't need */
#define LV_USE_BMP 0
#define LV_USE_PNG 0
#define LV_USE_SJPG 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 0
#define LV_USE_FFMPEG 0

#define LV_USE_DEMO_WIDGETS 0
#define LV_BUILD_EXAMPLES 0

#endif /* LV_CONF_H */
#endif