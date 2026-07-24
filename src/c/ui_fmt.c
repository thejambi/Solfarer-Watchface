#include "ui.h"

void fmt1(char *buf, size_t cap, double v) {
  bool neg = v < 0;
  if (neg) v = -v;
  int whole = (int)v;
  int tenths = (int)((v - whole) * 10 + 0.5);
  if (tenths >= 10) { whole++; tenths = 0; }
  snprintf(buf, cap, "%s%d.%d", neg ? "-" : "", whole, tenths);
}

void fmt_years(char *buf, size_t cap, double y) {
  if (y >= 1000) {
    int k = (int)(y / 10 + 0.5);            // kyr with two decimals
    snprintf(buf, cap, "%d.%02dkyr", k / 100, k % 100);
  } else {
    char t[12];
    fmt1(t, sizeof t, y);
    snprintf(buf, cap, "%syr", t);
  }
}

void fmt_beta(char *buf, size_t cap, double beta) {
  if (beta < 0.99995) {
    int b = (int)(beta * 10000 + 0.5);
    snprintf(buf, cap, "0.%04dc", b);
  } else {
    // count the nines so redline speeds stay readable
    int nines = 0;
    double x = 1.0 - beta;
    while (x < 0.1 && nines < 12) { x *= 10; nines++; }
    snprintf(buf, cap, "0.(9x%d)c", nines);
  }
}

void fmt_gamma(char *buf, size_t cap, double gamma) {
  if (gamma >= 10000) snprintf(buf, cap, "%dk", (int)(gamma / 1000 + 0.5));
  else if (gamma >= 100) snprintf(buf, cap, "%d", (int)(gamma + 0.5));
  else fmt1(buf, cap, gamma);
}
