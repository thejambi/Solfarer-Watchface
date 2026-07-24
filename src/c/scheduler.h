#pragma once
#include <pebble.h>

// The bell schedule: which runs have happened, which day this career belongs
// to, and the daily rollover. Slots are wall-clock (hour, or half-hour) so
// DST and timezone shifts just work; a clock set backwards never re-runs.

typedef enum { SCHED_IDLE, SCHED_RAN, SCHED_NEWDAY } SchedEvent;

// Persisted whole. APPEND-ONLY, like Settings: new fields at the end, older
// saves stop short and keep defaults. Bump SCHED_VERSION only on reorder.
#define SCHED_VERSION 1
typedef struct {
  uint8_t version;
  uint8_t cadence;              // cadence the slots were counted in
  int16_t last_slot;            // last processed bell slot of day_key, -1 none
  uint32_t day_key;             // yyyymmdd this career belongs to
  uint16_t streak;              // consecutive days flown
  uint32_t prev_day_key;        // finished day, for the summary card
  int32_t prev_balance;
  uint16_t prev_deliveries;
  float prev_gamma;
} SchedState;

extern SchedState g_sched;

void scheduler_boot(bool had_save);      // after settings_init + game_init
SchedEvent scheduler_tick(struct tm *t); // call every minute
bool scheduler_busy(void);               // silent catch-up in progress
bool scheduler_take_summary(void);       // boot rolled a day: show the card once
void scheduler_reseed(void);             // seed setting changed: fresh career now
void scheduler_apply_cadence(void);      // cadence setting changed mid-session
void scheduler_save(void);
