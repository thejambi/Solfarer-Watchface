#include "offers.h"
#include "settings.h"
#include "scheduler.h"
#include "rng.h"

// Board order is station-index order: core ports first as laid out, deep
// halo joining once licensed. Stable within a day, so rotation and bells
// agree on what entry i means.

int offers_count(void) {
  int n = 0;
  for (int i = 0; i < g_n_stations; i++) {
    if (i == (int)g.station) continue;
    if (g_stations[i].deep && !g.deep_license) continue;
    n++;
  }
  return n;
}

int offers_dest(int i) {
  int n = 0;
  for (int j = 0; j < g_n_stations; j++) {
    if (j == (int)g.station) continue;
    if (g_stations[j].deep && !g.deep_license) continue;
    if (n++ == i) return j;
  }
  return 0;
}

int offers_window(int hour, int min) {
  return g_cfg.cadence == CAD_HALF ? hour * 2 + (min >= 30 ? 1 : 0) : hour;
}

int offers_sel(int hour, int min) {
  int n = offers_count();
  return n > 0 ? (hour * 60 + min) % n : 0;
}

void offers_get(int i, int window, Contract *out) {
  // Seed a private roll and put the persistent stream back afterwards —
  // dock events still come off the career stream and must not be disturbed.
  uint32_t saved = rng_get_state();
  char key[48];
  snprintf(key, sizeof key, "%s|%lu|%d|%d|%d", g.seed,
           (unsigned long)g_sched.day_key, window, g.station, offers_dest(i));
  rng_seed_str(key);
  world_make_offer(g.station, offers_dest(i), out);
  uint16_t pay_pct = g.dock_event >= 0 ? DOCK_EVENTS[g.dock_event].pay_pct : 100;
  out->pay = (int32_t)((int64_t)out->pay * pay_pct / 100);
  rng_set_state(saved);
}
