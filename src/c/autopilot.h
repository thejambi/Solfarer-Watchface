#pragma once
#include "game.h"

// One bell = everything a player does at a dock, hands-free: top off the tank,
// take the guild tow if truly stranded, fly a contract off the board, refuel
// on the payout, and shop the dock's two-item outfitter. Fills g_last like
// win_flight did. The board is offers.c — every reachable port, per window.

extern char g_auto_from[NAME_LEN];   // short names of the leg just flown
extern char g_auto_to[NAME_LEN];

// board entry to fly: `selected` is the minute rotation's entry at the bell
int  auto_pick_offer(int dispatch, int selected, int window);
void auto_refuel(void);
bool auto_buy_upgrade(int strategy);
int  auto_run_bell(int dispatch, int upgrades, int hour, int min);
