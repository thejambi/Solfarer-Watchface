#include "ui.h"
#include "settings.h"
#include "scheduler.h"
#include "stars.h"
#include "walk.h"

// Solfarer's face: the Local Bubble as a clock. A traveling 40-ly viewport
// rides the daily wander; the gold route sweeps to a different neighbor
// every minute; on the bell the ship flies it — twin paradox and all. One
// window, five draw-states; scenes time themselves out.

typedef enum { MODE_MAP, MODE_FLIGHT, MODE_ARRIVE, MODE_SUMMARY } Mode;

#define FLIGHT_MS 4200
#define FLIGHT_TICK_MS 50
#define ARRIVE_MS 7000
#define SUMMARY_MS 10000
#define N_STARS_BG 26
#define VIEW_01 4000                   // viewport width: 40 ly
#define SLAB_01 1200                   // draw stars within ±12 ly of our depth

static Window *s_win;
static Layer *s_layer;
static Mode s_mode = MODE_MAP;
static AppTimer *s_timer;
static int s_elapsed;
static GPoint s_bg[N_STARS_BG];
static int s_hour, s_min, s_mday, s_mon, s_wday;
static bool s_bt_ok = true;
static uint8_t s_batt = 100;

static const char *WD[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
static const char *MO[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                              "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

static void fmt_time(char *buf, size_t cap) {
  int h = s_hour;
  if (!clock_is_24h_style()) { h %= 12; if (h == 0) h = 12; }
  if (clock_is_24h_style() || g_cfg.leading_zero)
    snprintf(buf, cap, "%02d:%02d", h, s_min);
  else
    snprintf(buf, cap, "%d:%02d", h, s_min);
}

static double ease(double t) { return t * t * (3 - 2 * t); }

static bool health_mode(void) { return g_cfg.face_mode == FMODE_HEALTH; }

static int cur_window(void) {
  int h = (s_hour - g_cfg.start_hour + 24) % 24;
  return g_cfg.cadence == CAD_HALF ? h * 2 + (s_min >= 30 ? 1 : 0) : h;
}

// ---------------------------------------------------------------------------
// Shake-to-browse: a manual walk around the neighbor board. Display only —
// the bell computes its own pick from its own wall time, so browsing can
// never change where the ship actually flies.
// ---------------------------------------------------------------------------
#define BROWSE_HOLD_S 30
static int s_browse = -1;            // manual board index, -1 = follow minutes
static time_t s_browse_at;

static bool browsing(void) {
  if (s_browse < 0) return false;
  if (time(NULL) - s_browse_at > BROWSE_HOLD_S) {
    s_browse = -1;
    return false;
  }
  return true;
}

static int active_sel(int window) {
  if (browsing()) {
    int n = board_count(window);
    return n > 0 ? s_browse % n : 0;
  }
  return board_sel(window, s_hour, s_min);
}

static void browse_reset(void) { s_browse = -1; }

// Health mode's shake: no card — the stats line's kcal swaps to hours slept
// for a few seconds (sleep is the only figure the face doesn't already show).
#define SLEEP_PEEK_MS 6000
static time_t s_sleep_peek_at;
static AppTimer *s_peek_timer;

static bool sleep_peek(void) {
  return s_sleep_peek_at != 0 &&
         time(NULL) - s_sleep_peek_at < SLEEP_PEEK_MS / 1000;
}

static void peek_expire(void *ctx) {
  s_peek_timer = NULL;
  s_sleep_peek_at = 0;
  face_poke();
}

// ---------------------------------------------------------------------------
// Health — the health-stats mode trades star chrome for the body's own.
// Same machinery as the Lighthaul watchface (see its README for the three
// ActiveHour lessons baked into the minute-history handling).
// ---------------------------------------------------------------------------
static void fmt_thousands(char *buf, size_t cap, int v) {
  if (v >= 1000) snprintf(buf, cap, "%d,%03d", v / 1000, v % 1000);
  else snprintf(buf, cap, "%d", v);
}

//#define DEV_FAKE_HEALTH

#if defined(PBL_HEALTH)
static int steps_today(void) {
#ifdef DEV_FAKE_HEALTH
  return 8842;
#endif
  return (int)health_service_sum_today(HealthMetricStepCount);
}
static int walked_m_today(void) {
#ifdef DEV_FAKE_HEALTH
  return 6300;
#endif
  return (int)health_service_sum_today(HealthMetricWalkedDistanceMeters);
}
static int kcal_today(void) {
#ifdef DEV_FAKE_HEALTH
  return 312;
#endif
  return (int)health_service_sum_today(HealthMetricActiveKCalories);
}
static int sleep_secs(void) {
#ifdef DEV_FAKE_HEALTH
  return 7 * 3600 + 42 * 60;
#endif
  time_t start = time_start_of_today(), end = time(NULL);
  HealthServiceAccessibilityMask m =
      health_service_metric_accessible(HealthMetricSleepSeconds, start, end);
  if (!(m & HealthServiceAccessibilityMaskAvailable)) return 0;
  return (int)health_service_sum_today(HealthMetricSleepSeconds);
}
static int hr_bpm(void) {
#ifdef DEV_FAKE_HEALTH
  return 68;
#endif
#if PBL_API_EXISTS(health_service_peek_current_value)
  return (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
#else
  return 0;
#endif
}

static uint8_t s_minsteps[60];
static int s_step_snap = -1;
static bool s_refetch_pending;

static void fetch_minute_history(void) {
#ifdef DEV_FAKE_HEALTH
  for (int i = 0; i < 60; i++) {
    int ago = 59 - i, st = 0;
    if (ago >= 40 && ago < 46) st = 70 + (i * 7) % 40;
    else if (ago >= 12 && ago < 22) st = 15 + (i * 5) % 30;
    else if (ago < 4) st = 55 + (i * 11) % 45;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    s_minsteps[((t->tm_min - ago) % 60 + 60) % 60] = st;
  }
  return;
#endif
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int cur = t->tm_min;
  HealthMinuteData *md = malloc(60 * sizeof *md);
  if (!md) return;
  time_t end = now, start = now - SECONDS_PER_HOUR;
  uint32_t nrec = health_service_get_minute_history(md, 60, &start, &end);
  for (uint32_t i = 0; i < nrec; i++) {
    int idx = ((int)i + 1 + cur) % 60;
    if (!md[i].is_invalid) s_minsteps[idx] = md[i].steps;
  }
  free(md);
}

static void minute_track(struct tm *t) {
  s_minsteps[t->tm_min] = 0;
  s_step_snap = steps_today();
  if (s_refetch_pending && t->tm_min % 15 == 1) {
    fetch_minute_history();
    s_refetch_pending = false;
  }
}

static int minute_steps_live(void) {
  if (s_step_snap < 0) return 0;
  int d = steps_today() - s_step_snap;
  return d > 0 ? d : 0;
}

static void health_evt(HealthEventType e, void *ctx) {
  if (e == HealthEventMovementUpdate) face_poke();
}

static void health_init(void) {
  fetch_minute_history();
  s_refetch_pending = true;
  s_step_snap = steps_today();
  health_service_events_subscribe(health_evt, NULL);
}
static void health_deinit(void) { health_service_events_unsubscribe(); }
#else
static int steps_today(void)    { return 0; }
static int walked_m_today(void) { return 0; }
static int kcal_today(void)     { return 0; }
static int sleep_secs(void)     { return 0; }
static int hr_bpm(void)         { return 0; }
static uint8_t s_minsteps[60];
static void minute_track(struct tm *t) { (void)t; }
static int minute_steps_live(void) { return 0; }
static void health_init(void) {}
static void health_deinit(void) {}
#endif

// ---------------------------------------------------------------------------
// Weather — phone fetches (Open-Meteo, pkjs), watch shows the latest number.
// ---------------------------------------------------------------------------
#define KEY_WEATHER 30
typedef struct { int32_t at; int16_t temp; } WeatherSave;
static int16_t s_temp;
static int32_t s_temp_at;

void face_set_temp(int temp) {
  s_temp = temp;
  s_temp_at = (int32_t)time(NULL);
  WeatherSave w = { s_temp_at, s_temp };
  persist_write_data(KEY_WEATHER, &w, sizeof w);
  face_poke();
}

static bool temp_fresh(void) {
  return g_cfg.weather_on && s_temp_at != 0 &&
         time(NULL) - s_temp_at < 3 * SECONDS_PER_HOUR;
}

static void weather_load(void) {
  if (persist_exists(KEY_WEATHER) &&
      persist_get_size(KEY_WEATHER) == (int)sizeof(WeatherSave)) {
    WeatherSave w;
    persist_read_data(KEY_WEATHER, &w, sizeof w);
    s_temp_at = w.at;
    s_temp = w.temp;
  }
}

// ---------------------------------------------------------------------------
// Scene switching
// ---------------------------------------------------------------------------
static void cancel_timer(void) {
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
}

static void back_to_map(void *ctx) {
  s_timer = NULL;
  s_mode = MODE_MAP;
  if (s_layer) layer_mark_dirty(s_layer);
}

static void enter_card(Mode m, uint32_t ms) {
  cancel_timer();
  s_mode = m;
  s_timer = app_timer_register(ms, back_to_map, NULL);
  if (s_layer) layer_mark_dirty(s_layer);
}

static void flight_tick(void *ctx) {
  s_elapsed += FLIGHT_TICK_MS;
  if (s_elapsed >= FLIGHT_MS + 400) {
    s_timer = NULL;
    enter_card(MODE_ARRIVE, ARRIVE_MS);
    return;
  }
  if (s_layer) layer_mark_dirty(s_layer);
  s_timer = app_timer_register(FLIGHT_TICK_MS, flight_tick, NULL);
}

static void enter_flight(void) {
  cancel_timer();
  s_mode = MODE_FLIGHT;
  s_elapsed = 0;
  GRect b = layer_get_bounds(s_layer);
  for (int i = 0; i < N_STARS_BG; i++)
    s_bg[i] = GPoint(rand() % b.size.w, rand() % b.size.h);
  s_timer = app_timer_register(FLIGHT_TICK_MS, flight_tick, NULL);
  layer_mark_dirty(s_layer);
}

void face_show_hop(void) {
  browse_reset();                  // new port, new board — follow the minutes
  if (g_cfg.hop_vibe && !quiet_time_is_active()) vibes_short_pulse();
  if (!s_layer) return;
  if (g_cfg.cutscene == CUT_FULL) enter_flight();
  else layer_mark_dirty(s_layer);
}

void face_show_summary(void) {
  browse_reset();
  if (!s_layer) return;
  enter_card(MODE_SUMMARY, SUMMARY_MS);
}

void face_poke(void) {
  if (s_layer) layer_mark_dirty(s_layer);
}

// ---------------------------------------------------------------------------
// Star colors & sizes — spectral class, honestly
// ---------------------------------------------------------------------------
static GColor class_color(char c) {
#ifdef PBL_COLOR
  switch (c) {
    case 'O': case 'B': return GColorPictonBlue;
    case 'A': return GColorWhite;
    case 'F': return GColorPastelYellow;
    case 'G': return GColorYellow;
    case 'K': return GColorOrange;
    case 'M': return GColorRed;
    case 'D': return GColorCeleste;
    default:  return GColorLightGray;
  }
#else
  (void)c;
  return GColorWhite;
#endif
}

static int class_radius(int idx) {
  int am4 = idx < 0 ? 19 : star_rec(idx)->absmag4;
  if (am4 <= 8) return 3;              // absmag ≤ 2: the beacons
  if (am4 <= 22) return 2;             // ≤ 5.5: suns
  return 1;                            // the dwarf multitude
}

// ---------------------------------------------------------------------------
// MAP — the traveling viewport
// ---------------------------------------------------------------------------
static void chrome_metrics(GRect b, int *top_h, int *bot_h, int *inset) {
  bool round = IS_ROUND, compact = IS_COMPACT(b);
  *top_h = round ? 48 : (compact ? 50 : 60);
  *bot_h = round ? 62 : (compact ? 36 : 50);
  *inset = round ? 24 : 0;
  if (health_mode()) *top_h += (round || compact) ? 8 : 4;
}

// Seeded per-(star, day) framing jitter: you're never dead-center, and a
// revisit frames differently — motion must be felt.
static void view_center(int *cx, int *cy) {
  int x, y, z;
  star_pos01(g_walk.cur, &x, &y, &z);
  uint32_t h = 2166136261u;
  h = (h ^ (uint32_t)(g_walk.cur + 2)) * 16777619u;
  h = (h ^ g_walk.day_key) * 16777619u;
  int jx = (int)(h % 1301) - 650;      // ±6.5 ly
  h = h * 1664525u + 1013904223u;
  int jy = (int)(h % 1301) - 650;
  *cx = x + jx;
  *cy = y + jy;
}

static void draw_map(GContext *ctx, GRect b) {
  bool round = IS_ROUND, compact = IS_COMPACT(b);
  int top_h, bot_h, inset;
  chrome_metrics(b, &top_h, &bot_h, &inset);
  char buf[96], t1[20], t2[20];

  // --- clock + date (identical chrome to the sibling face)
  fmt_time(t1, sizeof t1);
  graphics_context_set_text_color(ctx, GColorWhite);
  if (round) {
    graphics_draw_text(ctx, t1,
                       fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM),
                       GRect(0, 2, b.size.w, 28), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  } else {
    graphics_draw_text(ctx, t1,
                       fonts_get_system_font(compact ? FONT_KEY_LECO_32_BOLD_NUMBERS
                                                     : FONT_KEY_LECO_36_BOLD_NUMBERS),
                       GRect(2, -2, b.size.w - 46, compact ? 34 : 38),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    if (g_cfg.date_format != DATE_OFF) {
      const char *l1 = g_cfg.date_format == DATE_DAYNUM ? WD[s_wday] : MO[s_mon];
      snprintf(buf, sizeof buf, "%d", s_mday);
      graphics_context_set_text_color(ctx, COL_DIM);
      graphics_draw_text(ctx, l1, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                         GRect(b.size.w - 44, compact ? -3 : 0, 40, 16),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
      graphics_context_set_text_color(ctx, GColorWhite);
      graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                         GRect(b.size.w - 44, compact ? 10 : 14, 40, 20),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    }
    if (g_cfg.feature_steps && !compact) {
      // the featured step count lives in the gap between the time and the
      // date — sized to whatever the actual time string leaves free
      GSize tmsz = graphics_text_layout_get_content_size(t1,
          fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS),
          GRect(0, 0, b.size.w, 40), GTextOverflowModeTrailingEllipsis,
          GTextAlignmentLeft);
      int x0 = 2 + tmsz.w + 5;
      int x1 = g_cfg.date_format != DATE_OFF ? b.size.w - 48 : b.size.w - 4;
      char st[12];
      fmt_thousands(st, sizeof st, steps_today());
      GFont fs = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      GSize ssz = graphics_text_layout_get_content_size(st, fs,
          GRect(0, 0, b.size.w, 22), GTextOverflowModeTrailingEllipsis,
          GTextAlignmentLeft);
      if (ssz.w > x1 - x0) {           // tight gap (leading-zero times): step down
        fs = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
        ssz = graphics_text_layout_get_content_size(st, fs,
            GRect(0, 0, b.size.w, 22), GTextOverflowModeTrailingEllipsis,
            GTextAlignmentLeft);
      }
      if (ssz.w > x1 - x0)             // still tight: lose the comma
        snprintf(st, sizeof st, "%d", steps_today());
      graphics_context_set_text_color(ctx, COL_GOOD);
      graphics_draw_text(ctx, st, fs, GRect(x0, 8, x1 - x0, 22),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    }
  }

  // --- above the gauge: the body's line (health mode) or your position.
  // Health mode: personal values left in green; the temperature far right in
  // gray, stacked under the date — day values together. Sleep needs no slot
  // of its own: the shake peek swaps it in where kcal sits.
  int gh = health_mode() ? 10 : 2;
  int vy = top_h - 16 - gh;
  if (health_mode()) {
    char st[16], km[16], kc[16];
    fmt_thousands(st, sizeof st, steps_today());
    fmt1(km, sizeof km, walked_m_today() / 1000.0);
    int ss = sleep_peek() ? sleep_secs() : 0;
    if (ss > 0) {
      unsigned hh = ((unsigned)ss / 3600u) % 100u, mm = ((unsigned)ss / 60u) % 60u;
      snprintf(kc, sizeof kc, "%uh%02um", hh, mm);
    } else {
      snprintf(kc, sizeof kc, "%dkcal", kcal_today());
    }
    int hr = hr_bpm();
    if (hr > 0)
      snprintf(buf, sizeof buf, "%s %skm %s %dbpm", st, km, kc, hr);
    else
      snprintf(buf, sizeof buf, "%s %skm %s", st, km, kc);
    graphics_context_set_text_color(ctx, COL_GOOD);
    if (round) {
      graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                         GRect(inset, vy, b.size.w - 2 * inset, 16),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    } else {
      int tw = temp_fresh() ? 36 : 0;
      graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                         GRect(2, vy, b.size.w - 4 - tw, 16),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      if (temp_fresh()) {
        snprintf(buf, sizeof buf, "%d\xC2\xB0", (int)s_temp);
        graphics_context_set_text_color(ctx, COL_DIM);
        graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(b.size.w - 38, vy, 34, 16),
                           GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
      }
    }
  } else {
    // the bottom panel owns the station identity now — this line carries
    // the day's wander instead
    fmt1(t1, sizeof t1, g_walk.path_ly10 / 10.0);
    snprintf(buf, sizeof buf, "%d hop%s  %sly today", g_walk.hops,
             g_walk.hops == 1 ? "" : "s", t1);
    graphics_context_set_text_color(ctx, COL_DIM);
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(inset, vy, b.size.w - 2 * inset, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // --- the gold strip: the day's hops so far, or the health sparkline
  int gx0 = round ? 40 : 2, gw = b.size.w - 2 * gx0;
  int gy0 = top_h - gh;
  if (health_mode()) {
    int live = minute_steps_live();
    s_minsteps[s_min] = live > 255 ? 255 : live;
#ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, COL_FAINT);
    graphics_fill_rect(ctx, GRect(gx0, gy0, gw, 1), 0, GCornerNone);
    for (int i = 0; i < 60; i++) {
      int wall = (s_min + 1 + i) % 60;
      if (wall % 5) continue;
      graphics_fill_rect(ctx, GRect(gx0 + gw * i / 60, gy0, 1, wall == 0 ? gh : 3),
                         0, GCornerNone);
    }
#endif
    graphics_context_set_fill_color(ctx, COL_GOLD);
    const int cap = 90;
    for (int i = 0; i < 60; i++) {
      int x = gx0 + gw * i / 60, xe = gx0 + gw * (i + 1) / 60;
      int st = s_minsteps[(s_min + 1 + i) % 60];
      if (st > cap) st = cap;
      int hcol = 1 + st * (gh - 1) / cap;
      graphics_fill_rect(ctx, GRect(x, gy0 + gh - hcol, xe - x, hcol), 0, GCornerNone);
    }
  } else {
    int slots = g_cfg.cadence == CAD_HALF ? 48 : 24;
    int done = g_walk.last_slot + 1;
    if (done < 0) done = 0;
    if (done > slots) done = slots;
    graphics_context_set_fill_color(ctx, COL_FAINT);
    graphics_fill_rect(ctx, GRect(gx0, gy0, gw, gh), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, COL_GOLD);
    graphics_fill_rect(ctx, GRect(gx0, gy0, gw * done / slots, gh), 0, GCornerNone);
  }

  // --- the viewport
  GRect area = GRect(inset, top_h + 2, b.size.w - 2 * inset,
                     b.size.h - top_h - bot_h - 2);
  int vcx, vcy;
  view_center(&vcx, &vcy);
  int curx, cury, curz;
  star_pos01(g_walk.cur, &curx, &cury, &curz);
  int mx = area.origin.x + area.size.w / 2;
  int my = area.origin.y + area.size.h / 2;
  int span = area.size.w - 8;
  #define SX(px01) (mx + (int)((int32_t)((px01) - vcx) * span / VIEW_01))
  #define SY(py01) (my - (int)((int32_t)((py01) - vcy) * span / VIEW_01))

  int window = cur_window();
  int sel = active_sel(window);
  int tgt = board_count(window) > 0 ? board_star(window, sel) : g_walk.cur;

  // Target screen position, clamped to the frame edge along the route
  // bearing when the hop reaches past the viewport — computed up front so
  // the route line stops at the marker instead of sailing into the chrome.
  int tx, ty, tz;
  star_pos01(tgt, &tx, &ty, &tz);
  int tpx = SX(tx), tpy = SY(ty);
  int fx = SX(curx), fy = SY(cury);
  bool tclamped = false;
  {
    int xmin = area.origin.x + 5, xmax = area.origin.x + area.size.w - 6;
    int ymin = area.origin.y + 5, ymax = area.origin.y + area.size.h - 6;
    int dx = tpx - fx, dy = tpy - fy;
    float t = 1.0f;
    if (dx > 0 && tpx > xmax) { float u = (xmax - fx) / (float)dx; if (u < t) t = u; }
    if (dx < 0 && tpx < xmin) { float u = (xmin - fx) / (float)dx; if (u < t) t = u; }
    if (dy > 0 && tpy > ymax) { float u = (ymax - fy) / (float)dy; if (u < t) t = u; }
    if (dy < 0 && tpy < ymin) { float u = (ymin - fy) / (float)dy; if (u < t) t = u; }
    if (t < 1.0f) {
      tclamped = true;
      tpx = fx + (int)(dx * t);
      tpy = fy + (int)(dy * t);
    }
  }
  // The target label picks its spot around the marker — preferring the side
  // opposite the current star so the name never sits on the route, falling
  // back around the compass. Decided up front so SOL's label can dodge it.
  GRect tl_rect;
  GTextAlignment tl_align;
  {
    GRect cand[4];
    GTextAlignment al[4];
    bool right_first = fx <= tpx;          // route comes from the left → go right
    cand[0] = right_first ? GRect(tpx + 6, tpy - 8, 62, 16)
                          : GRect(tpx - 68, tpy - 8, 62, 16);
    al[0] = right_first ? GTextAlignmentLeft : GTextAlignmentRight;
    cand[1] = right_first ? GRect(tpx - 68, tpy - 8, 62, 16)
                          : GRect(tpx + 6, tpy - 8, 62, 16);
    al[1] = right_first ? GTextAlignmentRight : GTextAlignmentLeft;
    cand[2] = GRect(tpx - 31, tpy - 20, 62, 16);
    al[2] = GTextAlignmentCenter;
    cand[3] = GRect(tpx - 31, tpy + 6, 62, 16);
    al[3] = GTextAlignmentCenter;
    int best = 0, best_score = 1 << 20;
    for (int c = 0; c < 4; c++) {
      GRect r = cand[c];
      int score = c;
      if (r.origin.x < area.origin.x - 2 || r.origin.y < area.origin.y ||
          r.origin.x + r.size.w > area.origin.x + area.size.w + 2 ||
          r.origin.y + r.size.h > area.origin.y + area.size.h)
        score += 1000;
      for (int k = 0; k <= 8; k++) {       // the route, sampled
        int qx = fx + (tpx - fx) * k / 8, qy = fy + (tpy - fy) * k / 8;
        if (qx >= r.origin.x && qx < r.origin.x + r.size.w &&
            qy >= r.origin.y && qy < r.origin.y + r.size.h)
          score += 25;
      }
      if (score < best_score) { best_score = score; best = c; }
    }
    tl_rect = cand[best];
    tl_align = al[best];
  }

  // route first, under the dots — to the clamped marker, never past it
  graphics_context_set_stroke_color(ctx, COL_GOLD);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(fx, fy), GPoint(tpx, tpy));

  // the neighborhood, in its true colors
  for (int i = 0; i < CAT_COUNT; i++) {
    const StarRec *r = star_rec(i);
    if (r->z - curz > SLAB_01 || curz - r->z > SLAB_01) continue;
    int px = SX(r->x), py = SY(r->y);
    if (px < area.origin.x - 4 || px > area.origin.x + area.size.w + 4 ||
        py < area.origin.y + 2 || py > area.origin.y + area.size.h - 2)
      continue;               // tight vertically: no bleed into the chrome
    if (i == g_walk.cur || i == tgt) continue;   // drawn on top below
    int rad = class_radius(i);
    graphics_context_set_fill_color(ctx, class_color(CAT_CLASSES[r->spect >> 4]));
    if (rad == 1) graphics_fill_rect(ctx, GRect(px, py, 1, 1), 0, GCornerNone);
    else graphics_fill_circle(ctx, GPoint(px, py), rad - 1);
  }

  // Sol: ringed gold when in frame, an edge signpost pointing home when not
  if (g_walk.cur != STAR_SOL) {
    int spx = SX(0), spy = SY(0);
    bool in_frame = spx >= area.origin.x + 6 && spx <= area.origin.x + area.size.w - 6 &&
                    spy >= area.origin.y + 6 && spy <= area.origin.y + area.size.h - 6;
    if (in_frame && abs(curz) <= SLAB_01) {
      graphics_context_set_fill_color(ctx, COL_GOLD);
      graphics_fill_circle(ctx, GPoint(spx, spy), 2);
      graphics_context_set_stroke_color(ctx, COL_GOLD);
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_circle(ctx, GPoint(spx, spy), 5);
    } else {
      // ride the bearing home to the frame edge: screen-space direction to
      // Sol is (-vcx, +vcy) (y inverted), scaled to whichever edge it meets
      int hw = area.size.w / 2 - 8, hh = area.size.h / 2 - 8;
      int sdx = -vcx, sdy = vcy;
      int ax = sdx < 0 ? -sdx : sdx, ay = sdy < 0 ? -sdy : sdy;
      int ex = mx, ey = my;
      if (ax > 0 || ay > 0) {
        if ((int32_t)ax * hh >= (int32_t)ay * hw) {
          ex = mx + (sdx > 0 ? hw : -hw);
          ey = my + (ax ? (int)((int32_t)sdy * hw / ax) : 0);
        } else {
          ey = my + (sdy > 0 ? hh : -hh);
          ex = mx + (ay ? (int)((int32_t)sdx * hh / ay) : 0);
        }
      }
      graphics_context_set_fill_color(ctx, COL_GOLD);
      graphics_fill_rect(ctx, GRect(ex - 2, ey - 2, 5, 5), 0, GCornerNone);
      // The label dodges: four candidate spots around the square, scored
      // against the frame, the route line (sampled), and the target's
      // label — preferring the side that faces away from the route.
      GRect cl = GRect(ex - 37, ey - 8, 32, 16);
      GRect cr = GRect(ex + 6, ey - 8, 32, 16);
      GRect ca = GRect(ex - 16, ey - 23, 32, 16);
      GRect cb = GRect(ex - 16, ey + 7, 32, 16);
      bool left_first = (fx + tpx) / 2 > ex;
      GRect cands[4];
      cands[0] = left_first ? cl : cr;
      cands[1] = left_first ? cr : cl;
      cands[2] = ca;
      cands[3] = cb;
      int besti = 0, best_score = 1 << 20;
      for (int c = 0; c < 4; c++) {
        GRect r = cands[c];
        int score = c;                       // mild preference for early slots
        if (r.origin.x < area.origin.x || r.origin.y < area.origin.y ||
            r.origin.x + r.size.w > area.origin.x + area.size.w ||
            r.origin.y + r.size.h > area.origin.y + area.size.h)
          score += 1000;                     // off the map: last resort
        if (tgt != g_walk.cur &&
            !(r.origin.x + r.size.w < tl_rect.origin.x ||
              tl_rect.origin.x + tl_rect.size.w < r.origin.x ||
              r.origin.y + r.size.h < tl_rect.origin.y ||
              tl_rect.origin.y + tl_rect.size.h < r.origin.y))
          score += 200;                      // sits on the target's label
        for (int k = 0; k <= 8; k++) {       // crosses the route line
          int qx = fx + (tpx - fx) * k / 8, qy = fy + (tpy - fy) * k / 8;
          if (qx >= r.origin.x && qx < r.origin.x + r.size.w &&
              qy >= r.origin.y && qy < r.origin.y + r.size.h)
            score += 25;
        }
        if (score < best_score) { best_score = score; besti = c; }
      }
      graphics_context_set_text_color(ctx, COL_GOLD);
      graphics_draw_text(ctx, "SOL", fonts_get_system_font(FONT_KEY_GOTHIC_14),
                         cands[besti], GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentCenter, NULL);
    }
  }

  // the target marker at the (possibly clamped) route end — a square at the
  // edge, matching the signpost grammar: square = beyond the frame
  if (tgt != g_walk.cur) {
    graphics_context_set_fill_color(ctx, COL_CYAN);
    if (tclamped)
      graphics_fill_rect(ctx, GRect(tpx - 2, tpy - 2, 5, 5), 0, GCornerNone);
    else
      graphics_fill_circle(ctx, GPoint(tpx, tpy), 3);
    char tn[STAR_NAME_MAX];
    star_name(tgt, tn, sizeof tn);
    graphics_context_set_text_color(ctx, COL_CYAN);
    graphics_draw_text(ctx, tn, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       tl_rect, GTextOverflowModeTrailingEllipsis, tl_align, NULL);
  }

  // you are here — your star, in its true color, ringed gold
  {
    int px = SX(curx), py = SY(cury);
    graphics_context_set_fill_color(ctx, g_walk.cur == STAR_SOL
                                    ? COL_GOLD : class_color(star_class(g_walk.cur)));
    graphics_fill_circle(ctx, GPoint(px, py), 3);
    graphics_context_set_stroke_color(ctx, COL_GOLD);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, GPoint(px, py), 6);
  }

  // --- comms / power in the map corners
  if (g_cfg.show_bt && !s_bt_ok) {
    graphics_context_set_text_color(ctx, COL_BAD);
    graphics_draw_text(ctx, "BT!", fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(inset + 2, top_h + 2, 26, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
  if (g_cfg.show_battery) {
    int bx = b.size.w - inset - 19, by = top_h + 5;
    graphics_context_set_stroke_color(ctx, COL_FAINT);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_rect(ctx, GRect(bx, by, 14, 7));
    graphics_fill_rect(ctx, GRect(bx + 14, by + 2, 2, 3), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, s_batt <= 20 ? COL_BAD : COL_DIM);
    graphics_fill_rect(ctx, GRect(bx + 2, by + 2, s_batt / 10, 3), 0, GCornerNone);
  }

  // --- bottom panel: where you are, and where the minute points. Two
  // columns on rect screens — the @-station on the left, the target on the
  // right, distances beneath each (uni/you live in the cutscene now).
  int y = b.size.h - bot_h;
  int px_ = round ? 26 : 4;
  int pw = b.size.w - 2 * px_;
  graphics_context_set_stroke_color(ctx, COL_FAINT);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(inset, y), GPoint(b.size.w - inset, y));

  char cn[STAR_NAME_MAX], tn[STAR_NAME_MAX], cls[8] = "";
  star_name(g_walk.cur, cn, sizeof cn);
  star_name(tgt, tn, sizeof tn);
  if (star_class(tgt) != '?')
    snprintf(cls, sizeof cls, " %c%d", star_class(tgt), star_subclass(tgt));
  float td = star_dist_ly(g_walk.cur, tgt);

  if (round) {
    snprintf(buf, sizeof buf, "%s %s", browsing() ? ">>" : ">", tn);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(px_, y - 2, pw, 20), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
    fmt1(t1, sizeof t1, td);
    snprintf(buf, sizeof buf, "%sly%s", t1, cls);
    graphics_context_set_text_color(ctx, COL_GOOD);
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(px_, y + 13, pw, 16), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  } else {
    GFont fbold = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    GFont freg = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    // Prefixes ("@", ">", ">>") draw in their own column; both star names
    // start at the same x — the width of the widest prefix — so they line
    // up regardless of the browse marker.
    GSize at_sz = graphics_text_layout_get_content_size("@", fbold,
        GRect(0, 0, 40, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    GSize g2_sz = graphics_text_layout_get_content_size(">>", fbold,
        GRect(0, 0, 40, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    int name_x = px_ + (at_sz.w > g2_sz.w ? at_sz.w : g2_sz.w) + 4;

    // --- L1: the star you're at — name bold, then distance and nature
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "@", fbold, GRect(px_, y - 1, 40, 20),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    GSize nsz = graphics_text_layout_get_content_size(cn, fbold,
        GRect(0, 0, pw, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    graphics_draw_text(ctx, cn, fbold, GRect(name_x, y - 1, pw - (name_x - px_), 20),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    char ccls[8] = "";
    if (star_class(g_walk.cur) != '?')
      snprintf(ccls, sizeof ccls, "%c%d ", star_class(g_walk.cur),
               star_subclass(g_walk.cur));
    if (g_walk.cur == STAR_SOL) {
      snprintf(buf, sizeof buf, "home  %s%s", ccls, star_class_desc(g_walk.cur));
    } else {
      fmt1(t1, sizeof t1, star_dist_sol_ly(g_walk.cur));
      snprintf(buf, sizeof buf, "%sly  %s%s", t1, ccls,
               star_class_desc(g_walk.cur));
    }
    graphics_context_set_text_color(ctx, COL_DIM);
    graphics_draw_text(ctx, buf, freg,
                       GRect(name_x + nsz.w + 6, y - 1, pw - (name_x - px_) - nsz.w - 6, 20),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // --- L2: the target — marker in the prefix column, name aligned with L1
    int l2 = y + 14;
    int tw = !health_mode() && temp_fresh() ? 34 : 0;
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, browsing() ? ">>" : ">", fbold,
                       GRect(px_, l2, 40, 20),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    GSize tsz = graphics_text_layout_get_content_size(tn, fbold,
        GRect(0, 0, pw, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    graphics_draw_text(ctx, tn, fbold,
                       GRect(name_x, l2, pw - (name_x - px_) - tw, 20),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    fmt1(t2, sizeof t2, td);
    snprintf(buf, sizeof buf, "%sly%s", t2, cls);
    graphics_context_set_text_color(ctx, COL_GOOD);
    graphics_draw_text(ctx, buf, freg,
                       GRect(name_x + tsz.w + 6, l2, pw - (name_x - px_) - tsz.w - 6 - tw, 20),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    if (!compact) {
      // --- L3: the target's nature, constellation spelled out in full
      const char *conf = star_con_full(tgt);
      if (conf) snprintf(buf, sizeof buf, "%s in %s", star_class_desc(tgt), conf);
      else snprintf(buf, sizeof buf, "%s", star_class_desc(tgt));
      graphics_context_set_text_color(ctx, COL_DIM);
      graphics_draw_text(ctx, buf, freg, GRect(px_, y + 29, pw, 16),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
    if (tw) {                        // star mode keeps the corner temperature
      snprintf(buf, sizeof buf, "%d\xC2\xB0", (int)s_temp);
      graphics_context_set_text_color(ctx, COL_DIM);
      graphics_draw_text(ctx, buf, freg, GRect(b.size.w - 38, l2, 34, 16),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    }
  }
  if (round) {
    char db[40] = "";
    if (g_cfg.date_format == DATE_DAYNUM)
      snprintf(db, sizeof db, "%s %d", WD[s_wday], s_mday);
    else if (g_cfg.date_format == DATE_MONTHDAY)
      snprintf(db, sizeof db, "%s %d", MO[s_mon], s_mday);
    if (temp_fresh()) {
      char tb[12];
      snprintf(tb, sizeof tb, "%s%d\xC2\xB0", db[0] ? "  " : "", (int)s_temp);
      strncat(db, tb, sizeof db - strlen(db) - 1);
    }
    if (db[0]) {
      graphics_context_set_text_color(ctx, COL_DIM);
      graphics_draw_text(ctx, db, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                         GRect(0, b.size.h - 20, b.size.w, 16),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
  }
  #undef SX
  #undef SY
}

// ---------------------------------------------------------------------------
// FLIGHT — the twin paradox, with real stars this time
// ---------------------------------------------------------------------------
static void draw_flight(GContext *ctx, GRect b) {
  char buf[64], t1[20], t2[20];

  graphics_context_set_fill_color(ctx, COL_FAINT);
  for (int i = 0; i < N_STARS_BG; i++)
    graphics_fill_circle(ctx, s_bg[i], i % 7 == 0 ? 1 : 0);

  fmt_time(t1, sizeof t1);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, t1, fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS),
                     GRect(0, 0, b.size.w, 22), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);

  double t = (double)s_elapsed / FLIGHT_MS;
  if (t > 1) t = 1;
  double p = ease(t);

  int rx0 = PBL_IF_ROUND_ELSE(30, 16), rx1 = b.size.w - PBL_IF_ROUND_ELSE(30, 16);
  int ry = b.size.h / 2 + 14;
  graphics_context_set_stroke_color(ctx, COL_FAINT);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(rx0, ry), GPoint(rx1, ry));
  graphics_context_set_fill_color(ctx, COL_GOLD);
  graphics_fill_circle(ctx, GPoint(rx0, ry), 3);
  graphics_context_set_fill_color(ctx, COL_CYAN);
  graphics_fill_circle(ctx, GPoint(rx1, ry), 3);

  int sx = rx0 + (int)((rx1 - rx0) * p);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(sx, ry), 2);
  graphics_context_set_stroke_color(ctx, COL_CYAN);
  graphics_context_set_stroke_width(ctx, 1);
  int plume = 4 + (s_elapsed / FLIGHT_TICK_MS) % 3;
  if (sx - plume > rx0) graphics_draw_line(ctx, GPoint(sx - plume, ry), GPoint(sx - 3, ry));

  bool round = IS_ROUND;
  int lx = round ? 26 : 4;
  char fn[STAR_NAME_MAX], tn[STAR_NAME_MAX];
  star_name(g_hop.from, fn, sizeof fn);
  star_name(g_hop.to, tn, sizeof tn);
  graphics_context_set_text_color(ctx, COL_DIM);
  graphics_draw_text(ctx, fn, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(lx, ry + 8, b.size.w / 2 - lx, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, tn, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(b.size.w / 2, ry + 8, b.size.w / 2 - lx, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  fmt_years(t1, sizeof t1, g_hop.t_uni * p);
  snprintf(buf, sizeof buf, "UNIVERSE  %s", t1);
  graphics_context_set_text_color(ctx, COL_GOLD);
  graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, 26, b.size.w, 28), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
  fmt_years(t2, sizeof t2, g_hop.t_ship * p);
  snprintf(buf, sizeof buf, "SHIP  %s", t2);
  graphics_context_set_text_color(ctx, COL_CYAN);
  graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, 56, b.size.w, 28), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);

  fmt_beta(t1, sizeof t1, g_hop.beta);
  fmt_gamma(t2, sizeof t2, g_hop.gamma);
  snprintf(buf, sizeof buf, "%s  gamma %s  1g", t1, t2);
  graphics_context_set_text_color(ctx, COL_DIM);
  graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(0, round ? ry + 26 : b.size.h - 22, b.size.w, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ---------------------------------------------------------------------------
// Cards
// ---------------------------------------------------------------------------
static int s_x0, s_w;

static void line(GContext *ctx, int *y, const char *txt,
                 GColor color, const char *font_key, int h) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, txt, fonts_get_system_font(font_key),
                     GRect(s_x0, *y, s_w, h + 6),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  *y += h;
}

static int card_top(GContext *ctx, GRect b) {
  bool round = IS_ROUND, compact = IS_COMPACT(b);
  s_x0 = round ? (compact ? 28 : 34) : 6;
  s_w = b.size.w - 2 * s_x0;
  char t1[16];
  fmt_time(t1, sizeof t1);
  graphics_context_set_text_color(ctx, COL_FAINT);
  graphics_draw_text(ctx, t1, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(0, round ? 6 : 0, b.size.w, 16),
                     GTextOverflowModeTrailingEllipsis,
                     round ? GTextAlignmentCenter : GTextAlignmentRight, NULL);
  return round ? (compact ? 22 : 30) : 0;
}

static void draw_arrive(GContext *ctx, GRect b) {
  bool compact = IS_COMPACT(b);
  int lh = compact ? 14 : 16;
  char buf[96], t1[20], t2[20], nm[STAR_NAME_MAX];
  int y = card_top(ctx, b);

  line(ctx, &y, "ARRIVED", COL_GOOD, FONT_KEY_GOTHIC_28_BOLD, compact ? 26 : 30);
  star_name(g_hop.to, nm, sizeof nm);
  line(ctx, &y, nm, GColorWhite, FONT_KEY_GOTHIC_18_BOLD, compact ? 20 : 22);
  const char *con = star_con3(g_hop.to);
  if (con) snprintf(buf, sizeof buf, "%s in %s", star_class_desc(g_hop.to), con);
  else snprintf(buf, sizeof buf, "%s", star_class_desc(g_hop.to));
  line(ctx, &y, buf, COL_DIM, FONT_KEY_GOTHIC_14, lh);
  fmt1(t1, sizeof t1, star_dist_sol_ly(g_hop.to));
  snprintf(buf, sizeof buf, "%s ly from Sol", t1);
  line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh + 2);
  fmt_years(t1, sizeof t1, g_hop.t_uni);
  fmt_years(t2, sizeof t2, g_hop.t_ship);
  snprintf(buf, sizeof buf, "universe +%s", t1);
  line(ctx, &y, buf, COL_GOLD, FONT_KEY_GOTHIC_18_BOLD, compact ? 18 : 20);
  snprintf(buf, sizeof buf, "you +%s", t2);
  line(ctx, &y, buf, COL_CYAN, FONT_KEY_GOTHIC_18_BOLD, compact ? 18 : 22);
  fmt1(t1, sizeof t1, g_walk.path_ly10 / 10.0);
  snprintf(buf, sizeof buf, "today: %d hops, %s ly", g_walk.hops, t1);
  line(ctx, &y, buf, COL_DIM, FONT_KEY_GOTHIC_14, lh);
}

static void draw_summary(GContext *ctx, GRect b) {
  bool compact = IS_COMPACT(b);
  int lh = compact ? 14 : 16;
  char buf[96], t1[20], nm[STAR_NAME_MAX];
  int y = card_top(ctx, b);

  line(ctx, &y, "DAY COMPLETE", COL_GOLD, FONT_KEY_GOTHIC_28_BOLD, compact ? 26 : 30);
  fmt1(t1, sizeof t1, g_walk.prev_path_ly10 / 10.0);
  snprintf(buf, sizeof buf, "%d hops, %s ly wandered", g_walk.prev_hops, t1);
  line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_18_BOLD, compact ? 20 : 22);
  star_name(g_walk.prev_end, nm, sizeof nm);
  snprintf(buf, sizeof buf, "ended at %s", nm);
  line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
  fmt1(t1, sizeof t1, g_walk.prev_far_ly10 / 10.0);
  snprintf(buf, sizeof buf, "farthest out: %s ly%s", t1,
           g_walk.prev_far_ly10 >= g_wrec.far_ever_ly10 &&
           g_walk.prev_far_ly10 > 0 ? " *BEST*" : "");
  line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
  snprintf(buf, sizeof buf, "streak %d   day %d all-time",
           g_walk.streak, g_wrec.days);
  line(ctx, &y, buf, COL_DIM, FONT_KEY_GOTHIC_14, lh + 2);
  line(ctx, &y, "back at Sol - good morning", COL_CYAN, FONT_KEY_GOTHIC_14, lh);
}

// ---------------------------------------------------------------------------
// Window plumbing
// ---------------------------------------------------------------------------
static void draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  switch (s_mode) {
    case MODE_MAP:     draw_map(ctx, b); break;
    case MODE_FLIGHT:  draw_flight(ctx, b); break;
    case MODE_ARRIVE:  draw_arrive(ctx, b); break;
    case MODE_SUMMARY: draw_summary(ctx, b); break;
  }
}

static void set_clock(struct tm *t) {
  s_hour = t->tm_hour;
  s_min = t->tm_min;
  s_mday = t->tm_mday;
  s_mon = t->tm_mon;
  s_wday = t->tm_wday;
}

static void tick_handler(struct tm *t, TimeUnits changed) {
  set_clock(t);
  minute_track(t);
  SchedEvent e = scheduler_tick(t);
  if (e == SCHED_NEWDAY) face_show_summary();
  else if (e == SCHED_HOPPED) face_show_hop();
  else if (s_layer) layer_mark_dirty(s_layer);
}

static void tap_handler(AccelAxisType axis, int32_t dir) {
  if (!g_cfg.tap_info || s_mode != MODE_MAP) return;
  // both modes: browse the neighbor board — display only, the bell's pick
  // is untouched
  int window = cur_window();
  int n = board_count(window);
  if (n > 0) {
    int cur = browsing() ? s_browse % n : board_sel(window, s_hour, s_min);
    s_browse = (cur + 1) % n;
    s_browse_at = time(NULL);
  }
  if (health_mode()) {             // health mode: also peek the night's sleep
    s_sleep_peek_at = time(NULL);
    if (s_peek_timer) app_timer_cancel(s_peek_timer);
    s_peek_timer = app_timer_register(SLEEP_PEEK_MS + 300, peek_expire, NULL);
  }
  if (s_layer) layer_mark_dirty(s_layer);
}

static void bt_handler(bool connected) {
  if (!connected && s_bt_ok && g_cfg.bt_vibe && !quiet_time_is_active())
    vibes_short_pulse();
  s_bt_ok = connected;
  if (s_layer) layer_mark_dirty(s_layer);
}

static void batt_handler(BatteryChargeState st) {
  s_batt = st.charge_percent;
  if (s_layer) layer_mark_dirty(s_layer);
}

static void win_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, draw);
  layer_add_child(root, s_layer);
}

static void win_unload(Window *w) {
  layer_destroy(s_layer);
  s_layer = NULL;
}

void face_init(void) {
  time_t now = time(NULL);
  set_clock(localtime(&now));
  s_bt_ok = connection_service_peek_pebble_app_connection();
  s_batt = battery_state_service_peek().charge_percent;
  weather_load();

  s_win = window_create();
  window_set_background_color(s_win, GColorBlack);
  window_set_window_handlers(s_win, (WindowHandlers){
    .load = win_load, .unload = win_unload });
  window_stack_push(s_win, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  accel_tap_service_subscribe(tap_handler);
  connection_service_subscribe((ConnectionHandlers){
    .pebble_app_connection_handler = bt_handler });
  battery_state_service_subscribe(batt_handler);
  health_init();
}

void face_deinit(void) {
  cancel_timer();
  if (s_peek_timer) { app_timer_cancel(s_peek_timer); s_peek_timer = NULL; }
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
  connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  health_deinit();
  window_destroy(s_win);
}
