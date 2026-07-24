#include "walk.h"
#include "settings.h"
#include "rng.h"
#include "fastmath.h"

WalkState g_walk;
WalkRecords g_wrec;
HopResult g_hop;

#define KEY_WALK 10
#define KEY_WREC 11
#define G_ACCEL 1.032                // 1 g in c/yr — the comfortable cruise

// ---------------------------------------------------------------------------
// Geometry (0.01 ly integer units; dx up to 20000 → squares fit int32)
// ---------------------------------------------------------------------------
static int32_t dist2_01(int a, int b) {
  int ax, ay, az, bx, by, bz;
  star_pos01(a, &ax, &ay, &az);
  star_pos01(b, &bx, &by, &bz);
  int32_t dx = ax - bx, dy = ay - by, dz = az - bz;
  return dx * dx + dy * dy + dz * dz;
}

float star_dist_ly(int a, int b) {
  return (float)(lh_sqrt((double)dist2_01(a, b)) / 100.0);
}

float star_dist_sol_ly(int idx) { return star_dist_ly(idx, STAR_SOL); }

// ---------------------------------------------------------------------------
// The neighbor board — Lighthaul's windowed offer board, reborn: a seeded,
// outward-biased sample of up to BOARD_MAX neighbors within hop range.
// Cached per (day, window, star); the minute hand walks it, the bell takes
// whatever the minute hand shows.
// ---------------------------------------------------------------------------
static int s_board[BOARD_MAX];
static int s_board_n = -1;
static uint32_t s_board_day;
static int s_board_window = -1, s_board_star = -2;

static void board_ensure(int window) {
  if (s_board_n >= 0 && s_board_window == window &&
      s_board_star == g_walk.cur && s_board_day == g_walk.day_key)
    return;

  int cand[64], n_cand = 0;
  int32_t cur_sol2 = dist2_01(g_walk.cur, STAR_SOL);
  for (int i = 0; i < CAT_COUNT && n_cand < 64; i++) {
    if (i == g_walk.cur) continue;
    if (dist2_01(g_walk.cur, i) <= (int32_t)HOP_01 * HOP_01)
      cand[n_cand++] = i;
  }
  // Sol itself is always a candidate when in range (cur != Sol implied)
  if (g_walk.cur != STAR_SOL && n_cand < 64 &&
      cur_sol2 <= (int32_t)HOP_01 * HOP_01)
    cand[n_cand++] = STAR_SOL;

  uint32_t saved = rng_get_state();
  char key[40];
  snprintf(key, sizeof key, "sf|%lu|%d|%d",
           (unsigned long)g_walk.day_key, window, g_walk.cur);
  rng_seed_str(key);

  s_board_n = 0;
  // weighted sample without replacement: outward neighbors count double
  while (s_board_n < BOARD_MAX && n_cand > 0) {
    int total = 0, w[64];
    for (int i = 0; i < n_cand; i++) {
      w[i] = dist2_01(cand[i], STAR_SOL) > cur_sol2 ? 2 : 1;
      total += w[i];
    }
    int pick = rng_pick(total), j = 0;
    while (pick >= w[j]) pick -= w[j++];
    s_board[s_board_n++] = cand[j];
    cand[j] = cand[--n_cand];
  }
  rng_set_state(saved);

  s_board_window = window;
  s_board_star = g_walk.cur;
  s_board_day = g_walk.day_key;
}

int board_count(int window) {
  board_ensure(window);
  return s_board_n;
}

int board_star(int window, int k) {
  board_ensure(window);
  return s_board[k];
}

int board_sel(int window, int hour, int min) {
  board_ensure(window);
  return s_board_n > 0 ? (hour * 60 + min) % s_board_n : 0;
}

// ---------------------------------------------------------------------------
// Relativity — burn to midpoint at 1 g, flip, brake. In rapidity it's closed
// form: peak φ = arcosh(a·d/2 + 1); ship time 2φ/a; universe time 2·sinh(φ)/a.
// Earth→Proxima at 1 g: 3.5 ship years, 5.9 universe years. Real numbers.
// ---------------------------------------------------------------------------
void hop_profile(double d_ly, double *t_uni, double *t_ship,
                 double *beta_peak, double *gamma_peak) {
  double a = G_ACCEL;
  double m = a * d_ly / 2.0 + 1.0;
  double phi = lh_ln(m + lh_sqrt(m * m - 1.0));       // arcosh
  double e = lh_exp(phi);
  double sh = 0.5 * (e - 1.0 / e), ch = 0.5 * (e + 1.0 / e);
  *t_ship = 2.0 * phi / a;
  *t_uni = 2.0 * sh / a;
  *beta_peak = sh / ch;
  *gamma_peak = ch;
}

