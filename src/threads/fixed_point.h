#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include<stdint.h>

typedef struct {
    int32_t value;
} fixed_point;

fixed_point fp_create(int32_t x);
fixed_point fp_add(fixed_point a, fixed_point b);
fixed_point fp_sub(fixed_point a, fixed_point b);
// fixed_point fp_add_int(fixed_point a, int32_t b);
// fixed_point fp_sub_int(fixed_point a, int32_t b);
fixed_point fp_mul(fixed_point a, fixed_point b);
// fixed_point fp_mul_int(fixed_point a, int32_t b);
fixed_point fp_div(fixed_point a, fixed_point b);
// fixed_point fp_div_int(fixed_point a, int32_t b);
int32_t fp_to_int_round(fixed_point a);

#endif /* FIXED_POINT_H */