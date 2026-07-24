#pragma once
#include <pebble.h>

// Bell timing for the wander. Days are anchored at the configured start hour
// (the "wander day" — 7am to 7am, say), slots are wall-clock derived so DST
// just works, and missed bells replay synchronously — a replayed hop is just
// a board lookup, so there is no pump.

typedef enum { SCHED_IDLE, SCHED_HOPPED, SCHED_NEWDAY } SchedEvent;

void scheduler_boot(void);               // after settings + stars + walk_load
SchedEvent scheduler_tick(struct tm *t); // call every minute
bool scheduler_take_summary(void);       // boot crossed a day: card owed once
void scheduler_resync(void);             // cadence/start-hour setting changed
