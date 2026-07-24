#include "ui.h"
#include "settings.h"
#include "scheduler.h"
#include "autopilot.h"
#include "offers.h"

// The watchface: the docked star map as a clock. The gold route sweeps to a
// new offer every minute; on the hour the ship flies it. One window, five
// draw-states — a watchface gets no buttons, so scenes time themselves out.

typedef enum { MODE_MAP, MODE_FLIGHT, MODE_RESULTS, MODE_SUMMARY, MODE_INFO } Mode;

#define FLIGHT_MS 4200
#define FLIGHT_TICK_MS 50
#define RESULTS_MS 8000
#define SUMMARY_MS 10000
#define INFO_MS 6000
#define N_STARS 26

static Window *s_win;
static Layer *s_layer;
static Mode s_mode = MODE_MAP;
static AppTimer *s_timer;              // scene timeout / flight animation
static int s_elapsed;                  // flight ms
static GPoint s_stars[N_STARS];
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

static double ease(double t) { return t * t * (3 - 2 * t); }   // smoothstep

static bool chart_mode(void) { return g_cfg.face_mode == FMODE_CHART; }

// ---------------------------------------------------------------------------
// Health — chart mode trades the economy readouts for the pilot's own body.
// Values are peeked at draw time; the minute tick is refresh enough.
// ---------------------------------------------------------------------------
static void fmt_thousands(char *buf, size_t cap, int v) {
  if (v >= 1000) snprintf(buf, cap, "%d,%03d", v / 1000, v % 1000);
  else snprintf(buf, cap, "%d", v);
}

