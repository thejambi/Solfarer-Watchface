#include "scheduler.h"
#include "settings.h"
#include "autopilot.h"
#include "ui.h"

SchedState g_sched;

#define KEY_SCHED 10

static bool s_pumping;          // silent catch-up chain running
static bool s_summary;          // boot crossed a day: summary card owed
static AppTimer *s_pump;

// ---------------------------------------------------------------------------
// Wall-clock helpers
// ---------------------------------------------------------------------------
static uint32_t day_of(const struct tm *t) {
  return (uint32_t)((t->tm_year + 1900) * 10000 + (t->tm_mon + 1) * 100 + t->tm_mday);
}

// DEV_FAST_BELLS: a bell every minute, for watching the loop in the emulator.
//#define DEV_FAST_BELLS

static int slot_of(const struct tm *t) {
#ifdef DEV_FAST_BELLS
  return t->tm_hour * 60 + t->tm_min;
#endif
  if (g_sched.cadence == CAD_HALF) return t->tm_hour * 2 + (t->tm_min >= 30 ? 1 : 0);
  return t->tm_hour;
}

static bool is_bell_min(const struct tm *t) {
#ifdef DEV_FAST_BELLS
  return true;
#endif
  if (g_sched.cadence == CAD_HALF) return t->tm_min == 0 || t->tm_min == 30;
  return t->tm_min == 0;
}

// Hand-rolled so streaks never depend on the SDK having a full mktime.
static uint32_t next_day_key(uint32_t k) {
  int y = k / 10000, m = (k / 100) % 100, d = k % 100;
  static const uint8_t dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int md = dim[m - 1];
  if (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) md = 29;
  if (++d > md) { d = 1; if (++m > 12) { m = 1; y++; } }
  return (uint32_t)(y * 10000 + m * 100 + d);
}

static void expected_seed(char *out, size_t cap) {
  if (g_cfg.seed_mode == SEED_FIXED && g_cfg.fixed_seed[0]) {
    strncpy(out, g_cfg.fixed_seed, cap - 1);
    out[cap - 1] = 0;
  } else {
    daily_seed(out, cap);
  }
}

// ---------------------------------------------------------------------------
// Running bells
// ---------------------------------------------------------------------------
// The autopilot derives selection and offer window from the bell's own wall
// time, so "whatever was on screen when the bell struck" replays exactly.
static void run_slot(int slot) {
  int hour, min;
  if (g_sched.cadence == CAD_HALF) { hour = slot / 2; min = (slot % 2) * 30; }
  else { hour = slot; min = 0; }
  auto_run_bell(g_cfg.dispatch, g_cfg.upgrades, hour, min);
}

static void start_career(uint32_t today) {
  char seed[10];
  expected_seed(seed, sizeof seed);
  game_new(seed);
  g_sched.day_key = today;
}

static void rollover(uint32_t today) {
  game_fold_day();
  g_sched.streak = next_day_key(g_sched.day_key) == today ? g_sched.streak + 1 : 1;
  g_sched.prev_day_key = g_sched.day_key;
  g_sched.prev_balance = g.credits;
  g_sched.prev_deliveries = g.deliveries;
  g_sched.prev_gamma = g.max_gamma;
  start_career(today);
  g_sched.last_slot = -1;         // catch up today's bells from midnight
}

// ---------------------------------------------------------------------------
// Silent catch-up — one missed bell per timer step so the face never blocks;
// the map redraws between steps, so you can watch the backlog fly.
// ---------------------------------------------------------------------------
static void pump_step(void *ctx) {
  s_pump = NULL;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int cur = slot_of(t);
  if (g_sched.last_slot >= cur) {
    s_pumping = false;
    scheduler_save();
    face_poke();
    return;
  }
  run_slot(g_sched.last_slot + 1);
  g_sched.last_slot++;
  face_poke();
  s_pump = app_timer_register(20, pump_step, NULL);
}

static void pump_start(void) {
  if (s_pumping) return;
  s_pumping = true;
  s_pump = app_timer_register(10, pump_step, NULL);
}

bool scheduler_busy(void) { return s_pumping; }

