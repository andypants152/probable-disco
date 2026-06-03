#pragma once

#include "math/vec3.h"

namespace voxel {

struct ForestAudioPlayerState {
  Vec3 position = {};
  Vec3 forward = {0.0f, 0.0f, 1.0f};
  float movement_speed = 0.0f;
  bool on_ground = true;
};

struct ForestAudioWorldState {
  Vec3 objective_position = {};
  Vec3 owl_position = {};
  bool owl_visible = false;
};

void forest_audio_init();
void forest_audio_update(float dt,
                         const ForestAudioPlayerState* player,
                         const ForestAudioWorldState* world);
void forest_audio_shutdown();

}  // namespace voxel