// DEV_FAKE_HEALTH: synthetic sleep/steps/HR/minute curve — the emulator has
// no health history, so this is the only way to see these features render.
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
static int step_goal(void) {
  time_t start = time_start_of_today();
  HealthValue avg = health_service_sum_averaged(HealthMetricStepCount,
      start, start + SECONDS_PER_DAY, HealthServiceTimeScopeDaily);
  return avg >= 500 ? (int)avg : 10000;   // no history yet: a classic 10k
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

// ---------------------------------------------------------------------------
// Past-hour activity trace — chart mode's gauge. Three lessons inherited from
// ActiveHour: minute-history records can be missing or is_invalid (their
// .steps is undefined garbage — never read it); the newest ~15 minutes land
// in delayed batches, so refetch once just past the next quarter-hour; and
// the current minute is tracked live by diffing sum_today, never the
// (expensive, laggy) history API.
// ---------------------------------------------------------------------------
static uint8_t s_minsteps[60];       // steps per wall-clock minute of the hour
static int s_step_snap = -1;         // sum_today at the current minute's start
static bool s_refetch_pending;

static void fetch_minute_history(void) {
#ifdef DEV_FAKE_HEALTH
  // a believable hour: sat still, a 6-min walk, quiet, errands, just walked
  for (int i = 0; i < 60; i++) {
    int ago = 59 - i;                     // minutes before now
    int st = 0;
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

static void minute_track(struct tm *t) {   // every minute tick
  s_minsteps[t->tm_min] = 0;               // fresh minute starts still
  s_step_snap = steps_today();
  if (s_refetch_pending && t->tm_min % 15 == 1) {
    fetch_minute_history();                // the delayed batch has landed
    s_refetch_pending = false;
  }
}

static int minute_steps_live(void) {       // this minute so far, at draw time
  if (s_step_snap < 0) return 0;
  int d = steps_today() - s_step_snap;
  return d > 0 ? d : 0;
}

static void health_evt(HealthEventType e, void *ctx) {
  if (e == HealthEventMovementUpdate) face_poke();   // live spark growth
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
static int step_goal(void)      { return 10000; }
static int hr_bpm(void)         { return 0; }
static int sleep_secs(void)     { return 0; }
static uint8_t s_minsteps[60];
static void minute_track(struct tm *t) { (void)t; }
static int minute_steps_live(void) { return 0; }
static void health_init(void) {}
static void health_deinit(void) {}
#endif

// ---------------------------------------------------------------------------
// Weather — the phone fetches (Open-Meteo, see pkjs), the watch just shows
// the latest number. Persisted so a face restart keeps the last reading;
// hidden once it goes stale.
// ---------------------------------------------------------------------------
#define KEY_WEATHER 30
typedef struct { int32_t at; int16_t temp; } WeatherSave;
static int16_t s_temp;
static int32_t s_temp_at;            // epoch of the reading, 0 = never

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
  if (s_elapsed >= FLIGHT_MS + 400) {    // hold the final frame a beat
    s_timer = NULL;
    if (chart_mode()) back_to_map(NULL); // no invoice to read
    else enter_card(MODE_RESULTS, RESULTS_MS);
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
  for (int i = 0; i < N_STARS; i++)
    s_stars[i] = GPoint(rand() % b.size.w, rand() % b.size.h);
  s_timer = app_timer_register(FLIGHT_TICK_MS, flight_tick, NULL);
  layer_mark_dirty(s_layer);
}

static void do_vibe(void) {
  if (quiet_time_is_active()) return;
  if (chart_mode()) {
    // outcomes are hidden, so a hop is just a hop: one quiet pulse
    if (g_cfg.vibe_mode == VIBE_DELIVERY) vibes_short_pulse();
    return;
  }
  if (g_cfg.vibe_mode == VIBE_DELIVERY) {
    if (g_last.ok) vibes_double_pulse();
    else vibes_short_pulse();
  } else if (g_cfg.vibe_mode == VIBE_RECORDS) {
    if (g_last.licensed) vibes_double_pulse();
  }
}

void face_show_run(void) {
  do_vibe();
  if (!s_layer) return;
  if (g_cfg.cutscene == CUT_FULL) enter_flight();
  else if (g_cfg.cutscene == CUT_RESULTS && !chart_mode())
    enter_card(MODE_RESULTS, RESULTS_MS);
  else layer_mark_dirty(s_layer);
}

void face_show_summary(void) {
  if (!s_layer) return;
  if (chart_mode()) { layer_mark_dirty(s_layer); return; }   // all ledger — skip
  enter_card(MODE_SUMMARY, SUMMARY_MS);
}

void face_poke(void) {
  if (s_layer) layer_mark_dirty(s_layer);
}

// ---------------------------------------------------------------------------
// Star map projection — same chart as the game: core cluster sets the scale,
// deep halo stations pin to the frame edge along their true bearing.
// ---------------------------------------------------------------------------
typedef struct {
  GRect area;
  float cx, cz, scale;
  int mx, my;
} Proj;

static Proj make_proj(GRect area) {
  Proj p;
  float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
  for (int i = 0; i < g_n_stations; i++) {
    Station *s = &g_stations[i];
    if (s->deep) continue;
    if (s->x < minx) minx = s->x;
    if (s->x > maxx) maxx = s->x;
    if (s->z < minz) minz = s->z;
    if (s->z > maxz) maxz = s->z;
  }
  float dx = maxx - minx, dz = maxz - minz;
  if (dx < 1) dx = 1;
  if (dz < 1) dz = 1;
  float sx = (area.size.w - 16) / dx, sz = (area.size.h - 16) / dz;
  p.area = area;
  p.scale = sx < sz ? sx : sz;
  p.cx = (minx + maxx) / 2;
  p.cz = (minz + maxz) / 2;
  p.mx = area.origin.x + area.size.w / 2;
  p.my = area.origin.y + area.size.h / 2;
  return p;
}

static GPoint proj_of(const Proj *p, int idx) {
  Station *s = &g_stations[idx];
  float dx = s->x - p->cx, dz = s->z - p->cz;
  if (!s->deep)
    return GPoint(p->mx + (int)(dx * p->scale), p->my + (int)(dz * p->scale));
  int hw = p->area.size.w / 2 - 7, hh = p->area.size.h / 2 - 7;
  if (hw < 4) hw = 4;
  if (hh < 4) hh = 4;
  float ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz;
  float t = 1e9f;
  if (ax > 1e-4f) { float tx = hw / ax; if (tx < t) t = tx; }
  if (az > 1e-4f) { float tz = hh / az; if (tz < t) t = tz; }
  if (t > 1e8f) t = 0;
  return GPoint(p->mx + (int)(dx * t), p->my + (int)(dz * t));
}

// ---------------------------------------------------------------------------
// MAP — the resting face
// ---------------------------------------------------------------------------
// Chrome budget shared by the map and the info overlay that sits on it.
// Chart mode's gauge is a 10px activity trace instead of a 2px fuel line,
// so its top chrome runs deeper — but the tall rects (emery) have slack
// under their big clock, so they pay half and the map keeps the rest.
static void chrome_metrics(GRect b, int *top_h, int *bot_h, int *inset) {
  bool round = IS_ROUND, compact = IS_COMPACT(b);
  *top_h = round ? 48 : (compact ? 50 : 60);
  *bot_h = round ? 62 : (compact ? 36 : 50);
  *inset = round ? 24 : 0;
  if (chart_mode()) *top_h += (round || compact) ? 8 : 4;
}

static void draw_map(GContext *ctx, GRect b) {
  bool round = IS_ROUND, compact = IS_COMPACT(b);
  int top_h, bot_h, inset;
  chrome_metrics(b, &top_h, &bot_h, &inset);
  char buf[96], t1[16];

  // --- clock
  fmt_time(t1, sizeof t1);
  if (round) {
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, t1,
                       fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM),
                       GRect(0, 2, b.size.w, 28), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  } else {
    graphics_context_set_text_color(ctx, GColorWhite);
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
  }

  // --- vitals: where you're docked — plus the ledger, or sleep and pulse
  char nm[NAME_LEN];
  station_short_name(g.station, nm, sizeof nm);
  int gh = chart_mode() ? 10 : 2;          // gauge height below the vitals
  if (chart_mode()) {
    // before 10am the night still matters: last night's sleep rides along
    char sl[16] = "";
    int ss = s_hour < 10 ? sleep_secs() : 0;
#ifdef DEV_FAKE_HEALTH
    ss = sleep_secs();                     // dev: visible at any hour
#endif
    if (ss > 0) {
      unsigned hh = ((unsigned)ss / 3600u) % 100u, mm = ((unsigned)ss / 60u) % 60u;
      snprintf(sl, sizeof sl, "  %uh%02um", hh, mm);
    }
    int hr = hr_bpm();
    if (hr > 0) snprintf(buf, sizeof buf, "@%s%s  %dbpm", nm, sl, hr);
    else snprintf(buf, sizeof buf, "@%s%s", nm, sl);
  } else {
    snprintf(buf, sizeof buf, "@%s  $%ld  x%d", nm, (long)g.credits, g.deliveries);
  }
  graphics_context_set_text_color(ctx, COL_DIM);
  graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(inset, top_h - 16 - gh, b.size.w - 2 * inset, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // --- the gold strip: fuel tank, or the past hour minute by minute — the
  // area under the activity curve fills gold, the sky above stays gray
  int gx0 = round ? 40 : 2, gw = b.size.w - 2 * gx0;
  int gy0 = top_h - gh;
  if (chart_mode()) {
    int live = minute_steps_live();
    s_minsteps[s_min] = live > 255 ? 255 : live;
#ifdef PBL_COLOR
    // a faint ruler behind the curve: full-height tick at the hour boundary,
    // short ticks each 5 minutes — orange rides on plain black otherwise.
    // (B&W skips the ruler: white ticks would read as activity.)
    graphics_context_set_fill_color(ctx, COL_FAINT);
    graphics_fill_rect(ctx, GRect(gx0, gy0, gw, 1), 0, GCornerNone);   // ceiling
    for (int i = 0; i < 60; i++) {
      int wall = (s_min + 1 + i) % 60;
      if (wall % 5) continue;
      graphics_fill_rect(ctx, GRect(gx0 + gw * i / 60, gy0, 1, wall == 0 ? gh : 3),
                         0, GCornerNone);
    }
#endif
    graphics_context_set_fill_color(ctx, COL_GOLD);
    const int cap = 90;                    // ~a solid minute of walking
    for (int i = 0; i < 60; i++) {         // left = 59 min ago, right = now
      int x = gx0 + gw * i / 60, xe = gx0 + gw * (i + 1) / 60;
      int st = s_minsteps[(s_min + 1 + i) % 60];
      if (st > cap) st = cap;
      int hcol = 1 + st * (gh - 1) / cap;
      graphics_fill_rect(ctx, GRect(x, gy0 + gh - hcol, xe - x, hcol), 0, GCornerNone);
    }
  } else {
    float frac = g.fuel / tank_cap();
    if (frac > 1) frac = 1;
    if (frac < 0) frac = 0;
    graphics_context_set_fill_color(ctx, COL_FAINT);
    graphics_fill_rect(ctx, GRect(gx0, gy0, gw, gh), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, COL_GOLD);
    graphics_fill_rect(ctx, GRect(gx0, gy0, (int)(gw * frac), gh), 0, GCornerNone);
  }

  GRect map_area = GRect(inset, top_h + 2, b.size.w - 2 * inset,
                         b.size.h - top_h - bot_h - 2);
  Proj pj = make_proj(map_area);

  // the minute's offer, off the windowed all-destinations board
  Contract oc;
  offers_get(offers_sel(s_hour, s_min), offers_window(s_hour, s_min), &oc);

  // --- selected route (under the dots)
  GPoint here = proj_of(&pj, g.station);
  GPoint dst = proj_of(&pj, oc.to);
  graphics_context_set_stroke_color(ctx, COL_GOLD);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, here, dst);

  // --- stations: every port is an offer now, so dots stay plain — only the
  // minute's target gets the cyan halo
  for (int i = 0; i < g_n_stations; i++) {
    Station *s = &g_stations[i];
    if (s->deep && !g.deep_license) continue;
    bool is_target = oc.to == i;
    bool is_here = (i == (int)g.station);
    if (s->deep && !g_cfg.show_signposts && !is_target && !is_here) continue;
    GPoint p = proj_of(&pj, i);
    if (s->deep) {
      int r = (is_target || is_here) ? 3 : 2;
      graphics_context_set_fill_color(ctx, is_here ? COL_GOLD
                                                   : (is_target ? COL_CYAN : COL_DIM));
      graphics_fill_rect(ctx, GRect(p.x - r, p.y - r, r * 2 + 1, r * 2 + 1), 0, GCornerNone);
      if (is_here) {
        graphics_context_set_stroke_color(ctx, COL_GOLD);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_circle(ctx, p, 6);
      }
    } else if (is_here) {
      graphics_context_set_fill_color(ctx, COL_GOLD);
      graphics_fill_circle(ctx, p, 3);
      graphics_context_set_stroke_color(ctx, COL_GOLD);
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_circle(ctx, p, 6);
    } else if (is_target) {
      graphics_context_set_fill_color(ctx, COL_CYAN);
      graphics_fill_circle(ctx, p, 4);
    } else {
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_circle(ctx, p, 2);
    }
    if (is_target) {
      char tn[NAME_LEN];
      station_short_name(i, tn, sizeof tn);
      int lx = p.x < b.size.w / 2 ? p.x + 6 : p.x - 66;
      int ly = p.y < map_area.origin.y + 14 ? p.y + 4 : p.y - 16;
      graphics_context_set_text_color(ctx, COL_CYAN);
      graphics_draw_text(ctx, tn, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                         GRect(lx, ly, 62, 16), GTextOverflowModeTrailingEllipsis,
                         p.x < b.size.w / 2 ? GTextAlignmentLeft : GTextAlignmentRight, NULL);
    }
  }

  // --- comms / power, tucked into the map corners
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

  // --- bottom panel: the minute's contract
  int y = b.size.h - bot_h;
  int px = round ? 26 : 4;
  int pw = b.size.w - 2 * px;
  GTextAlignment align = round ? GTextAlignmentCenter : GTextAlignmentLeft;
  graphics_context_set_stroke_color(ctx, COL_FAINT);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(inset, y), GPoint(b.size.w - inset, y));

  Contract *c = &oc;
  char cn[NAME_LEN];
  station_short_name(c->to, cn, sizeof cn);
  snprintf(buf, sizeof buf, "%s > %s", c->type == 0 ? "CARGO" : "PAX", cn);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf,
                     fonts_get_system_font(compact || round ? FONT_KEY_GOTHIC_14_BOLD
                                                            : FONT_KEY_GOTHIC_18_BOLD),
                     GRect(px, y - 2, pw, 20), GTextOverflowModeTrailingEllipsis, align, NULL);
  int l2 = y + (compact || round ? 13 : 17);
  if (!compact && !round) {
    graphics_context_set_text_color(ctx, COL_DIM);
    graphics_draw_text(ctx, c->what, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(px, l2, pw, 16), GTextOverflowModeTrailingEllipsis, align, NULL);
    l2 += 16;
  }
  // the temperature borrows the stats line's right corner on rect screens
  int pw2 = pw - (temp_fresh() && !round ? 30 : 0);
  if (chart_mode()) {
    // the route stays, the invoice goes: your own legs, not the ship's
    char st[16], km[16];
    fmt_thousands(st, sizeof st, steps_today());
    fmt1(km, sizeof km, walked_m_today() / 1000.0);
    if (round || compact)
      snprintf(buf, sizeof buf, "%s steps  %skm", st, km);
    else
      snprintf(buf, sizeof buf, "%s steps  %skm  %dkcal", st, km, kcal_today());
    graphics_context_set_text_color(ctx, COL_GOOD);
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(px, l2, pw2, 16), GTextOverflowModeTrailingEllipsis, align, NULL);
  } else {
    RunPlan p = game_plan(c);
    bool ok = p.deadline_ok && p.aging_ok && p.retire_ok;
    if (round || compact)
      snprintf(buf, sizeof buf, "$%ld  %dg  %dly  %s",
               (long)contract_pay(c), c->g_limit, (int)(c->d + 0.5f), ok ? "OK" : "!!");
    else
      snprintf(buf, sizeof buf, "$%ld  %dg  %dly  DL%d  %s",
               (long)contract_pay(c), c->g_limit, (int)(c->d + 0.5f),
               (int)(c->deadline + 0.5f), ok ? "OK" : "!!");
    graphics_context_set_text_color(ctx, ok ? COL_GOOD : COL_BAD);
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(px, l2, pw2, 16), GTextOverflowModeTrailingEllipsis, align, NULL);
  }
  if (temp_fresh() && !round) {
    snprintf(buf, sizeof buf, "%d\xC2\xB0", (int)s_temp);
    graphics_context_set_text_color(ctx, COL_DIM);
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(b.size.w - 46, l2, 42, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }

  // round screens: date (and temperature) ride the bottom arc
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
}

