#pragma once
#include <pebble.h>

// mulberry32 PRNG + FNV-1a seeding — exact port of the web game's world RNG,
// so a seed string builds the same cluster here as in the browser.
void rng_seed_str(const char *str);
uint32_t rng_get_state(void);
void rng_set_state(uint32_t s);
uint32_t rng_u32(void);
double rng_d(void);                       // [0,1) as double, matching JS
int rng_pick(int n);                      // (rng()*n)|0
