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
  Vec3 heart_position = {};
  Vec3 owl_position = {};
  bool owl_visible = false;
};

struct ForestAudioDebugStatus {
  float distance_to_heart = 0.0f;
  float alignment = 0.0f;
  float forest_hum_volume = 0.0f;
  float forest_hum_pitch = 1.0f;
  float signal = 0.0f;
  float mote_chime_cooldown = 0.0f;
  bool owl_visible = false;
  bool owl_appear_sound_played = false;
  Vec3 owl_position = {};
};

void forest_audio_init();
void forest_audio_update(float dt,
                         const ForestAudioPlayerState* player,
                         const ForestAudioWorldState* world);
void forest_audio_shutdown();
void forest_audio_debug_override_hum(float volume, float pitch);

ForestAudioDebugStatus forest_audio_debug_status();

}  // namespace voxel
