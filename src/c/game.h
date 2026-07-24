#pragma once
#include <pebble.h>
#include "world.h"

#define START_AGE 22.0f
#define RETIRE_AGE 82.0f
#define BASE_TANK 14.0f
#define FUEL_PRICE 20.0
#define FUEL_BULK_DISC 0.02
#define FUEL_MAX_DISC 0.35
#define C_CAP 0.9999995
// Deep Space License threshold — γ2000, the same number the web game calls
// redline (its HUD goes red and buzzes there). A leg's peak γ is capped by
// honest ramp geometry at ~d·a/2, so this is a genuine multi-part gate rather
// than a grind. Measured over 400 seeds, % of careers where it's reachable
// at all: coils L0 0%, L1 12%, L2 93%, L3 100% — so it *requires* Redline
// Coils, which is what the web intends by "redline territory". Also needs a
// ~200 ly leg, rugged freight to use the thrust, and a tank or drive upgrade
// to afford the Δv 16.6. (γ3000 would strand 12% of maps — see the γ25,000
// mistake this replaced.)
#define DEEP_GAMMA 2000.0
#define REP_PER_DELIVERY 0.03
#define REP_MAX 0.75
#define G_ACCEL 1.032           // c/yr per felt g — honest 1g = 1.032 ly/yr^2
#define PHI_DOCK 0.2027         // rapidity of 0.2c — hot-dock arrival speed
#define THRIFT_SHIP_YR 0.05     // don't burn extra dv to save under ~3 weeks
#define SHIP_MAX_G 7.0          // Courier drive's base proper thrust, in g —
                                // rugged cargo can take more than the drive gives.
                                // Redline Coils raise it; see ship_thrust_g().
#define N_PLAN_RUNGS 9          // AUTO, 7 fixed cruise speeds, MAX (governor)

enum { UP_TANK, UP_DRIVE, UP_DAMPER, UP_BROKER, UP_REJUV, UP_OVERDRIVE, UP_AUTOPILOT, N_UPGRADES };

typedef struct {
  const char *name, *desc;
  uint8_t n_levels;
  uint16_t costs[6];
} UpgradeDef;
extern const UpgradeDef UPGRADES[N_UPGRADES];

typedef struct {
  char seed[10];
  uint32_t rng_state;           // world RNG mid-career (contracts keep rolling)
  int32_t credits, earned;
  float fuel;                   // Δv remaining, in rapidity units
  float pilot_age;              // ship-time years
  float uni_time;               // universe-years elapsed this career
  uint8_t station;
  uint8_t upgrades[N_UPGRADES];
  uint16_t deliveries, failures;
  bool deep_license;
  int8_t dock_event;            // index into DOCK_EVENTS, -1 = none
  float max_gamma;
  uint8_t plan_rung;            // sticky speed-ladder pick (0 = AUTO); appended
                                // last so old saves load with it zeroed
} Game;

extern Game g;

// The auto-run flight plan: a burn→cruise→brake profile at the hardest proper
// acceleration the cargo's g-rating (through your dampers) tolerates. The
// optimizer picks the cruise rapidity that minimizes pilot aging within the
// deadline, the passenger cap, and the tank — thriftily: it won't spend Δv on
// savings under THRIFT_SHIP_YR.
typedef struct {
  double phi, beta, gamma;      // cruise rapidity and its β/γ
  double accel;                 // burn/brake proper acceleration, c/yr
  float dv;                     // fuel this run actually consumes
  float t_uni, t_ship;          // universe-years, ship-years (ramps included)
  float ramp_ship;              // ship-years spent ramping (the g-rating's price)
  bool deadline_ok, aging_ok, retire_ok, feasible;
} RunPlan;

typedef struct {
  bool ok, late, aged_out, retired, licensed;
  bool rec_balance, rec_deliveries, rec_gamma;   // records broken at retirement
  uint8_t type, g_limit;
  int32_t pay;
  float t_uni, t_ship;
  double gamma, beta;
  char to_name[NAME_LEN];
  char what[WHAT_LEN];
  char vignette[128];
} RunResult;
extern RunResult g_last;

// All-time bests, persisted across careers.
typedef struct {
  int32_t best_balance;
  uint16_t best_deliveries, careers;
  float best_gamma;
} Records;
extern Records g_rec;

// derived dials
float tank_cap(void);
float retire_age(void);                    // watchface mode: effectively infinite
float fuel_factor(void);
float load_factor(void);                   // felt-load multiplier (dampers)
float ship_thrust_g(void);                 // drive's max proper thrust (Redline Coils)
double cap_beta(void);
int32_t contract_pay(const Contract *c);   // base pay × broker × reputation

RunPlan game_plan(const Contract *c);              // plan at the sticky rung
RunPlan game_plan_rung(const Contract *c, int rung);
const char *plan_rung_label(int rung);             // "AUTO".."0.999999c","MAX"
void game_resolve(const Contract *c);      // apply plan, move dock, reroll offers

// fuel & shop
int32_t fuel_cost(float qty);              // bulk discount + dock event pricing
void buy_fuel(float qty);                  // clamped to tank space & credits
int32_t upgrade_cost(int id);              // next level, event-adjusted; -1 if maxed
bool buy_upgrade(int id);

// stranded at a dock: 40% of your credits and 4 years of your life for half a tank
bool tow_available(void);
int32_t tow_cost(void);
void guild_tow(void);

const char *rank_for(int32_t balance);
void daily_seed(char *out, size_t cap);    // today's shared map, "lhYYMMDD"

void game_new(const char *seed_or_null);
bool game_init(void);                      // load save; false = fresh install
void game_save(void);
void game_fold_day(void);                  // day rollover: fold career into records
