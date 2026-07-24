#pragma once
#include <pebble.h>

// All watchface behavior dials, configured through Clay on the phone and
// persisted on the watch. Defaults live in settings_init().

enum { FMODE_GAME, FMODE_CHART };                       // full game / chart skin
enum { CAD_HOURLY, CAD_HALF };                          // bell cadence
enum { DSP_SELECTED, DSP_FALLBACK, DSP_BESTPAY, DSP_SAFEST };
enum { CUT_FULL, CUT_RESULTS, CUT_SILENT };             // what a live bell shows
enum { UPG_PRIORITY, UPG_CHEAPEST, UPG_RANDOM, UPG_OFF };
enum { SEED_DAILY, SEED_FIXED };
enum { DATE_DAYNUM, DATE_MONTHDAY, DATE_OFF };          // "WED 22" / "JUL 22"
enum { VIBE_OFF, VIBE_DELIVERY, VIBE_RECORDS };

// Persisted whole. APPEND-ONLY: new fields go at the end and must treat 0 as
// a sane value or be defaulted before the partial read (see settings_init) —
// older saves simply stop short and leave them at their defaults. Bump
// SETTINGS_VERSION only if existing fields ever move or change meaning.
#define SETTINGS_VERSION 1
typedef struct {
  uint8_t version;
  uint8_t face_mode;             // FMODE_CHART: sim runs underneath, unshown
  uint8_t cadence, dispatch, cutscene, upgrades;
  uint8_t seed_mode;
  char fixed_seed[10];
  uint8_t date_format, vibe_mode;
  bool show_signposts, leading_zero, show_battery, show_bt, bt_vibe;
  bool tap_info;                 // tap/shake opens the map info overlay
  bool weather_on;               // corner temp; the location prompt gate
} Settings;

extern Settings g_cfg;

// cb fires after new settings land from the phone; seed_changed means the
// map source itself moved (seed mode or fixed seed text)
void settings_init(void (*cb)(bool seed_changed));
