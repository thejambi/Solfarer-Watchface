#include "game.h"
#include "rng.h"
#include "fastmath.h"

Game g;
RunResult g_last;

const UpgradeDef UPGRADES[N_UPGRADES] = {
  [UP_TANK]      = { "Fuel Cell Array",  "+3 dv tank",          4, {200, 320, 460, 620} },
  [UP_DRIVE]     = { "Drive Efficiency", "-12% fuel per dv",    3, {240, 400, 560} },
  [UP_DAMPER]    = { "Inertial Dampers", "-15% felt load",      3, {260, 420, 600} },
  [UP_BROKER]    = { "Broker License",   "+8% contract pay",    3, {280, 460, 640} },
  [UP_REJUV]     = { "Rejuv Course",     "+6 yr career",        4, {300, 460, 640, 840} },
  [UP_OVERDRIVE] = { "Redline Coils",    "+thrust, +MAX",      6, {400, 720, 1150, 1700, 2400, 3300} },
  [UP_AUTOPILOT] = { "Docking Assist",   "hot-dock at 0.2c: saves dv each leg", 1, {500} },
};

Records g_rec;

float tank_cap(void)    { return BASE_TANK + g.upgrades[UP_TANK] * 3.0f; }
// Watchface mode: nobody retires — the pilot flies forever, a day per career.
// pilot_age still accrues (the sim reads it) but never gates anything.
float retire_age(void)  { return 1e9f; }
float fuel_factor(void) { return 1.0f - g.upgrades[UP_DRIVE] * 0.12f; }
float load_factor(void) { return 0.85f * (1.0f - g.upgrades[UP_DAMPER] * 0.15f); }  // Courier hull baseline

// Redline Coils raise the drive's thrust ceiling as well as the governor. In
// the web game the coil-gated "warp burn" runs the throttle 9x faster with its
// drive response scaled by coil level, which is what makes redline speeds
// reachable inside the small core cluster. Here runway is (γ−1)/a, so thrust
// is the only lever on how much of a leg a burn eats — without this, coils are
// dead weight anywhere but the deep halo. Roughly x1.4 per level.
float ship_thrust_g(void) {
  static const float THRUST_G[7] = { 7, 10, 14, 20, 28, 39, 55 };
  int lv = g.upgrades[UP_OVERDRIVE];
  return THRUST_G[lv > 6 ? 6 : lv];
}
static double pay_mult(void) { return 1.0 + g.upgrades[UP_BROKER] * 0.08; }
static double rep_mult(void) {
  double r = g.deliveries * REP_PER_DELIVERY;
  return 1.0 + (r > REP_MAX ? REP_MAX : r);
}

double cap_beta(void) {
  double b = 1.0 - (1.0 - C_CAP) * lh_pow10i(-(int)g.upgrades[UP_OVERDRIVE]);
  return b > 1.0 - 5.1e-13 ? 1.0 - 4.9995e-13 : b;
}

int32_t contract_pay(const Contract *c) {
  return (int32_t)(c->pay * pay_mult() * rep_mult() + 0.5);
}

// ---------------------------------------------------------------------------
// Flight planning — burn→cruise→brake with honest relativistic kinematics.
// Rapidity makes it closed-form: ramping 0→φ at proper acceleration a takes
// φ/a ship-years, sinh(φ)/a universe-years, and (cosh(φ)−1)/a ly; the cruise
// covers what's left at βγ = sinh(φ) ly per ship-year. Gentle (low-g) ramps
// cost both clocks — that's the g-rating's price, and where aging comes from.
// ---------------------------------------------------------------------------
typedef struct { double t_uni, t_ship, ramp_ship; } Prof;

static bool eval_profile(double phi, double a, double phi_end, double d, Prof *o) {
  double e = lh_exp(phi);
  double sh = 0.5 * (e - 1.0 / e), ch = 0.5 * (e + 1.0 / e);
  double ee = lh_exp(phi_end);
  double she = 0.5 * (ee - 1.0 / ee), che = 0.5 * (ee + 1.0 / ee);
  double dc = d - (ch - 1.0) / a - (ch - che) / a;   // cruise distance
  if (dc < 0) {
    // hairline negatives are rounding at the geometry bound (φ came from
    // arcosh of this very equation) — that's the triangular profile, dc = 0
    if (dc < -1e-6 * (d + 1.0)) return false;        // genuinely too short
    dc = 0;
  }
  o->ramp_ship = (2.0 * phi - phi_end) / a;
  o->t_ship = o->ramp_ship + dc / sh;
  o->t_uni = (2.0 * sh - she) / a + dc * ch / sh;
  return true;
}

