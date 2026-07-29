#pragma once
#include <pebble.h>

enum { CAD_HOURLY, CAD_HALF };                          // bell cadence
enum { CUT_FULL, CUT_SILENT };                          // hop cutscene
enum { DATE_DAYNUM, DATE_MONTHDAY, DATE_OFF };          // "FRI 24" / "JUL 24"

// Persisted whole. APPEND-ONLY: new fields go at the end; older saves stop
// short and keep defaults. Bump SETTINGS_VERSION only on reorder.
#define SETTINGS_VERSION 1
typedef struct {
  uint8_t version;
  uint8_t show_health;           // health corner + sparkline; off = pure chart
  uint8_t cadence;
  uint8_t start_hour;            // the wander day begins at Sol at this hour
  uint8_t cutscene;
  uint8_t date_format;
  bool leading_zero, show_battery, show_bt;
  bool hop_vibe, bt_vibe, tap_info;
  bool weather_on;
  // ActiveHour's rule: last night's sleep holds the step slot until you've
  // actually got up and moved past wake_threshold steps, then steps take it
  // back. Appended — older saves stop short and keep these defaults.
  bool show_sleep;
  uint16_t wake_threshold;
} Settings;

extern Settings g_cfg;

// cb fires after new settings land; resync = cadence or start hour moved
void settings_init(void (*cb)(bool resync));
