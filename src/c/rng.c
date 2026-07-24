#include "rng.h"

static uint32_t s_state;

void rng_seed_str(const char *str) {
  uint32_t h = 2166136261u;               // FNV-1a, as in the web strToSeed()
  for (const char *p = str; *p; p++) {
    h ^= (uint8_t)*p;
    h *= 16777619u;
  }
  s_state = h;
}

uint32_t rng_get_state(void) { return s_state; }
void rng_set_state(uint32_t s) { s_state = s; }

uint32_t rng_u32(void) {
  s_state += 0x6D2B79F5u;
  uint32_t a = s_state;
  uint32_t t = (a ^ (a >> 15)) * (1u | a);
  t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
  return t ^ (t >> 14);
}

double rng_d(void) { return rng_u32() / 4294967296.0; }

int rng_pick(int n) { return (int)(rng_d() * n); }