static bool prof_ok(const Contract *c, const Prof *pr) {
  return pr->t_uni <= c->deadline
      && (c->type == 0 || pr->t_ship <= c->max_aging)
      && g.pilot_age + pr->t_ship < retire_age();
}

// The fastest cruise rapidity the leg's geometry allows (cruise length ≥ 0):
// cosh(φ) ≤ (d·a + 1 + cosh(φ_end)) / 2. At the bound the cruise vanishes —
// the honest accelerate-to-midpoint, flip, brake profile.
static double phi_geometry_max(double d, double a, double phi_end) {
  double ee = lh_exp(phi_end);
  double m = (d * a + 1.0 + 0.5 * (ee + 1.0 / ee)) / 2.0;
  if (m < 1.0) m = 1.0;
  return lh_ln(m + lh_sqrt(m * m - 1.0));   // arcosh
}

// The speed ladder: fixed cruise rungs the player cycles on the contract card.
// Rung 0 is AUTO (optimizer), rungs 1..7 fixed speeds, the last is MAX —
// whatever the governor allows, so Redline Coils raise that rung's ceiling.
// Note a leg's own geometry (φ ≤ arcosh(d·a/2 + 1)) usually binds first in the
// core cluster; the coils only pay off out in the deep-space halo.
static const double RUNG_PHI[N_PLAN_RUNGS - 2] = {
  0.549306,   // 0.5c
  1.472219,   // 0.9c
  2.646652,   // 0.99c
  3.800201,   // 0.999c
  4.951719,   // 0.9999c
  6.103034,   // 0.99999c
  7.254329,   // 0.999999c
};

const char *plan_rung_label(int rung) {
  static const char *L[N_PLAN_RUNGS] = { "AUTO", "0.5c", "0.9c", "0.99c",
    "0.999c", "0.9999c", "0.99999c", "0.999999c", "MAX" };
  // the top rung becomes WARP once Redline Coils are fitted — it's the rung
  // that can suddenly reach what nothing else could, so it carries the badge
  if (rung == N_PLAN_RUNGS - 1 && g.upgrades[UP_OVERDRIVE] > 0) return "WARP";
  return L[rung < 0 || rung >= N_PLAN_RUNGS ? 0 : rung];
}