// ---------------------------------------------------------------------------
// FLIGHT — the twin-paradox cutscene, clock still on top
// ---------------------------------------------------------------------------
static void draw_flight(GContext *ctx, GRect b) {
  char buf[64], t1[20], t2[20];

  graphics_context_set_fill_color(ctx, COL_FAINT);
  for (int i = 0; i < N_STARS; i++)
    graphics_fill_circle(ctx, s_stars[i], i % 7 == 0 ? 1 : 0);

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
  graphics_context_set_text_color(ctx, COL_DIM);
  graphics_draw_text(ctx, g_auto_from, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(lx, ry + 8, b.size.w / 2 - lx, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, g_auto_to, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(b.size.w / 2, ry + 8, b.size.w / 2 - lx, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // the two clocks — the whole point
  fmt_years(t1, sizeof t1, g_last.t_uni * p);
  snprintf(buf, sizeof buf, "UNIVERSE  %s", t1);
  graphics_context_set_text_color(ctx, COL_GOLD);
  graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, 26, b.size.w, 28), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
  fmt_years(t2, sizeof t2, g_last.t_ship * p);
  snprintf(buf, sizeof buf, "SHIP  %s", t2);
  graphics_context_set_text_color(ctx, COL_CYAN);
  graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, 56, b.size.w, 28), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);

  fmt_beta(t1, sizeof t1, g_last.beta);
  fmt_gamma(t2, sizeof t2, g_last.gamma);
  snprintf(buf, sizeof buf, "%s  gamma %s  %dg", t1, t2, g_last.g_limit);
  graphics_context_set_text_color(ctx, COL_DIM);
  graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(0, round ? ry + 26 : b.size.h - 22, b.size.w, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ---------------------------------------------------------------------------
// Cards: shared line helper
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

// ---------------------------------------------------------------------------
// RESULTS — what the last bell did
// ---------------------------------------------------------------------------
static void draw_results(GContext *ctx, GRect b) {
  bool compact = IS_COMPACT(b);
  int lh = compact ? 14 : 16;
  char buf[96], t1[20], t2[20];
  int y = card_top(ctx, b);

  line(ctx, &y, g_last.ok ? "DELIVERED" : "CONTRACT FAILED",
       g_last.ok ? COL_GOOD : COL_BAD, FONT_KEY_GOTHIC_28_BOLD, compact ? 26 : 30);
  line(ctx, &y, g_last.what, COL_DIM, FONT_KEY_GOTHIC_14, lh);
  snprintf(buf, sizeof buf, "to %s", g_last.to_name);
  line(ctx, &y, buf, COL_DIM, FONT_KEY_GOTHIC_14, lh + 2);

  snprintf(buf, sizeof buf, "PAID $%ld", (long)g_last.pay);
  line(ctx, &y, buf, COL_GOLD, FONT_KEY_GOTHIC_24_BOLD, compact ? 24 : 26);
  if (g_last.late)
    line(ctx, &y, "LATE - pay docked 75%", COL_BAD, FONT_KEY_GOTHIC_14, lh);
  if (g_last.aged_out)
    line(ctx, &y, "PAX aged past cap - pay docked 80%", COL_BAD, FONT_KEY_GOTHIC_14, lh);

  fmt_years(t1, sizeof t1, g_last.t_uni);
  fmt_years(t2, sizeof t2, g_last.t_ship);
  snprintf(buf, sizeof buf, "uni %s  ship %s", t1, t2);
  line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
  snprintf(buf, sizeof buf, "$%ld  x%d  %s", (long)g.credits, g.deliveries,
           rank_for(g.credits));
  line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh + 2);

  if (g_last.licensed)
    line(ctx, &y, "DEEP SPACE LICENSE EARNED", COL_CYAN, FONT_KEY_GOTHIC_14, lh);
  if (g_last.vignette[0]) {
    graphics_context_set_text_color(ctx, COL_CYAN);
    graphics_draw_text(ctx, g_last.vignette, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(s_x0, y, s_w, b.size.h - y), GTextOverflowModeWordWrap,
                       GTextAlignmentLeft, NULL);
  }
}

// ---------------------------------------------------------------------------
// SUMMARY — yesterday folded, a fresh map today
// ---------------------------------------------------------------------------
static void draw_summary(GContext *ctx, GRect b) {
  bool compact = IS_COMPACT(b);
  int lh = compact ? 14 : 16;
  char buf[96], t1[20];
  int y = card_top(ctx, b);

  line(ctx, &y, "DAY COMPLETE", COL_GOLD, FONT_KEY_GOTHIC_28_BOLD, compact ? 26 : 30);
  snprintf(buf, sizeof buf, "CLOSED AT $%ld%s", (long)g_sched.prev_balance,
           g_sched.prev_balance >= g_rec.best_balance ? " *BEST*" : "");
  line(ctx, &y, buf, COL_GOLD, FONT_KEY_GOTHIC_18_BOLD, compact ? 20 : 22);
  snprintf(buf, sizeof buf, "RANK: %s", rank_for(g_sched.prev_balance));
  line(ctx, &y, buf, COL_GOLD, FONT_KEY_GOTHIC_18_BOLD, compact ? 20 : 24);
  snprintf(buf, sizeof buf, "%d delivered%s", g_sched.prev_deliveries,
           g_sched.prev_deliveries >= g_rec.best_deliveries ? " *BEST*" : "");
  line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
  fmt_gamma(t1, sizeof t1, g_sched.prev_gamma);
  snprintf(buf, sizeof buf, "highest gamma: %s", t1);
  line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
  snprintf(buf, sizeof buf, "streak: %d day%s   day %d all-time",
           g_sched.streak, g_sched.streak == 1 ? "" : "s", g_rec.careers);
  line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh + 2);
  snprintf(buf, sizeof buf, "new map: %s", g.seed);
  line(ctx, &y, buf, COL_CYAN, FONT_KEY_GOTHIC_14, lh);
}

