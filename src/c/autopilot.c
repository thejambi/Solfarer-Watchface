#include "autopilot.h"
#include "settings.h"
#include "offers.h"

char g_auto_from[NAME_LEN];
char g_auto_to[NAME_LEN];

// Static: MAX_STATIONS contracts would eat too much of the 2K app stack.
static Contract s_cand[MAX_STATIONS];

static bool plan_ok(const RunPlan *p) {
  return p->deadline_ok && p->aging_ok && p->retire_ok;
}

void auto_refuel(void) {
  float missing = tank_cap() - g.fuel;
  if (missing > 0.05f) buy_fuel(missing);   // clamps to wallet on its own
}

int auto_pick_offer(int dispatch, int selected, int window) {
  int n = offers_count();
  if (n < 1) return 0;
  if (selected >= n) selected %= n;
  if (dispatch == DSP_SELECTED) return selected;

  bool ok[MAX_STATIONS];
  int32_t pay[MAX_STATIONS];
  float margin[MAX_STATIONS];
  for (int i = 0; i < n; i++) {
    offers_get(i, window, &s_cand[i]);
    RunPlan p = game_plan(&s_cand[i]);
    ok[i] = plan_ok(&p);
    pay[i] = contract_pay(&s_cand[i]);
    margin[i] = s_cand[i].deadline - p.t_uni;
  }
  if (dispatch == DSP_FALLBACK && ok[selected]) return selected;
  if (dispatch == DSP_SAFEST) {
    // widest deadline margin among the OK offers; least-late otherwise
    int best = selected;
    float best_m = -1e18f;
    for (int i = 0; i < n; i++) {
      float m = margin[i] + (ok[i] ? 1e9f : 0);
      if (m > best_m) { best_m = m; best = i; }
    }
    return best;
  }
  // FALLBACK (selected was doomed) and BESTPAY both want the richest OK offer
  int best = -1;
  int32_t best_pay = -1;
  for (int i = 0; i < n; i++)
    if (ok[i] && pay[i] > best_pay) { best_pay = pay[i]; best = i; }
  if (best >= 0) return best;
  if (dispatch == DSP_FALLBACK) return selected;   // nothing OK: fly it anyway
  for (int i = 0; i < n; i++)                      // BESTPAY: richest, late or not
    if (pay[i] > best_pay) { best_pay = pay[i]; best = i; }
  return best;
}

// Rejuv is excluded: without retirement its +6yr career buys nothing.
static const uint8_t PRIO[] = { UP_TANK, UP_DRIVE, UP_DAMPER, UP_BROKER,
                                UP_OVERDRIVE, UP_AUTOPILOT };

static int prio_rank(int id) {
  for (int i = 0; i < (int)sizeof PRIO; i++)
    if (PRIO[i] == id) return i;
  return 99;
}

bool auto_buy_upgrade(int strategy) {
  if (strategy == UPG_OFF) return false;
  Station *st = &g_stations[g.station];
  int cand[2], n = 0;
  for (int s = 0; s < 2; s++) {
    int id = st->shop[s];
    if (id == UP_REJUV) continue;
    int32_t cost = upgrade_cost(id);
    if (cost < 0 || cost > g.credits) continue;
    // never shop the ship broke: keep a full-tank refuel in reserve
    if (g.credits - cost < fuel_cost(tank_cap())) continue;
    cand[n++] = id;
  }
  if (n == 0) return false;
  int id = cand[0];
  if (n == 2) {
    if (strategy == UPG_CHEAPEST)
      id = upgrade_cost(cand[0]) <= upgrade_cost(cand[1]) ? cand[0] : cand[1];
    else if (strategy == UPG_RANDOM)
      id = cand[rand() % 2];
    else
      id = prio_rank(cand[0]) <= prio_rank(cand[1]) ? cand[0] : cand[1];
  }
  return buy_upgrade(id);
}

int auto_run_bell(int dispatch, int upgrades, int hour, int min) {
  auto_refuel();
  // stranded — pump unaffordable and tank near dry: the tow is the escape
  // hatch, and it always works (cost clamps to whatever's in the wallet)
  if (g.fuel < tank_cap() * 0.25f && tow_available()) {
    guild_tow();
    auto_refuel();
  }
  int window = offers_window(hour, min);
  int idx = auto_pick_offer(dispatch, offers_sel(hour, min), window);
  Contract c;
  offers_get(idx, window, &c);
  station_short_name(g.station, g_auto_from, sizeof g_auto_from);
  station_short_name(c.to, g_auto_to, sizeof g_auto_to);
  game_resolve(&c);
  auto_refuel();
  auto_buy_upgrade(upgrades);
  return idx;
}