RunPlan game_plan_rung(const Contract *c, int rung) {
  RunPlan p;
  memset(&p, 0, sizeof p);
  // burn/brake acceleration: the cargo's rating through the dampers, but never
  // more than the drive itself can thrust
  double a_g = (double)c->g_limit / load_factor();
  double t_max = ship_thrust_g();
  if (a_g > t_max) a_g = t_max;
  double a = a_g * G_ACCEL;
  double ff = fuel_factor();
  double phi_end = g.upgrades[UP_AUTOPILOT] ? PHI_DOCK : 0.0;
  double phi_hi = (g.fuel / ff + phi_end) * 0.5;      // Δv = (2φ−φ_end)·ff ≤ fuel
  double phi_cap = lh_atanh(cap_beta());
  if (phi_hi > phi_cap) phi_hi = phi_cap;
  if (phi_hi < phi_end + 0.01) { phi_hi = phi_end + 0.01; phi_end = 0.0; }  // dry tank: crawl

  Prof pr;
  double chosen;
  bool any_feasible = false;

  double phi_geom = phi_geometry_max(c->d, a, phi_end);
  if (rung > 0) {
    // a fixed rung: fly the chosen speed, clamped by tank, governor, and the
    // leg's own geometry (short legs can't finish ramping to high φ)
    double target = rung >= N_PLAN_RUNGS - 1 ? phi_cap : RUNG_PHI[rung - 1];
    chosen = target < phi_hi ? target : phi_hi;
    if (chosen > phi_geom) chosen = phi_geom;
    if (chosen < phi_end + 0.01 || !eval_profile(chosen, a, phi_end, c->d, &pr)) {
      chosen = phi_end + 0.01;
      eval_profile(chosen, a, 0.0, c->d, &pr);
      phi_end = 0.0;
    }
    any_feasible = prof_ok(c, &pr);
  } else {
    // AUTO — pass 1: the feasible plan with the least pilot aging (fastest
    // arrival as best-effort fallback); pass 2: thriftiest φ near that optimum
    const int N = 120;
    double phi_top = phi_hi < phi_geom ? phi_hi : phi_geom;
    double best_ship = 1e18, fast_uni = 1e18, fast_phi = -1;
    for (int i = 1; i <= N; i++) {
      double phi = phi_top * i / N;
      if (phi <= phi_end + 1e-4) continue;
      if (!eval_profile(phi, a, phi_end, c->d, &pr)) break;   // higher φ only worse
      if (pr.t_uni < fast_uni) { fast_uni = pr.t_uni; fast_phi = phi; }
      if (prof_ok(c, &pr) && pr.t_ship < best_ship) { best_ship = pr.t_ship; any_feasible = true; }
    }
    chosen = fast_phi > 0 ? fast_phi : phi_top;
    if (any_feasible) {
      for (int i = 1; i <= N; i++) {
        double phi = phi_top * i / N;
        if (phi <= phi_end + 1e-4) continue;
        if (!eval_profile(phi, a, phi_end, c->d, &pr)) break;
        if (prof_ok(c, &pr) && pr.t_ship <= best_ship + THRIFT_SHIP_YR) { chosen = phi; break; }
      }
    }
    if (!eval_profile(chosen, a, phi_end, c->d, &pr)) {   // degenerate short leg
      chosen = phi_end + 0.01;
      eval_profile(chosen, a, 0.0, c->d, &pr);
      phi_end = 0.0;
    }
  }

  double e = lh_exp(chosen);
  p.phi = chosen;
  p.accel = a;
  p.gamma = 0.5 * (e + 1.0 / e);
  p.beta = (e - 1.0 / e) / (e + 1.0 / e);
  p.dv = (float)((2.0 * chosen - phi_end) * ff);
  if (p.dv > g.fuel) p.dv = g.fuel;
  p.t_uni = (float)pr.t_uni;
  p.t_ship = (float)pr.t_ship;
  p.ramp_ship = (float)pr.ramp_ship;
  p.deadline_ok = pr.t_uni <= c->deadline;
  p.aging_ok = c->type == 0 || pr.t_ship <= c->max_aging;
  p.retire_ok = g.pilot_age + pr.t_ship < retire_age();
  p.feasible = any_feasible;
  return p;
}

RunPlan game_plan(const Contract *c) {
  int r = g.plan_rung < N_PLAN_RUNGS ? g.plan_rung : 0;
  return game_plan_rung(c, r);
}

// One line of the story the clocks write: how long this dock has waited.
static void make_vignette(char *out, size_t cap, int st_idx, float now) {
  Station *st = &g_stations[st_idx];
  char nm[NAME_LEN];
  station_short_name(st_idx, nm, sizeof nm);
  if (st->last_visit < 0) {
    snprintf(out, cap, "First call at %s - the Guild ledger opens a page for you.", nm);
    return;
  }
  int gap = (int)(now - st->last_visit);
  if (gap < 60)
    snprintf(out, cap, "%d yr since you left - the dockmaster still remembers your name.", gap);
  else if (gap < 300)
    snprintf(out, cap, "%d yr gone. Everyone you dealt with here has retired.", gap);
  else if (gap < 1000)
    snprintf(out, cap, "You left a town and docked at a city - %d yr of history passed in cruise.", gap);
  else
    snprintf(out, cap, "%d yr since you stood here. Your landing is in their founding mural.", gap);
}