// ---------------------------------------------------------------------------
// INFO — tap: a boxed panel over the star map. The clock, vitals, gauge, and
// contract panel stay visible around it, so it only carries what they don't:
// rank, failures, gamma, universe time, streak, records — or goal math.
// ---------------------------------------------------------------------------
static void draw_info_overlay(GContext *ctx, GRect b) {
  bool round = IS_ROUND, compact = IS_COMPACT(b);
  int top_h, bot_h, inset;
  chrome_metrics(b, &top_h, &bot_h, &inset);
  GRect box = GRect(inset + 2, top_h + 2,
                    b.size.w - 2 * (inset + 2), b.size.h - top_h - bot_h - 4);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, box, 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COL_FAINT);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, box);

  s_x0 = box.origin.x + 5;
  s_w = box.size.w - 10;
  int y = box.origin.y - 2;
  int lh = 14;
  bool tall = !compact;                  // emery/gabbro: room for two more lines
  const char *hdr_font = round ? FONT_KEY_GOTHIC_14_BOLD : FONT_KEY_GOTHIC_18_BOLD;
  int hdr_h = round ? 16 : 20;
  char buf[96], t1[20], t2[20];

  if (chart_mode()) {
    int goal = step_goal();
    int pct = goal > 0 ? (int)((int64_t)steps_today() * 100 / goal) : 0;
    if (pct > 999) pct = 999;
    snprintf(buf, sizeof buf, "%d%% of daily avg", pct);
    line(ctx, &y, buf, COL_GOLD, hdr_font, hdr_h);
    fmt_thousands(t1, sizeof t1, steps_today());
    fmt_thousands(t2, sizeof t2, goal);
    snprintf(buf, sizeof buf, "%s / %s steps", t1, t2);
    line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
    snprintf(buf, sizeof buf, "%d active kcal", kcal_today());
    line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
    int hr = hr_bpm();
    if (hr > 0) {
      snprintf(buf, sizeof buf, "heart %d bpm", hr);
      line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
    }
    if (tall) {
      snprintf(buf, sizeof buf, "map: %s", g.seed);
      line(ctx, &y, buf, COL_FAINT, FONT_KEY_GOTHIC_14, lh);
    }
    return;
  }

  line(ctx, &y, rank_for(g.credits), COL_GOLD, hdr_font, hdr_h);
  fmt_gamma(t1, sizeof t1, g.max_gamma);
  snprintf(buf, sizeof buf, "%d failed  gamma %s%s", g.failures, t1,
           g.deep_license ? "  DEEP" : "");
  line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
  fmt_years(t1, sizeof t1, g.uni_time);
  snprintf(buf, sizeof buf, "uni %s  streak %dd", t1, (int)g_sched.streak);
  line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
  if (g_rec.best_balance > 0) {
    snprintf(buf, sizeof buf, "best day $%ld", (long)g_rec.best_balance);
    line(ctx, &y, buf, COL_DIM, FONT_KEY_GOTHIC_14, lh);
  }
  if (tall) {
    if (g_sched.prev_day_key) {
      snprintf(buf, sizeof buf, "yesterday $%ld  x%d",
               (long)g_sched.prev_balance, g_sched.prev_deliveries);
      line(ctx, &y, buf, COL_DIM, FONT_KEY_GOTHIC_14, lh);
    }
    snprintf(buf, sizeof buf, "map: %s", g.seed);
    line(ctx, &y, buf, COL_FAINT, FONT_KEY_GOTHIC_14, lh);
  }
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
    case MODE_RESULTS: draw_results(ctx, b); break;
    case MODE_SUMMARY: draw_summary(ctx, b); break;
    case MODE_INFO:    draw_map(ctx, b); draw_info_overlay(ctx, b); break;
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
  else if (e == SCHED_RAN) face_show_run();
  else if (s_layer) layer_mark_dirty(s_layer);
}

static void tap_handler(AccelAxisType axis, int32_t dir) {
  if (!g_cfg.tap_info) return;
  if (s_mode == MODE_MAP) enter_card(MODE_INFO, INFO_MS);
  else if (s_mode == MODE_INFO) { cancel_timer(); back_to_map(NULL); }
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
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
  connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  health_deinit();
  window_destroy(s_win);
}
