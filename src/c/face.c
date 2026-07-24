#include "ui.h"
#include "settings.h"
#include "scheduler.h"
#include "stars.h"
#include "walk.h"

// Solfarer's face: the Local Bubble as a clock. A traveling 40-ly viewport
// rides the daily wander; the gold route sweeps to a different neighbor
// every minute; on the bell the ship flies it — twin paradox and all. One
// window, five draw-states; scenes time themselves out.

typedef enum { MODE_MAP, MODE_FLIGHT, MODE_ARRIVE, MODE_SUMMARY, MODE_INFO } Mode;

#define FLIGHT_MS 4200
#define FLIGHT_TICK_MS 50
#define ARRIVE_MS 7000
#define SUMMARY_MS 10000
#define INFO_MS 6000
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
  if (g_cfg.hop_vibe && !quiet_time_is_active()) vibes_short_pulse();
  if (!s_layer) return;
  if (g_cfg.cutscene == CUT_FULL) enter_flight();
  else layer_mark_dirty(s_layer);
}

void face_show_summary(void) {
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
  }

  // --- vitals: where you are — and how far from home
  char nm[STAR_NAME_MAX];
  star_name(g_walk.cur, nm, sizeof nm);
  int gh = health_mode() ? 10 : 2;
  if (health_mode()) {
    char sl[16] = "";
    int ss = s_hour < 10 ? sleep_secs() : 0;
#ifdef DEV_FAKE_HEALTH
    ss = sleep_secs();
#endif
    if (ss > 0) {
      unsigned hh = ((unsigned)ss / 3600u) % 100u, mm = ((unsigned)ss / 60u) % 60u;
      snprintf(sl, sizeof sl, "  %uh%02um", hh, mm);
    }
    int hr = hr_bpm();
    if (hr > 0) snprintf(buf, sizeof buf, "@%s%s  %dbpm", nm, sl, hr);
    else snprintf(buf, sizeof buf, "@%s%s", nm, sl);
  } else {
    fmt1(t1, sizeof t1, star_dist_sol_ly(g_walk.cur));
    snprintf(buf, sizeof buf, "@%s  %sly out", nm, t1);
  }
  graphics_context_set_text_color(ctx, COL_DIM);
  graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(inset, top_h - 16 - gh, b.size.w - 2 * inset, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

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
  int sel = board_sel(window, s_hour, s_min);
  int tgt = board_count(window) > 0 ? board_star(window, sel) : g_walk.cur;

  // route first, under the dots
  int tx, ty, tz;
  star_pos01(tgt, &tx, &ty, &tz);
  graphics_context_set_stroke_color(ctx, COL_GOLD);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(SX(curx), SY(cury)), GPoint(SX(tx), SY(ty)));

  // the neighborhood, in its true colors
  for (int i = 0; i < CAT_COUNT; i++) {
    const StarRec *r = star_rec(i);
    if (r->z - curz > SLAB_01 || curz - r->z > SLAB_01) continue;
    int px = SX(r->x), py = SY(r->y);
    if (px < area.origin.x - 4 || px > area.origin.x + area.size.w + 4 ||
        py < area.origin.y - 4 || py > area.origin.y + area.size.h + 4)
      continue;
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
      graphics_context_set_text_color(ctx, COL_GOLD);
      int lx = ex < mx ? ex + 5 : ex - 33;
      int ly = ey < my ? ey + 2 : ey - 16;
      graphics_draw_text(ctx, "SOL", fonts_get_system_font(FONT_KEY_GOTHIC_14),
                         GRect(lx, ly, 30, 16), GTextOverflowModeTrailingEllipsis,
                         ex < mx ? GTextAlignmentLeft : GTextAlignmentRight, NULL);
    }
  }

  // the target, cyan and labeled — clamped to the frame edge along the
  // route bearing when the hop reaches past the viewport (a square there,
  // matching the signpost grammar: square = beyond the frame)
  if (tgt != g_walk.cur) {
    int px = SX(tx), py = SY(ty);
    int fx = SX(curx), fy = SY(cury);
    int xmin = area.origin.x + 5, xmax = area.origin.x + area.size.w - 6;
    int ymin = area.origin.y + 5, ymax = area.origin.y + area.size.h - 6;
    bool clamped = false;
    int dx = px - fx, dy = py - fy;
    float t = 1.0f;
    if (dx > 0 && px > xmax) { float u = (xmax - fx) / (float)dx; if (u < t) t = u; }
    if (dx < 0 && px < xmin) { float u = (xmin - fx) / (float)dx; if (u < t) t = u; }
    if (dy > 0 && py > ymax) { float u = (ymax - fy) / (float)dy; if (u < t) t = u; }
    if (dy < 0 && py < ymin) { float u = (ymin - fy) / (float)dy; if (u < t) t = u; }
    if (t < 1.0f) {
      clamped = true;
      px = fx + (int)(dx * t);
      py = fy + (int)(dy * t);
    }
    graphics_context_set_fill_color(ctx, COL_CYAN);
    if (clamped)
      graphics_fill_rect(ctx, GRect(px - 2, py - 2, 5, 5), 0, GCornerNone);
    else
      graphics_fill_circle(ctx, GPoint(px, py), 3);
    char tn[STAR_NAME_MAX];
    star_name(tgt, tn, sizeof tn);
    int lx = px < b.size.w / 2 ? px + 6 : px - 66;
    int ly = py < area.origin.y + 14 ? py + 4 : py - 16;
    graphics_context_set_text_color(ctx, COL_CYAN);
    graphics_draw_text(ctx, tn, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(lx, ly, 62, 16), GTextOverflowModeTrailingEllipsis,
                       px < b.size.w / 2 ? GTextAlignmentLeft : GTextAlignmentRight, NULL);
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

  // --- bottom panel: the minute's neighbor
  int y = b.size.h - bot_h;
  int px_ = round ? 26 : 4;
  int pw = b.size.w - 2 * px_;
  GTextAlignment align = round ? GTextAlignmentCenter : GTextAlignmentLeft;
  graphics_context_set_stroke_color(ctx, COL_FAINT);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(inset, y), GPoint(b.size.w - inset, y));

  char tn[STAR_NAME_MAX];
  star_name(tgt, tn, sizeof tn);
  snprintf(buf, sizeof buf, "> %s", tn);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf,
                     fonts_get_system_font(compact || round ? FONT_KEY_GOTHIC_14_BOLD
                                                            : FONT_KEY_GOTHIC_18_BOLD),
                     GRect(px_, y - 2, pw, 20), GTextOverflowModeTrailingEllipsis, align, NULL);
  int l2 = y + (compact || round ? 13 : 17);
  if (!compact && !round) {
    const char *con = star_con3(tgt);
    if (con) snprintf(buf, sizeof buf, "%s in %s", star_class_desc(tgt), con);
    else snprintf(buf, sizeof buf, "%s", star_class_desc(tgt));
    graphics_context_set_text_color(ctx, COL_DIM);
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(px_, l2, pw, 16), GTextOverflowModeTrailingEllipsis, align, NULL);
    l2 += 16;
  }
  int pw2 = pw - (temp_fresh() && !round ? 30 : 0);
  if (health_mode()) {
    char st[16], km[16];
    fmt_thousands(st, sizeof st, steps_today());
    fmt1(km, sizeof km, walked_m_today() / 1000.0);
    if (round || compact)
      snprintf(buf, sizeof buf, "%s steps  %skm", st, km);
    else
      snprintf(buf, sizeof buf, "%s steps  %skm  %dkcal", st, km, kcal_today());
    graphics_context_set_text_color(ctx, COL_GOOD);
  } else {
    double tu, ts, beta, gamma;
    float d = star_dist_ly(g_walk.cur, tgt);
    hop_profile(d, &tu, &ts, &beta, &gamma);
    fmt1(t1, sizeof t1, d);
    fmt1(t2, sizeof t2, tu);
    char t3[16], cls[8] = "";
    fmt1(t3, sizeof t3, ts);
    if (star_class(tgt) != '?')
      snprintf(cls, sizeof cls, "%c%d  ", star_class(tgt), star_subclass(tgt));
    snprintf(buf, sizeof buf, "%sly  %suni %s  you %s", t1, cls, t2, t3);
    graphics_context_set_text_color(ctx, COL_GOOD);
  }
  graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(px_, l2, pw2, 16), GTextOverflowModeTrailingEllipsis, align, NULL);
  if (temp_fresh() && !round) {
    snprintf(buf, sizeof buf, "%d\xC2\xB0", (int)s_temp);
    graphics_context_set_text_color(ctx, COL_DIM);
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(b.size.w - 46, l2, 42, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
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
// INFO — tap: the current star's card (or the health card), boxed on the map
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
  bool tall = !compact;
  const char *hdr_font = round ? FONT_KEY_GOTHIC_14_BOLD : FONT_KEY_GOTHIC_18_BOLD;
  int hdr_h = round ? 16 : 20;
  char buf[96], t1[20], t2[20];

  if (health_mode()) {
    fmt_thousands(t1, sizeof t1, steps_today());
    snprintf(buf, sizeof buf, "%s steps today", t1);
    line(ctx, &y, buf, COL_GOLD, hdr_font, hdr_h);
    fmt1(t1, sizeof t1, walked_m_today() / 1000.0);
    snprintf(buf, sizeof buf, "%s km   %d kcal", t1, kcal_today());
    line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
    int hr = hr_bpm();
    if (hr > 0) {
      snprintf(buf, sizeof buf, "heart %d bpm", hr);
      line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
    }
    int ss = sleep_secs();
    if (ss > 0) {
      unsigned hh = ((unsigned)ss / 3600u) % 100u, mm = ((unsigned)ss / 60u) % 60u;
      snprintf(buf, sizeof buf, "slept %uh %02um", hh, mm);
      line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
    }
    if (tall) {
      char nm[STAR_NAME_MAX];
      star_name(g_walk.cur, nm, sizeof nm);
      snprintf(buf, sizeof buf, "docked at %s", nm);
      line(ctx, &y, buf, COL_FAINT, FONT_KEY_GOTHIC_14, lh);
    }
    return;
  }

  char nm[STAR_NAME_MAX];
  star_name(g_walk.cur, nm, sizeof nm);
  line(ctx, &y, nm, COL_GOLD, hdr_font, hdr_h);
  const char *con = star_con3(g_walk.cur);
  snprintf(buf, sizeof buf, "%c%d %s%s%s", star_class(g_walk.cur),
           star_subclass(g_walk.cur), star_class_desc(g_walk.cur),
           con ? " in " : "", con ? con : "");
  line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
  fmt1(t1, sizeof t1, star_dist_sol_ly(g_walk.cur));
  if (tall) {
    fmt1(t2, sizeof t2, star_absmag(g_walk.cur));
    snprintf(buf, sizeof buf, "%s ly from Sol  absmag %s", t1, t2);
  } else {
    snprintf(buf, sizeof buf, "%s ly from Sol", t1);
  }
  line(ctx, &y, buf, GColorWhite, FONT_KEY_GOTHIC_14, lh);
  fmt1(t1, sizeof t1, g_walk.path_ly10 / 10.0);
  snprintf(buf, sizeof buf, "today %d hops, %s ly", g_walk.hops, t1);
  line(ctx, &y, buf, COL_DIM, FONT_KEY_GOTHIC_14, lh);
  if (tall) {
    if (g_wrec.far_ever_ly10 > 0) {
      fmt1(t1, sizeof t1, g_wrec.far_ever_ly10 / 10.0);
      snprintf(buf, sizeof buf, "farthest ever %s ly", t1);
      line(ctx, &y, buf, COL_DIM, FONT_KEY_GOTHIC_14, lh);
    }
    if (g_wrec.days > 0)
      snprintf(buf, sizeof buf, "streak %d  day %d", g_walk.streak, g_wrec.days);
    else
      snprintf(buf, sizeof buf, "streak %d", g_walk.streak);
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
    case MODE_ARRIVE:  draw_arrive(ctx, b); break;
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
  else if (e == SCHED_HOPPED) face_show_hop();
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
