#pragma once
#include <pebble.h>
#include "game.h"

// Platform-adaptive palette: black & white watches render everything white on
// black — state is always carried by text (OK/!!, LATE!) and shape, never by
// color alone.
#define COL_GOLD  PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite)
#define COL_CYAN  PBL_IF_COLOR_ELSE(GColorCyan, GColorWhite)
#define COL_BLUE  PBL_IF_COLOR_ELSE(GColorPictonBlue, GColorWhite)
#define COL_DIM   PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
#define COL_FAINT PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite)
#define COL_GOOD  PBL_IF_COLOR_ELSE(GColorGreen, GColorWhite)
#define COL_BAD   PBL_IF_COLOR_ELSE(GColorRed, GColorWhite)

// Layout probes: `compact` = 144x168 and 180x180 screens; round = chalk/gabbro.
#define IS_ROUND PBL_IF_ROUND_ELSE(true, false)
#define IS_COMPACT(b) ((b).size.h < 200)

// Pebble's snprintf has no %f — small fixed-point formatters instead.
void fmt1(char *buf, size_t cap, double v);            // "12.3"
void fmt_years(char *buf, size_t cap, double y);       // "88.3yr" / "1.23kyr"
void fmt_beta(char *buf, size_t cap, double beta);     // "0.9973c"
void fmt_gamma(char *buf, size_t cap, double gamma);   // "13.6" / "25k"

// The one window. The face tells it what just happened; it picks the scene.
void face_init(void);
void face_deinit(void);
void face_poke(void);            // redraw (catch-up progress, settings change)
void face_show_run(void);        // a live bell just resolved: cutscene per settings
void face_show_summary(void);    // day rolled over: summary card, then the new map
void face_set_temp(int temp);    // phone delivered a fresh Open-Meteo reading
