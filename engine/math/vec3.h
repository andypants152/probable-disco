#pragma once

#include <cmath>

namespace voxel {

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 operator-(Vec3 a, Vec3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 operator*(Vec3 v, float scale) {
  return {v.x * scale, v.y * scale, v.z * scale};
}

inline Vec3 operator/(Vec3 v, float scale) {
  return {v.x / scale, v.y / scale, v.z / scale};
}

inline Vec3& operator+=(Vec3& a, Vec3 b) {
  a = a + b;
  return a;
}

inline Vec3& operator-=(Vec3& a, Vec3 b) {
  a = a - b;
  return a;
}

inline float dot(Vec3 a, Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(Vec3 a, Vec3 b) {
  return {
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x,
  };
}

inline float length(Vec3 v) {
  return std::sqrt(dot(v, v));
}

inline Vec3 normalize(Vec3 v) {
  const float len = length(v);
  if (len <= 0.00001f) {
    return {};
  }
  return v / len;
}

}  // namespace voxel
