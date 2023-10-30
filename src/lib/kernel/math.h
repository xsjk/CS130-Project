#pragma once

inline float round(float x) {
  return (int)(x + (x >= 0) - 0.5);
}
