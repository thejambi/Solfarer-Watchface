#pragma once
#include <pebble.h>
#include "stars.h"

// The daily wander: start at Sol at the configured hour, hop each bell to a
// neighbor within HOP range. Boards are seeded from (day, window, star) with
// a mild outward bias — stateless, so any missed bell replays identically.

#define HOP_01 2000                  // hop range: 20 ly, in 0.01 ly units
#define BOARD_MAX 9                  // neighbors sampled per bell window
#define WALK_VERSION 1

typedef struct {
  uint8_t version;
  uint8_t cadence;                   // units last_slot was counted in
  int16_t cur;                       // current star, STAR_SOL = home
  int16_t last_slot;                 // last processed bell slot, -1 none
  uint32_t day_key;                  // wander-day (start-hour anchored) yyyymmdd
  uint16_t hops;
  uint16_t path_ly10;                // total distance wandered today, x10
  uint16_t far_ly10;                 // farthest from Sol reached today, x10
  int16_t prev_end;                  // yesterday, for the summary card
  uint16_t prev_hops, prev_path_ly10, prev_far_ly10;
  uint32_t prev_day_key;
  uint16_t streak;
} WalkState;
extern WalkState g_walk;

typedef struct {
  uint16_t days, best_hops, far_ever_ly10, longest_streak;
} WalkRecords;
extern WalkRecords g_wrec;

// the last hop flown, for the cutscene and arrival card
typedef struct {
  int16_t from, to;
  float d_ly;
  float t_uni, t_ship;
  double beta, gamma;
} HopResult;
extern HopResult g_hop;

// neighbor board for (current star, window) — built lazily, cached
int board_count(int window);
int board_star(int window, int k);
int board_sel(int window, int hour, int min);   // minute rotation index

void walk_hop_slot(int slot);        // fly that slot's rotation pick
// fold yesterday, reset to Sol; consecutive = this day directly follows
void walk_new_day(uint32_t day_key, bool consecutive);
float star_dist_sol_ly(int idx);
float star_dist_ly(int a, int b);

// honest 1g burn-flip-brake profile for a hop of d light-years
void hop_profile(double d_ly, double *t_uni, double *t_ship,
                 double *beta_peak, double *gamma_peak);

void walk_save(void);
bool walk_load(void);                // false: nothing persisted / stale layout