void game_resolve(const Contract *c) {
  RunPlan p = game_plan(c);
  memset(&g_last, 0, sizeof g_last);
  g_last.type = c->type;
  g_last.g_limit = c->g_limit;
  g_last.t_uni = p.t_uni;
  g_last.t_ship = p.t_ship;
  g_last.beta = p.beta;
  g_last.gamma = p.gamma;
  strncpy(g_last.to_name, g_stations[c->to].name, NAME_LEN - 1);
  strncpy(g_last.what, c->what, WHAT_LEN - 1);

  int32_t pay = contract_pay(c);
  bool ok = true;
  if (!p.deadline_ok) { ok = false; g_last.late = true; pay = (int32_t)(pay * 0.25 + 0.5); }
  if (c->type == 1 && !p.aging_ok) { ok = false; g_last.aged_out = true; pay = (int32_t)(pay * 0.2 + 0.5); }
  g_last.ok = ok;
  g_last.pay = pay;

  g.credits += pay;
  g.earned += pay;
  if (ok) g.deliveries++; else g.failures++;
  g.fuel -= p.dv;
  if (g.fuel < 0) g.fuel = 0;
  g.pilot_age += p.t_ship;
  g.uni_time += p.t_uni;
  if (p.gamma > g.max_gamma) g.max_gamma = (float)p.gamma;

  if (!g.deep_license && ok && p.gamma >= DEEP_GAMMA) {
    g.deep_license = true;
    g_last.licensed = true;
  }

  if (rand() % 4 == 0) make_vignette(g_last.vignette, sizeof g_last.vignette, c->to, g.uni_time);
  g_stations[c->to].last_visit = g.uni_time;
  g.station = c->to;

  // the next dock's economy comes off the seeded stream; the offer board is
  // stateless (see offers.c) and needs no roll here
  g.dock_event = world_roll_dock_event();
  g.rng_state = rng_get_state();
  game_save();
}

// ---------------------------------------------------------------------------
// Fuel & shop
// ---------------------------------------------------------------------------
static uint16_t event_pct(int which) {
  if (g.dock_event < 0) return 100;
  const DockEvent *e = &DOCK_EVENTS[g.dock_event];
  return which == 0 ? e->fuel_pct : which == 2 ? e->shop_pct : e->pay_pct;
}

int32_t fuel_cost(float qty) {
  if (qty <= 0) return 0;
  double disc = qty * FUEL_BULK_DISC;
  if (disc > FUEL_MAX_DISC) disc = FUEL_MAX_DISC;
  return (int32_t)(qty * FUEL_PRICE * (1.0 - disc) * event_pct(0) / 100.0 + 0.5);
}

void buy_fuel(float qty) {
  float missing = tank_cap() - g.fuel;
  if (qty > missing) qty = missing;
  while (qty > 0.05f && fuel_cost(qty) > g.credits) qty -= 0.1f;  // buy what you can afford
  if (qty <= 0.05f) return;
  g.credits -= fuel_cost(qty);
  g.fuel += qty;
  if (g.fuel > tank_cap()) g.fuel = tank_cap();
  game_save();
}

int32_t upgrade_cost(int id) {
  int lvl = g.upgrades[id];
  if (lvl >= UPGRADES[id].n_levels) return -1;
  return (int32_t)((int64_t)UPGRADES[id].costs[lvl] * event_pct(2) / 100);
}

bool buy_upgrade(int id) {
  int32_t cost = upgrade_cost(id);
  if (cost < 0 || cost > g.credits) return false;
  g.credits -= cost;
  g.upgrades[id]++;
  game_save();
  return true;
}

// ---------------------------------------------------------------------------
// Recovery tow — the dockside bailout for a pilot stranded broke with a dry
// tank: 40% of your credits (min $150) and 4 years of your life for half a tank.
// ---------------------------------------------------------------------------
// Offered only when market fuel is out of reach: low tank AND too broke to buy
// your way back to half. Otherwise the pump is always the better deal — the
// tow's real price is the 4 years of your life.
bool tow_available(void) {
  float half = tank_cap() * 0.5f;
  return g.fuel < half && fuel_cost(half - g.fuel) > g.credits;
}

int32_t tow_cost(void) {
  int32_t c = (int32_t)(g.credits * 0.4);
  return c < 150 ? 150 : c;
}

