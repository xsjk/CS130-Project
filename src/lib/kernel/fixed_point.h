#pragma once

#include <stdint.h>

typedef int32_t fixed_point;

inline fixed_point
fp_create (int32_t x)
{
  return x << 14;
}

inline fixed_point
fp_add (fixed_point a, fixed_point b)
{
  return a + b;
}

inline fixed_point
fp_sub (fixed_point a, fixed_point b)
{
  return a - b;
}

inline fixed_point
fp_mul (fixed_point a, fixed_point b)
{
  // Multiply x by y:	((int64_t) x) * y / f
  return ((int64_t)a) * b >> 14;
}

inline fixed_point
fp_div (fixed_point a, fixed_point b)
{
  // Divide x by y:	((int64_t) x) * f / y
  return (((int64_t)a) << 14) / b;
}

inline int32_t
fp_to_int_nearest (fixed_point a)
{
  // Convert x to integer (rounding to nearest):
  // (x + f / 2) / f if x >= 0,
  // (x - f / 2) / f if x <= 0.
  if (a >= 0)
    return (a + (1 << 14) / 2) >> 14;
  else
    return (a - (1 << 14) / 2) >> 14;
}

inline int32_t
fp_to_int_down (fixed_point a)
{
  // Convert x to integer (rounding toward zero):
  // x / f.
  return a >> 14;
}
