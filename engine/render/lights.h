#pragma once

#include "math/vec3.h"

namespace voxel {

struct GameplayLight {
  Vec3 position = {};
  Vec3 color = {1.0f, 1.0f, 1.0f};
  float radius = 1.0f;
  float intensity = 1.0f;
  bool active = false;
};

constexpr int kMaxGameplayLights = 32;
constexpr int kMaxRendererGameplayLights = 8;

}  // namespace voxel
