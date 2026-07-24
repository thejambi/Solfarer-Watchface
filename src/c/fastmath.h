#pragma once

// Minimal double-precision math for the relativity economy. The SDK's libm
// faults at runtime on double transcendentals (its error path crashes the
// app), so we carry our own. Soft-double arithmetic (+ - * /) works fine;
// these build on it. Accuracy ~1e-12 relative — plenty for β up to the
// redline cap at 1 - 5e-13.
double lh_sqrt(double x);
double lh_exp(double x);
double lh_ln(double x);
double lh_tanh(double x);
double lh_atanh(double x);
double lh_pow10i(int n);      // 10^n, integer n
