#include "world.h"
#include "rng.h"
#include "game.h"
#include "fastmath.h"

Station g_stations[MAX_STATIONS];
int g_n_stations = 0;

// ---------------------------------------------------------------------------
// Procedural names — same syllable pools and draw order as the web game, so a
// shared seed names the same stations on watch and browser.
// ---------------------------------------------------------------------------
static const char *LON[] = {"b","c","d","g","k","l","m","n","r","s","t","v","z",
  "th","dr","kr","tr","br","st","ph","vel","cor",
  "f","h","j","p","w","sh","ch","gr","pr","fr","cl","gl",
  "bl","sk","sp","sl","thr","str","vor","mor","sar","hel",
  "ser","zor","syn","lyr","thal","cyg"};
static const char *LC[] = {"b","d","g","k","l","m","n","r","s","t","v","z",
  "c","f","p","x","th","sh","ch","ph","ss","ll","nn","rr",
  "nd","nt","rn","ld","sk","st","dr","tr"};
static const char *LV[] = {"a","e","i","o","u","y"};
static const char *LV1[] = {"a","e","i","o","u","ae","ei","ia","au","y",
  "ea","eo","io","oa","ou","ui","ya","yo"};
static const char *LEND[] = {"n","r","s","l","x","th","is","or","yx",
  "m","k","d","sh","ss","ll","rn","sk","st","nx","rk",
  "ld","nt","ron","dor","thar","vex","nyx","rax","dex","mir","var"};
static const char *SUFFIX[] = {"Station","Port","Relay","Anchorage","Hub","Gate",
  "Depot","Yards","Outpost","Citadel","Nexus","Terminal","Dock","Haven","Bastion",
  "Platform","Array","Beacon","Sentinel","Forge","Spire","Ring","Core","Node",
  "Stronghold","Enclave","Aerie","Perch","Vista","Orbital"};
#define LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const char *pick(const char **a, int n) { return a[rng_pick(n)]; }

static void append(char *out, size_t cap, const char *s) {
  size_t l = strlen(out);
  if (l < cap - 1) strncpy(out + l, s, cap - 1 - l);
  out[cap - 1] = 0;
}

static void core_name(char *out, size_t cap) {
  out[0] = 0;
  append(out, cap, pick(LON, LEN(LON)));
  append(out, cap, pick(LV1, LEN(LV1)));
  int extra = rng_d() < 0.6 ? 1 : 2;
  for (int i = 0; i < extra; i++) {
    append(out, cap, pick(LC, LEN(LC)));
    append(out, cap, pick(LV, LEN(LV)));
  }
  if (rng_d() < 0.45) append(out, cap, pick(LEND, LEN(LEND)));
  if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
}

static void station_name_gen(char *out, size_t cap) {
  core_name(out, cap);
  append(out, cap, " ");
  append(out, cap, pick(SUFFIX, LEN(SUFFIX)));
}

static void place_name(char *out, size_t cap) {
  if (rng_d() < 0.4) station_name_gen(out, cap);
  else core_name(out, cap);
}

void station_short_name(int idx, char *out, size_t cap) {
  strncpy(out, g_stations[idx].name, cap - 1);
  out[cap - 1] = 0;
  char *sp = strrchr(out, ' ');
  if (sp) *sp = 0;   // every suffix is a single trailing word
}

// ---------------------------------------------------------------------------
// Cluster layout — port of placeStations(): 9 core stations in a ~340 ly box
// plus a 3–4 station deep-space halo, each dock stocking two upgrades.
// ---------------------------------------------------------------------------
static float dist3(float ax, float ay, float az, float bx, float by, float bz) {
  float dx = ax - bx, dy = ay - by, dz = az - bz;
  return (float)lh_sqrt(dx * dx + dy * dy + dz * dz);
}

float station_dist(int a, int b) {
  Station *A = &g_stations[a], *B = &g_stations[b];
  return dist3(A->x, A->y, A->z, B->x, B->y, B->z);
}

