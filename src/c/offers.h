#pragma once
#include "game.h"

// The offer board, stretched to every reachable port. Each bell window rolls
// a contract to every destination, seeded from (map seed, day, window, from,
// to) — stateless, so nothing persists, any bell replays identically during
// catch-up, and the minute hand can walk the whole board instead of cycling
// three slots. The board refreshes every window.

int offers_count(void);                 // destinations from the current dock
int offers_dest(int i);                 // station index of board entry i
int offers_window(int hour, int min);   // bell window id under the cadence
int offers_sel(int hour, int min);      // the minute rotation's board entry
void offers_get(int i, int window, Contract *out);
