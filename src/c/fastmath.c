#include "fastmath.h"
#include <stdint.h>

#define LN2 0.6931471805599453

double lh_sqrt(double x) {
  if (x <= 0) return 0;
  union { double d; uint64_t u; } v = { x };
  v.u = (v.u >> 1) + 0x1FF8000000000000ULL;   // halve the exponent for a seed
  double y = v.d;
  for (int i = 0; i < 6; i++) y = 0.5 * (y + x / y);
  return y;
}

double lh_exp(double x) {
  if (x > 700) x = 700;
  if (x < -700) return 0;
  int k = (int)(x / LN2 + (x >= 0 ? 0.5 : -0.5));   // x = k·ln2 + r
  double r = x - k * LN2;
  double term = 1, sum = 1;
  for (int i = 1; i <= 12; i++) { term *= r / i; sum += term; }
  union { double d; uint64_t u; } s = { 1.0 };      // scale by 2^k
  s.u += (uint64_t)((int64_t)k << 52);
  return sum * s.d;
}

double lh_ln(double x) {
  if (x <= 0) return -1e308;
  union { double d; uint64_t u; } v = { x };
  int e = (int)((v.u >> 52) & 0x7FF) - 1023;        // x = m·2^e, m in [1,2)
  v.u = (v.u & 0x000FFFFFFFFFFFFFULL) | 0x3FF0000000000000ULL;
  double m = v.d;
  if (m > 1.5) { m *= 0.5; e++; }                   // recenter m into [0.75,1.5)
  double t = (m - 1) / (m + 1), t2 = t * t;         // ln(m) = 2·atanh series
  double sum = 0, pw = t;
  for (int i = 1; i <= 15; i += 2) { sum += pw / i; pw *= t2; }
  return e * LN2 + 2 * sum;
}

double lh_tanh(double x) {
  if (x > 20) return 1;
  if (x < -20) return -1;
  double e2 = lh_exp(2 * x);
  return (e2 - 1) / (e2 + 1);
}

double lh_atanh(double x) {
  if (x >= 1) return 40;                             // clamp: β pinned at cap
  if (x <= -1) return -40;
  return 0.5 * lh_ln((1 + x) / (1 - x));
}

double lh_pow10i(int n) {
  double r = 1, b = n < 0 ? 0.1 : 10.0;
  int k = n < 0 ? -n : n;
  while (k--) r *= b;
  return r;
}