void world_build(const char *seed) {
  rng_seed_str(seed);
  g_n_stations = 0;

  Station *s0 = &g_stations[0];
  station_name_gen(s0->name, NAME_LEN);
  s0->x = s0->y = s0->z = 0;
  s0->deep = false;
  s0->last_visit = -1;
  g_n_stations = 1;

  int guard = 0;
  while (g_n_stations < 9 && guard++ < 800) {
    float x = (float)((rng_d() * 2 - 1) * 170.0);
    float y = (float)((rng_d() * 2 - 1) * 0.5 * 170.0);
    float z = (float)((rng_d() * 2 - 1) * 170.0);
    if (dist3(x, y, z, 0, 0, 0) < 30) continue;
    bool clear = true;
    for (int i = 0; i < g_n_stations; i++) {
      if (dist3(x, y, z, g_stations[i].x, g_stations[i].y, g_stations[i].z) <= 40) { clear = false; break; }
    }
    if (!clear) continue;
    Station *s = &g_stations[g_n_stations++];
    station_name_gen(s->name, NAME_LEN);
    s->x = x; s->y = y; s->z = z;
    s->deep = false;
    s->last_visit = -1;
  }

  int deep_n = 3 + rng_pick(2), n_deep = 0, dguard = 0;
  while (n_deep < deep_n && dguard++ < 200 && g_n_stations < MAX_STATIONS) {
    double x = rng_d() * 2 - 1, y = (rng_d() * 2 - 1) * 0.4, z = rng_d() * 2 - 1;
    double len = lh_sqrt(x * x + y * y + z * z);
    if (len < 1e-9) continue;
    double r = DEEP_MIN + rng_d() * (DEEP_MAX - DEEP_MIN);
    float px = (float)(x / len * r), py = (float)(y / len * r), pz = (float)(z / len * r);
    bool clear = true;
    for (int i = 0; i < g_n_stations; i++) {
      if (g_stations[i].deep &&
          dist3(px, py, pz, g_stations[i].x, g_stations[i].y, g_stations[i].z) <= 400) { clear = false; break; }
    }
    if (!clear) continue;
    Station *s = &g_stations[g_n_stations++];
    station_name_gen(s->name, NAME_LEN);
    s->x = px; s->y = py; s->z = pz;
    s->deep = true;
    s->last_visit = -1;
    n_deep++;
  }

  // Shops: two distinct upgrades per dock, every upgrade guaranteed in the core.
  for (int i = 0; i < g_n_stations; i++) {
    int a = rng_pick(N_UPGRADES), b = rng_pick(N_UPGRADES);
    while (b == a) b = rng_pick(N_UPGRADES);
    g_stations[i].shop[0] = a;
    g_stations[i].shop[1] = b;
  }
  int core_idx[MAX_STATIONS], n_core = 0;
  for (int i = 0; i < g_n_stations; i++) if (!g_stations[i].deep) core_idx[n_core++] = i;
  for (int k = 0; k < N_UPGRADES; k++) {
    bool sold = false;
    for (int i = 0; i < n_core && !sold; i++) {
      Station *s = &g_stations[core_idx[i]];
      sold = (s->shop[0] == k || s->shop[1] == k);
    }
    if (!sold) g_stations[core_idx[rng_pick(n_core)]].shop[rng_pick(2)] = k;
  }
}

// ---------------------------------------------------------------------------
// Dock economy events
// ---------------------------------------------------------------------------
const DockEvent DOCK_EVENTS[N_DOCK_EVENTS] = {
  { "FUEL RATIONING - dv x2 here",      200, 100, 100 },
  { "TANK-FARM SURPLUS - dv at 60%",     60, 100, 100 },
  { "EXPORT BOOM - contracts +30%",     100, 130, 100 },
  { "DOCK STRIKE - contracts -20%",     100,  80, 100 },
  { "OUTFITTER CLEARANCE - shop -25%",  100, 100,  75 },
};

int world_roll_dock_event(void) {
  return rng_d() < 0.35 ? rng_pick(N_DOCK_EVENTS) : -1;
}

// ---------------------------------------------------------------------------
// Contracts — port of makeOffer()/makeContracts()
// ---------------------------------------------------------------------------
static const char *CARGO[] = {"medical isotopes","a cryo seed vault","quantum cores",
  "antimatter cells","archive crystals","terraforming spores","vaccine printers",
  "a reactor lattice","orbital tether spools","heirloom soil","singularity ballast",
  "a caged plasma sun","prototype warp coils","memory diamonds","a bonsai biosphere",
  "salvaged AI cores","a monastery's relics","self-replicating looms",
  "cryo-preserved coral","living timber","a shard of a dead moon",
  "unlabeled medical vats","a black-box flight recorder","gene-locked seed grain",
  "a whale in a tank","contraband star charts"};
static const char *PAX[] = {"Dr. {N}","Envoy {N}","the {N} family",
  "a colonist cohort from {P}","Magistrate {N}","two exogeologists",
  "a stasis choir from {P}","Capt. {N} (ret.)","a diplomatic quartet",
  "the last archivist of {P}","Ambassador {N}","the {N} twins",
  "a pilgrim caravan from {P}","a fugitive heiress","a touring orchestra",
  "three cryo-nauts","a xenolinguist","Dr. {N} and her live samples",
  "a newlywed couple","a witness in protective transit","an off-world monk",
  "a delegation of terraformers from {P}"};
static const char *CARGO_DEEP[] = {"a generation-ship seed core","a dormant AI archive",
  "the relics of a lost expedition","a prefabbed embassy, crated","a sealed sleeper vault"};
static const char *PAX_DEEP[] = {"a cryo-sealed magnate","the exiled heir of {P}",
  "a deep-survey crew in stasis","an exiled queen and her court",
  "a terraforming vanguard in cryo"};