bool scheduler_take_summary(void) {
  bool b = s_summary;
  s_summary = false;
  return b;
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------
void scheduler_boot(bool had_save) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  uint32_t today = day_of(t);

  memset(&g_sched, 0, sizeof g_sched);
  g_sched.version = SCHED_VERSION;
  g_sched.last_slot = -1;
  g_sched.cadence = g_cfg.cadence;
  g_sched.day_key = today;
  // Older (shorter) blobs read over the defaults; appended fields keep them.
  bool had_sched = false;
  int n = persist_exists(KEY_SCHED) ? persist_get_size(KEY_SCHED) : 0;
  if (n > 0 && n <= (int)sizeof g_sched) {
    SchedState tmp = g_sched;
    persist_read_data(KEY_SCHED, &tmp, n);
    if (tmp.version == SCHED_VERSION) { g_sched = tmp; had_sched = true; }
  }
  if (!had_sched && had_save) {
    // schedule state lost under a live career (e.g. layout change): pick up
    // from this moment rather than re-running the whole day onto it
    g_sched.last_slot = slot_of(t);
  }

  if (g_sched.cadence != g_cfg.cadence) {   // changed on the phone while away
    g_sched.cadence = g_cfg.cadence;
    g_sched.last_slot = slot_of(t);
  }

  char seed[10];
  expected_seed(seed, sizeof seed);
  if (!had_save) {
    // fresh install: today's career, catch up from midnight so the day's
    // state matches the time of day
    start_career(today);
    g_sched.last_slot = -1;
    g_sched.streak = 1;
  } else if (strcmp(g.seed, seed) != 0) {
    if (g_cfg.seed_mode == SEED_DAILY && g_sched.day_key != today) {
      rollover(today);
      s_summary = true;          // first glance after midnight gets the card
    } else {
      // seed source changed while away — fresh start, no fold, no backlog
      start_career(today);
      g_sched.last_slot = slot_of(t);
    }
  } else if (g_sched.day_key != today) {
    // fixed-seed career crossing days: the pilot slept; skip the dark hours
    g_sched.day_key = today;
    g_sched.last_slot = slot_of(t);
  }

#ifdef DEV_FAST_BELLS
  g_sched.last_slot = slot_of(t);   // dev: skip the backlog, live bells only
#endif
  scheduler_save();
  pump_start();
}

SchedEvent scheduler_tick(struct tm *t) {
  if (s_pumping) return SCHED_IDLE;
  uint32_t today = day_of(t);
  if (today != g_sched.day_key) {
    if (g_cfg.seed_mode == SEED_DAILY || !g_cfg.fixed_seed[0]) {
      rollover(today);
      pump_start();              // runs the 00:00 bell behind the summary card
      return SCHED_NEWDAY;
    }
    // fixed seed, worn across midnight: same career, slots restart with the day
    g_sched.day_key = today;
    g_sched.last_slot = -1;
    scheduler_save();
  }
  int cur = slot_of(t);
  if (cur < g_sched.last_slot) {           // clock set backwards: never re-run
    g_sched.last_slot = cur;
    scheduler_save();
    return SCHED_IDLE;
  }
  if (cur == g_sched.last_slot) return SCHED_IDLE;
  if (cur - g_sched.last_slot == 1 && is_bell_min(t)) {
    run_slot(cur);                         // the bell, live on the wrist
    g_sched.last_slot = cur;
    scheduler_save();
    return SCHED_RAN;
  }
  pump_start();                            // several missed: quiet backlog
  return SCHED_IDLE;
}

void scheduler_reseed(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  start_career(day_of(t));
  g_sched.last_slot = slot_of(t);          // fresh from this moment
  scheduler_save();
  face_poke();
}

void scheduler_apply_cadence(void) {
  if (g_sched.cadence == g_cfg.cadence) return;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  g_sched.cadence = g_cfg.cadence;
  g_sched.last_slot = slot_of(t);          // count in the new units from now
  scheduler_save();
}

void scheduler_save(void) {
  g_sched.version = SCHED_VERSION;
  persist_write_data(KEY_SCHED, &g_sched, sizeof g_sched);
}