void guild_tow(void) {
  if (!tow_available()) return;
  g.credits -= tow_cost();
  if (g.credits < 0) g.credits = 0;
  g.pilot_age += 4;
  g.uni_time += 4;
  if (g.fuel < tank_cap() * 0.5f) g.fuel = tank_cap() * 0.5f;
  game_save();
}

const char *rank_for(int32_t bal) {
  if (bal >= 128000) return "The Ageless";
  if (bal >= 64000)  return "Deep Space Magnate";
  if (bal >= 32000)  return "Redline Royalty";
  if (bal >= 16000)  return "Lightspeed Legend";
  if (bal >= 10000)  return "Void Baron";
  if (bal >= 6000)   return "Master Courier";
  if (bal >= 3000)   return "Journeyman Courier";
  if (bal >= 1000)   return "Drifter";
  return "Deadhead";
}

// ---------------------------------------------------------------------------
// Careers & persistence
// ---------------------------------------------------------------------------
#define KEY_VERSION 0
#define KEY_GAME 1
#define KEY_OFFERS 2
#define KEY_VISITS 3
#define KEY_RECORDS 4
#define SAVE_VERSION 1

void daily_seed(char *out, size_t cap) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  snprintf(out, cap, "lh%02d%02d%02d", t->tm_year % 100, t->tm_mon + 1, t->tm_mday);
}

void game_new(const char *seed_or_null) {
  memset(&g, 0, sizeof g);
  if (seed_or_null && seed_or_null[0]) {
    strncpy(g.seed, seed_or_null, sizeof g.seed - 1);
  } else {
    static const char *b36 = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < 6; i++) g.seed[i] = b36[rand() % 36];
    g.seed[6] = 0;
  }
  g.credits = 450;            // Courier-class opening balance
  g.fuel = BASE_TANK;
  g.pilot_age = START_AGE;
  g.max_gamma = 1;
  g.dock_event = -1;
  world_build(g.seed);
  g.dock_event = world_roll_dock_event();
  g.rng_state = rng_get_state();
  game_save();
}

// Fold the finished day's career into the all-time records. The scheduler
// calls this at day rollover — the watchface's replacement for retirement.
void game_fold_day(void) {
  g_rec.careers++;
  if (g.credits > g_rec.best_balance) g_rec.best_balance = g.credits;
  if (g.deliveries > g_rec.best_deliveries) g_rec.best_deliveries = g.deliveries;
  if (g.max_gamma > g_rec.best_gamma) g_rec.best_gamma = g.max_gamma;
  game_save();
}

bool game_init(void) {
  srand(time(NULL));
  memset(&g, 0, sizeof g);   // fields appended since a save was written stay 0
  memset(&g_rec, 0, sizeof g_rec);
  if (persist_exists(KEY_RECORDS)) persist_read_data(KEY_RECORDS, &g_rec, sizeof g_rec);
  if (persist_exists(KEY_OFFERS)) persist_delete(KEY_OFFERS);   // pre-board saves
  if (persist_exists(KEY_VERSION) && persist_read_int(KEY_VERSION) == SAVE_VERSION &&
      persist_exists(KEY_GAME)) {
    persist_read_data(KEY_GAME, &g, sizeof g);
    world_build(g.seed);
    rng_set_state(g.rng_state);   // resume the event stream where it left off
    if (persist_exists(KEY_VISITS)) {
      float visits[MAX_STATIONS];
      persist_read_data(KEY_VISITS, visits, sizeof visits);
      for (int i = 0; i < g_n_stations; i++) g_stations[i].last_visit = visits[i];
    }
    return true;
  }
  return false;   // no save — the scheduler starts the day's career
}

void game_save(void) {
  persist_write_int(KEY_VERSION, SAVE_VERSION);
  persist_write_data(KEY_GAME, &g, sizeof g);
  float visits[MAX_STATIONS];
  for (int i = 0; i < MAX_STATIONS; i++)
    visits[i] = i < g_n_stations ? g_stations[i].last_visit : -1;
  persist_write_data(KEY_VISITS, visits, sizeof visits);
  persist_write_data(KEY_RECORDS, &g_rec, sizeof g_rec);
}
