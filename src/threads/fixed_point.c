#include"fixed_point.h"

fixed_point fp_create(int32_t x) {
  return (fixed_point) { x << 14 };
}
fixed_point fp_add(fixed_point a, fixed_point b) {
  return (fixed_point) { a.value + b.value };
}
fixed_point fp_sub(fixed_point a, fixed_point b) {
  return (fixed_point) { a.value - b.value };
}
fixed_point fp_mul(fixed_point a, fixed_point b) {
  //Multiply x by y:	((int64_t) x) * y / f
  return (fixed_point) { ((int64_t)a.value)* b.value >> 14 };
}
fixed_point fp_div(fixed_point a, fixed_point b) {
  // Divide x by y:	((int64_t) x) * f / y
  return (fixed_point) { (((int64_t)a.value) << 14) / b.value };
}
int32_t fp_to_int_round(fixed_point a) {
  // Convert x to integer (rounding to nearest):	
  // (x + f / 2) / f if x >= 0,
  // (x - f / 2) / f if x <= 0.
  if (a.value >= 0)
    return (a.value + (1 << 14) / 2) >> 14;
  else
    return (a.value - (1 << 14) / 2) >> 14;
}
