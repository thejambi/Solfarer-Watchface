#include "settings.h"
#include "ui.h"

Settings g_cfg;
static void (*s_cb)(bool resync);

#define KEY_SETTINGS 20

static void defaults(void) {
  memset(&g_cfg, 0, sizeof g_cfg);
  g_cfg.version = SETTINGS_VERSION;
  g_cfg.show_health = true;
  g_cfg.cadence = CAD_HOURLY;
  g_cfg.start_hour = 7;          // mornings begin at Sol
  g_cfg.cutscene = CUT_FULL;
  g_cfg.date_format = DATE_DAYNUM;
  g_cfg.show_battery = true;
  g_cfg.show_bt = true;
  g_cfg.tap_info = true;
  g_cfg.weather_on = true;
}

// Clay sends selects as strings ("0","1",..) and toggles as small ints —
// accept either so the config page and the C side can't drift apart.
static int tup_int(DictionaryIterator *it, uint32_t key, int fallback) {
  Tuple *t = dict_find(it, key);
  if (!t) return fallback;
  if (t->type == TUPLE_CSTRING) return atoi(t->value->cstring);
  return (int)t->value->int32;
}

static void inbox(DictionaryIterator *it, void *ctx) {
  Settings old = g_cfg;
  g_cfg.show_health  = tup_int(it, MESSAGE_KEY_ShowHealth, g_cfg.show_health);
  g_cfg.cadence      = tup_int(it, MESSAGE_KEY_Cadence, g_cfg.cadence);
  g_cfg.start_hour   = tup_int(it, MESSAGE_KEY_StartHour, g_cfg.start_hour) % 24;
  g_cfg.cutscene     = tup_int(it, MESSAGE_KEY_Cutscene, g_cfg.cutscene);
  g_cfg.date_format  = tup_int(it, MESSAGE_KEY_DateFormat, g_cfg.date_format);
  g_cfg.leading_zero = tup_int(it, MESSAGE_KEY_LeadingZero, g_cfg.leading_zero);
  g_cfg.show_battery = tup_int(it, MESSAGE_KEY_ShowBattery, g_cfg.show_battery);
  g_cfg.show_bt      = tup_int(it, MESSAGE_KEY_ShowBT, g_cfg.show_bt);
  g_cfg.hop_vibe     = tup_int(it, MESSAGE_KEY_HopVibe, g_cfg.hop_vibe);
  g_cfg.bt_vibe      = tup_int(it, MESSAGE_KEY_BTVibe, g_cfg.bt_vibe);
  g_cfg.tap_info     = tup_int(it, MESSAGE_KEY_TapInfo, g_cfg.tap_info);
  g_cfg.weather_on   = tup_int(it, MESSAGE_KEY_WeatherOn, g_cfg.weather_on);
  Tuple *wt = dict_find(it, MESSAGE_KEY_WeatherTemp);   // phone-side fetch
  if (wt) face_set_temp((int)wt->value->int32);
  g_cfg.version = SETTINGS_VERSION;
  persist_write_data(KEY_SETTINGS, &g_cfg, sizeof g_cfg);
  bool resync = old.cadence != g_cfg.cadence ||
                old.start_hour != g_cfg.start_hour;
  if (s_cb) s_cb(resync);
}

void settings_init(void (*cb)(bool resync)) {
  s_cb = cb;
  defaults();
  // Older (shorter) blobs read over the defaults and stop where they end.
  int n = persist_exists(KEY_SETTINGS) ? persist_get_size(KEY_SETTINGS) : 0;
  if (n > 0 && n <= (int)sizeof g_cfg) {
    Settings tmp = g_cfg;
    persist_read_data(KEY_SETTINGS, &tmp, n);
    if (tmp.version == SETTINGS_VERSION) g_cfg = tmp;
  }
  app_message_register_inbox_received(inbox);
  app_message_open(512, 64);
}
