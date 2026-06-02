#include "forest_audio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "audio.h"

namespace voxel {

namespace {

constexpr float kMinHumVolume = 0.04f;
constexpr float kMaxHumVolume = 0.18f;
constexpr float kBaseHumPitch = 1.0f;
constexpr float kCloseHumPitch = 1.15f;
constexpr float kHeartPitchRange = 120.0f;
constexpr float kHumSmoothingSpeed = 2.2f;
constexpr float kChimeAlignmentThreshold = 0.65f;
constexpr float kStrongChimeAlignment = 0.85f;
constexpr float kFootstepMinSpeed = 0.8f;
constexpr float kFootstepMaxSpeed = 8.5f;

struct ForestAudioState {
  float hum_volume = 0.0f;
  float hum_pitch = 1.0f;
  float distance_to_heart = 0.0f;
  float previous_distance_to_heart = 0.0f;
  float alignment = 0.0f;
  float signal = 0.0f;
  float mote_chime_cooldown = 1.6f;
  float footstep_cooldown = 0.0f;
  float debug_hum_volume = 0.0f;
  float debug_hum_pitch = 1.0f;
  float debug_override_timer = 0.0f;
  bool has_previous_distance = false;
  bool owl_visible = false;
  bool owl_appear_sound_played = false;
  Vec3 owl_position = {};
  std::uint32_t rng = 0x8f4c2d31u;
};

ForestAudioState g_forest_audio;

float clamp_float(float value, float minimum, float maximum) {
  return std::max(minimum, std::min(maximum, value));
}

float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

float next_random_unit() {
  std::uint32_t x = g_forest_audio.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_forest_audio.rng = x;
  return static_cast<float>(x & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

float horizontal_distance(Vec3 a, Vec3 b) {
  const float dx = b.x - a.x;
  const float dz = b.z - a.z;
  return std::sqrt(dx * dx + dz * dz);
}

Vec3 horizontal_direction(Vec3 from, Vec3 to) {
  Vec3 direction = {to.x - from.x, 0.0f, to.z - from.z};
  const float len = length(direction);
  if (len <= 0.00001f) {
    return {};
  }
  return direction / len;
}

float smoothed(float current, float target, float dt, float speed) {
  const float t = 1.0f - std::exp(-std::max(0.0f, dt) * speed);
  return lerp(current, target, clamp_float(t, 0.0f, 1.0f));
}

void update_hum(float dt,
                const ForestAudioPlayerState& player,
                const ForestAudioWorldState& world) {
  ForestAudioState& state = g_forest_audio;
  const Vec3 direction_to_heart = horizontal_direction(player.position, world.heart_position);
  const Vec3 player_forward = normalize({player.forward.x, 0.0f, player.forward.z});

  state.distance_to_heart = horizontal_distance(player.position, world.heart_position);
  state.alignment = clamp_float(dot(player_forward, direction_to_heart), 0.0f, 1.0f);

  const float close = clamp_float(1.0f - state.distance_to_heart / kHeartPitchRange, 0.0f, 1.0f);
  state.signal = state.alignment * (0.72f + close * 0.28f);
  const float target_volume = lerp(kMinHumVolume, kMaxHumVolume, state.signal);
  const float target_pitch = lerp(kBaseHumPitch, kCloseHumPitch, close);

  if (state.debug_override_timer > 0.0f) {
    state.debug_override_timer = std::max(0.0f, state.debug_override_timer - dt);
    state.hum_volume = smoothed(state.hum_volume, state.debug_hum_volume, dt, kHumSmoothingSpeed * 3.0f);
    state.hum_pitch = smoothed(state.hum_pitch, state.debug_hum_pitch, dt, kHumSmoothingSpeed * 3.0f);
  } else {
    state.hum_volume = smoothed(state.hum_volume, target_volume, dt, kHumSmoothingSpeed);
    state.hum_pitch = smoothed(state.hum_pitch, target_pitch, dt, kHumSmoothingSpeed);
  }

  audio_set_forest_hum(state.hum_volume, state.hum_pitch);
}

void update_mote_chimes(float dt) {
  ForestAudioState& state = g_forest_audio;
  state.mote_chime_cooldown = std::max(0.0f, state.mote_chime_cooldown - dt);
  if (state.alignment < kChimeAlignmentThreshold) {
    return;
  }
  if (state.mote_chime_cooldown > 0.0f) {
    return;
  }

  const bool moving_toward_heart = state.has_previous_distance &&
      state.distance_to_heart < state.previous_distance_to_heart - 0.015f;
  const float strong_alignment = clamp_float((state.alignment - kStrongChimeAlignment) /
                                             (1.0f - kStrongChimeAlignment),
                                             0.0f,
                                             1.0f);
  const float chance = 0.34f + strong_alignment * 0.24f + (moving_toward_heart ? 0.18f : 0.0f);
  if (next_random_unit() < chance) {
    const float intensity = clamp_float(0.22f + state.signal * 0.30f +
                                            strong_alignment * 0.14f +
                                            next_random_unit() * 0.12f,
                                        0.2f,
                                        0.6f);
    audio_play_mote_chime(intensity);
  }

  const float cooldown_min = state.alignment >= kStrongChimeAlignment ? 1.45f : 1.8f;
  const float cooldown_max = moving_toward_heart ? 3.0f : 4.0f;
  state.mote_chime_cooldown = lerp(cooldown_min, cooldown_max, next_random_unit());
}

void update_owl(const ForestAudioWorldState& world) {
  ForestAudioState& state = g_forest_audio;
  state.owl_position = world.owl_position;
  state.owl_visible = world.owl_visible;
  if (!world.owl_visible) {
    state.owl_appear_sound_played = false;
    return;
  }
  if (!state.owl_appear_sound_played) {
    audio_play_owl_appear();
    state.owl_appear_sound_played = true;
  }
}

void update_footsteps(float dt, const ForestAudioPlayerState& player) {
  ForestAudioState& state = g_forest_audio;
  state.footstep_cooldown = std::max(0.0f, state.footstep_cooldown - dt);
  if (!player.on_ground || player.movement_speed < kFootstepMinSpeed || state.footstep_cooldown > 0.0f) {
    return;
  }

  const float speed_t = clamp_float((player.movement_speed - kFootstepMinSpeed) /
                                    (kFootstepMaxSpeed - kFootstepMinSpeed),
                                    0.0f,
                                    1.0f);
  audio_play_footstep_rustle(0.18f + speed_t * 0.24f);
  state.footstep_cooldown = lerp(0.48f, 0.24f, speed_t);
}

}  // namespace

void forest_audio_init() {
  g_forest_audio = {};
  g_forest_audio.hum_pitch = 1.0f;
  g_forest_audio.debug_hum_pitch = 1.0f;
  g_forest_audio.mote_chime_cooldown = 1.6f;
  g_forest_audio.rng = 0x8f4c2d31u;
}

void forest_audio_update(float dt,
                         const ForestAudioPlayerState* player,
                         const ForestAudioWorldState* world) {
  if (player == nullptr || world == nullptr) {
    return;
  }

  dt = clamp_float(dt, 0.0f, 0.10f);
  update_hum(dt, *player, *world);
  update_mote_chimes(dt);
  update_owl(*world);
  update_footsteps(dt, *player);

  g_forest_audio.previous_distance_to_heart = g_forest_audio.distance_to_heart;
  g_forest_audio.has_previous_distance = true;
}

void forest_audio_shutdown() {
  g_forest_audio = {};
}

void forest_audio_debug_override_hum(float volume, float pitch) {
  g_forest_audio.debug_hum_volume = clamp_float(volume, 0.0f, kMaxHumVolume);
  g_forest_audio.debug_hum_pitch = clamp_float(pitch, 0.55f, 1.85f);
  g_forest_audio.debug_override_timer = 1.5f;
  audio_set_forest_hum(g_forest_audio.debug_hum_volume, g_forest_audio.debug_hum_pitch);
}

ForestAudioDebugStatus forest_audio_debug_status() {
  ForestAudioDebugStatus status;
  status.distance_to_heart = g_forest_audio.distance_to_heart;
  status.alignment = g_forest_audio.alignment;
  status.forest_hum_volume = g_forest_audio.hum_volume;
  status.forest_hum_pitch = g_forest_audio.hum_pitch;
  status.signal = g_forest_audio.signal;
  status.mote_chime_cooldown = g_forest_audio.mote_chime_cooldown;
  status.owl_visible = g_forest_audio.owl_visible;
  status.owl_appear_sound_played = g_forest_audio.owl_appear_sound_played;
  status.owl_position = g_forest_audio.owl_position;
  return status;
}

}  // namespace voxel