// ---------------------------------------------------------------------------
// Hopping
// ---------------------------------------------------------------------------
static void slot_wall_time(int slot, int *hour, int *min) {
  int h;
  if (g_walk.cadence == CAD_HALF) { h = slot / 2; *min = (slot % 2) * 30; }
  else { h = slot; *min = 0; }
  *hour = (h + g_cfg.start_hour) % 24;    // back to wall-clock hours
}

void walk_hop_slot(int slot) {
  int hour, min;
  slot_wall_time(slot, &hour, &min);
  int window = slot;                       // one board per bell slot
  int n = board_count(window);
  if (n < 1) return;                       // can't happen: bubble is connected
  int to = board_star(window, board_sel(window, hour, min));

  double tu, ts, beta, gamma;
  float d = star_dist_ly(g_walk.cur, to);
  hop_profile(d, &tu, &ts, &beta, &gamma);
  g_hop.from = g_walk.cur;
  g_hop.to = to;
  g_hop.d_ly = d;
  g_hop.t_uni = (float)tu;
  g_hop.t_ship = (float)ts;
  g_hop.beta = beta;
  g_hop.gamma = gamma;

  g_walk.cur = to;
  g_walk.hops++;
  g_walk.path_ly10 += (uint16_t)(d * 10.0f + 0.5f);
  float out = star_dist_sol_ly(to);
  uint16_t out10 = (uint16_t)(out * 10.0f + 0.5f);
  if (out10 > g_walk.far_ly10) g_walk.far_ly10 = out10;
  s_board_n = -1;                          // new star, new boards
  walk_save();
}

void walk_new_day(uint32_t day_key, bool consecutive) {
  if (g_walk.day_key) {                    // fold the finished day
    g_wrec.days++;
    if (g_walk.hops > g_wrec.best_hops) g_wrec.best_hops = g_walk.hops;
    if (g_walk.far_ly10 > g_wrec.far_ever_ly10)
      g_wrec.far_ever_ly10 = g_walk.far_ly10;
  }
  g_walk.streak = consecutive ? g_walk.streak + 1 : 1;
  g_walk.prev_end = g_walk.cur;
  g_walk.prev_hops = g_walk.hops;
  g_walk.prev_path_ly10 = g_walk.path_ly10;
  g_walk.prev_far_ly10 = g_walk.far_ly10;
  g_walk.prev_day_key = g_walk.day_key;
  g_walk.cur = STAR_SOL;
  g_walk.hops = 0;
  g_walk.path_ly10 = 0;
  g_walk.far_ly10 = 0;
  g_walk.day_key = day_key;
  g_walk.last_slot = -1;
  s_board_n = -1;
  if (g_wrec.longest_streak < g_walk.streak)
    g_wrec.longest_streak = g_walk.streak;
  persist_write_data(KEY_WREC, &g_wrec, sizeof g_wrec);
  walk_save();
}

void walk_save(void) {
  g_walk.version = WALK_VERSION;
  g_walk.cadence = g_cfg.cadence;
  persist_write_data(KEY_WALK, &g_walk, sizeof g_walk);
}

bool walk_load(void) {
  memset(&g_walk, 0, sizeof g_walk);
  memset(&g_wrec, 0, sizeof g_wrec);
  g_walk.version = WALK_VERSION;
  g_walk.cur = STAR_SOL;
  g_walk.last_slot = -1;
  if (persist_exists(KEY_WREC) &&
      persist_get_size(KEY_WREC) == (int)sizeof g_wrec)
    persist_read_data(KEY_WREC, &g_wrec, sizeof g_wrec);
  int n = persist_exists(KEY_WALK) ? persist_get_size(KEY_WALK) : 0;
  if (n > 0 && n <= (int)sizeof g_walk) {
    WalkState tmp = g_walk;
    persist_read_data(KEY_WALK, &tmp, n);
    if (tmp.version == WALK_VERSION && tmp.cur >= -1 && tmp.cur < CAT_COUNT) {
      g_walk = tmp;
      return true;
    }
  }
  return false;
}