// Copy template into out, expanding {N} (person/house) and {P} (place).
static void fill_names(char *out, size_t cap, const char *tpl) {
  out[0] = 0;
  const char *p = tpl;
  while (*p) {
    if (p[0] == '{' && (p[1] == 'N' || p[1] == 'P') && p[2] == '}') {
      char nm[NAME_LEN * 2];
      if (p[1] == 'N') core_name(nm, sizeof nm);
      else place_name(nm, sizeof nm);
      append(out, cap, nm);
      p += 3;
    } else {
      char c[2] = { *p++, 0 };
      append(out, cap, c);
    }
  }
}

static void cargo_name(char *out, size_t cap) {
  out[0] = 0;
  append(out, cap, pick(CARGO, LEN(CARGO)));
  if (rng_d() < 0.7) {
    char pl[NAME_LEN * 2];
    place_name(pl, sizeof pl);
    append(out, cap, " from ");
    append(out, cap, pl);
  }
}

static void make_offer(int from, int t, Contract *c) {
  memset(c, 0, sizeof *c);
  float d = station_dist(from, t);
  bool lng = d > LONG_HAUL;
  c->to = t; c->d = d; c->lng = lng;
  if (rng_d() < 0.55) {
    // cargo: universe-time deadline
    double beta_req = lng ? 0.9 + rng_d() * 0.09 : 0.55 + rng_d() * 0.4;
    int g_limit = (int)(4 + rng_d() * 14 + 0.5);
    c->type = 0;
    c->g_limit = g_limit;
    bool deep_flavor = lng && rng_d() < 0.4;
    if (deep_flavor) fill_names(c->what, WHAT_LEN, pick(CARGO_DEEP, LEN(CARGO_DEEP)));
    else cargo_name(c->what, WHAT_LEN);
    c->deadline = (float)(d / beta_req + (lng ? 25 : 4));
    double pay = lng
      ? (250 + d * (0.45 + (beta_req - 0.9) * 2)) * (1 + (g_limit < 11 ? (11 - g_limit) * 0.05 : 0))
      : (90 + d * (0.9 + (beta_req - 0.5) * 3.2)) * (1 + (g_limit < 11 ? (11 - g_limit) * 0.05 : 0));
    c->pay = (int32_t)(pay + 0.5);
    return;
  }
  if (lng) {
    // long-haul passage: cryo pods, human aging cap regardless of distance
    double max_aging = 2 + rng_d() * 4;
    int g_limit = (int)(3 + rng_d() * 4 + 0.5);
    c->type = 1;
    c->g_limit = g_limit;
    if (rng_d() < 0.45) fill_names(c->what, WHAT_LEN, pick(PAX_DEEP, LEN(PAX_DEEP)));
    else fill_names(c->what, WHAT_LEN, pick(PAX, LEN(PAX)));
    c->deadline = d / 0.7f + 50;
    c->max_aging = (float)max_aging;
    c->pay = (int32_t)((400 + d * 0.55) * (1 + (g_limit < 8 ? (8 - g_limit) * 0.06 : 0)) + 0.5);
    return;
  }
  // passenger: ship-time aging cap → minimum average γ
  double g_req = 4 + rng_d() * 12;
  int g_limit = (int)(4 + rng_d() * 4 + 0.5);
  c->type = 1;
  c->g_limit = g_limit;
  fill_names(c->what, WHAT_LEN, pick(PAX, LEN(PAX)));
  c->deadline = d / 0.7f + 10;
  c->max_aging = (float)(d / g_req * 1.25 + 1.5);
  c->pay = (int32_t)((160 + d * (0.7 + g_req * 0.2)) * (1 + (g_limit < 8 ? (8 - g_limit) * 0.06 : 0)) + 0.5);
}

// Watchface: a single offer, rolled on whatever the RNG stream is seeded to.
void world_make_offer(int from, int to, Contract *out) { make_offer(from, to, out); }

void world_make_contracts(int from, bool deep_license, uint16_t pay_pct, Contract out[3]) {
  int n = 0, guard = 0;
  bool used[MAX_STATIONS] = { false };
  used[from] = true;
  int n_long = deep_license ? 1 + rng_pick(2) : 0;
  while (n < 3 - n_long && guard++ < 200) {
    int t = rng_pick(g_n_stations);
    if (used[t] || g_stations[t].deep) continue;
    used[t] = true;
    make_offer(from, t, &out[n++]);
  }
  if (n_long) {
    int deep_idx[MAX_STATIONS], nd = 0;
    for (int i = 0; i < g_n_stations; i++)
      if (g_stations[i].deep && i != from) deep_idx[nd++] = i;
    while (n < 3 && nd > 0) {
      int j = rng_pick(nd);
      int t = deep_idx[j];
      deep_idx[j] = deep_idx[--nd];
      make_offer(from, t, &out[n++]);
    }
    guard = 0;
    while (n < 3 && guard++ < 200) {
      int t = rng_pick(g_n_stations);
      if (used[t] || g_stations[t].deep) continue;
      used[t] = true;
      make_offer(from, t, &out[n++]);
    }
  }
  for (int i = 0; i < n; i++)
    out[i].pay = (int32_t)((int64_t)out[i].pay * pay_pct / 100);
}
