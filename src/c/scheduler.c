#include "scheduler.h"
#include "settings.h"
#include "walk.h"
#include "ui.h"

static bool s_summary;               // boot crossed a day: summary card owed

// ---------------------------------------------------------------------------
// Wander time: shift the clock back by the start hour, and the wander day /
// slot fall out of ordinary calendar math.
// ---------------------------------------------------------------------------
static void wander_now(uint32_t *day, int *slot, int *wall_min) {
  time_t t = time(NULL) - (time_t)g_cfg.start_hour * 3600;
  struct tm *w = localtime(&t);
  *day = (uint32_t)((w->tm_year + 1900) * 10000 + (w->tm_mon + 1) * 100 +
                    w->tm_mday);
  if (g_cfg.cadence == CAD_HALF)
    *slot = w->tm_hour * 2 + (w->tm_min >= 30 ? 1 : 0);
  else
    *slot = w->tm_hour;
  *wall_min = w->tm_min;             // minutes are unshifted by whole hours
}

static uint32_t next_day_key(uint32_t k) {
  int y = k / 10000, m = (k / 100) % 100, d = k % 100;
  static const uint8_t dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int md = dim[m - 1];
  if (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) md = 29;
  if (++d > md) { d = 1; if (++m > 12) { m = 1; y++; } }
  return (uint32_t)(y * 10000 + m * 100 + d);
}

// A replayed hop is a board build and a pick — microseconds. No pump needed.
static void replay_to(int cur_slot) {
  while (g_walk.last_slot < cur_slot) {
    walk_hop_slot(++g_walk.last_slot);
  }
  walk_save();
}

void scheduler_boot(void) {
  uint32_t day; int slot, min;
  wander_now(&day, &slot, &min);
  bool had = walk_load();
  APP_LOG(APP_LOG_LEVEL_INFO, "boot: had=%d day=%lu key=%lu slot=%d last=%d cur=%d cad=%d/%d",
          (int)had, (unsigned long)day, (unsigned long)g_walk.day_key, slot,
          g_walk.last_slot, g_walk.cur, g_walk.cadence, g_cfg.cadence);
  if (g_walk.cadence != g_cfg.cadence) {   // counted in old units: skip to now
    g_walk.last_slot = slot;
    g_walk.cadence = g_cfg.cadence;
  }
  if (!had) {
    walk_new_day(day, false);              // first flight: fresh at Sol
  } else if (g_walk.day_key != day) {
    bool consec = next_day_key(g_walk.day_key) == day;
    walk_new_day(day, consec);
    s_summary = true;                      // first glance shows the card
  }
  replay_to(slot);                         // the day so far, in an instant
  APP_LOG(APP_LOG_LEVEL_INFO, "boot done: last=%d cur=%d hops=%d",
          g_walk.last_slot, g_walk.cur, g_walk.hops);
}

SchedEvent scheduler_tick(struct tm *t) {
  (void)t;
  uint32_t day; int slot, min;
  wander_now(&day, &slot, &min);
  if (day != g_walk.day_key) {
    bool consec = next_day_key(g_walk.day_key) == day;
    walk_new_day(day, consec);
    replay_to(slot);                       // usually just slot 0
    return SCHED_NEWDAY;
  }
  if (slot < g_walk.last_slot) {
    // Clock went backwards (DST fall-back, or transient emulator skew at
    // boot). Flown slots STAY flown — clamping last_slot down re-armed them
    // and every skew event minted phantom hops. The wander simply pauses
    // until the clock catches back up to the high-water mark.
    return SCHED_IDLE;
  }
  if (slot == g_walk.last_slot) return SCHED_IDLE;
  bool bell_now = (g_cfg.cadence == CAD_HALF) ? (min == 0 || min == 30)
                                              : (min == 0);
  if (slot - g_walk.last_slot == 1 && bell_now) {
    walk_hop_slot(++g_walk.last_slot);     // the bell, live on the wrist
    walk_save();
    return SCHED_HOPPED;
  }
  replay_to(slot);                         // missed a few: catch up silently
  return SCHED_IDLE;
}

bool scheduler_take_summary(void) {
  bool b = s_summary;
  s_summary = false;
  return b;
}

void scheduler_resync(void) {
  uint32_t day; int slot, min;
  wander_now(&day, &slot, &min);
  if (g_walk.day_key != day) {             // start hour moved across midnight
    bool consec = next_day_key(g_walk.day_key) == day;
    walk_new_day(day, consec);
  }
  g_walk.last_slot = slot;                 // no burst on a settings change
  g_walk.cadence = g_cfg.cadence;
  walk_save();
  face_poke();
}
