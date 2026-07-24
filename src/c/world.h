#pragma once
#include <pebble.h>

#define MAX_STATIONS 14
#define NAME_LEN 28
#define WHAT_LEN 56

#define LONG_HAUL 500.0f
#define DEEP_MIN 800.0
#define DEEP_MAX 2500.0

typedef struct {
  char name[NAME_LEN];
  float x, y, z;                // ly; map view projects x/z top-down
  uint8_t shop[2];              // two upgrade ids stocked at this dock
  bool deep;                    // long-haul halo station (license-gated)
  float last_visit;             // universe time of last dock, -1 = never
} Station;

typedef struct {
  uint8_t type;                 // 0 cargo, 1 passenger
  uint8_t lng;                  // long-haul tier
  uint8_t to;
  uint8_t g_limit;              // max maneuvering g the load tolerates
  float d;                      // ly
  float deadline;               // universe-years
  float max_aging;              // ship-years cap (passengers only)
  int32_t pay;                  // base pay (dock events baked in)
  char what[WHAT_LEN];
} Contract;

// Dock economy events — fuel/pay/shop price multipliers ×100 (100 = neutral).
typedef struct { const char *txt; uint16_t fuel_pct, pay_pct, shop_pct; } DockEvent;
#define N_DOCK_EVENTS 5
extern const DockEvent DOCK_EVENTS[N_DOCK_EVENTS];

extern Station g_stations[MAX_STATIONS];
extern int g_n_stations;

void world_build(const char *seed);
float station_dist(int a, int b);
int world_roll_dock_event(void);  // -1 = none
// Fills out[3] with offers from `from`. pay_pct: dock-event pay multiplier ×100.
void world_make_contracts(int from, bool deep_license, uint16_t pay_pct, Contract out[3]);
// One offer from `from` to `to` off the current RNG stream (no pay_pct baked).
void world_make_offer(int from, int to, Contract *out);
// Compact map label: station name with the " Suffix" stripped.
void station_short_name(int idx, char *out, size_t cap);
