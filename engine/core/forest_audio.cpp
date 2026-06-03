#include "forest_audio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "audio.h"

namespace voxel {

namespace {

constexpr float kObjectivePitchRange = 120.0f;
constexpr float kChimeAlignmentThreshold = 0.45f;
constexpr float kStrongChimeAlignment = 0.78f;
constexpr float kFootstepMinSpeed = 0.8f;
constexpr float kFootstepMaxSpeed = 8.5f;
constexpr float kMoteScale[] = {
  293.66f,  // D4
  349.23f,  // F4
  392.00f,  // G4
  440.00f,  // A4
  523.25f,  // C5
  587.33f,  // D5
  698.46f,  // F5
  783.99f,  // G5
  880.00f,  // A5
  1046.50f, // C6
};

struct ForestAudioState {
  float distance_to_objective = 0.0f;
  float previous_distance_to_objective = 0.0f;
  float alignment = 0.0f;
  float signal = 0.0f;
  float mote_chime_cooldown = 1.6f;
  float mote_phrase_timer = 0.0f;
  int mote_phrase_step = 0;
  int mote_phrase_length = 0;
  int mote_phrase_root = 0;
  bool mote_phrase_rising = true;
  float footstep_cooldown = 0.0f;
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

float mote_note_frequency(int note) {
  const int note_count = static_cast<int>(sizeof(kMoteScale) / sizeof(kMoteScale[0]));
  note = std::max(0, std::min(note_count - 1, note));
  return kMoteScale[note];
}

float note_pan(const ForestAudioPlayerState& player, const ForestAudioWorldState& world) {
  const Vec3 direction = horizontal_direction(player.position, world.objective_position);
  const Vec3 forward = normalize({player.forward.x, 0.0f, player.forward.z});
  const Vec3 right = {forward.z, 0.0f, -forward.x};
  return clamp_float(dot(right, direction) * 0.58f, -0.65f, 0.65f);
}

void update_guidance_signal(const ForestAudioPlayerState& player,
                            const ForestAudioWorldState& world) {
  ForestAudioState& state = g_forest_audio;
  const Vec3 direction_to_objective = horizontal_direction(player.position, world.objective_position);
  const Vec3 player_forward = normalize({player.forward.x, 0.0f, player.forward.z});

  state.distance_to_objective = horizontal_distance(player.position, world.objective_position);
  state.alignment = clamp_float(dot(player_forward, direction_to_objective), 0.0f, 1.0f);

  const float close = clamp_float(1.0f - state.distance_to_objective / kObjectivePitchRange, 0.0f, 1.0f);
  state.signal = state.alignment * (0.72f + close * 0.28f);
}

void update_mote_chimes(float dt,
                        const ForestAudioPlayerState& player,
                        const ForestAudioWorldState& world) {
  if (!audio_ready_for_gameplay_sound()) {
    return;
  }

  ForestAudioState& state = g_forest_audio;
  state.mote_chime_cooldown = std::max(0.0f, state.mote_chime_cooldown - dt);
  state.mote_phrase_timer = std::max(0.0f, state.mote_phrase_timer - dt);

  const bool moving_toward_objective = state.has_previous_distance &&
      state.distance_to_objective < state.previous_distance_to_objective - 0.015f;
  const float close = clamp_float(1.0f - state.distance_to_objective / kObjectivePitchRange, 0.0f, 1.0f);
  const float strong_alignment = clamp_float((state.alignment - kStrongChimeAlignment) /
                                             (1.0f - kStrongChimeAlignment),
                                             0.0f,
                                             1.0f);

  if (state.mote_phrase_step < state.mote_phrase_length && state.mote_phrase_timer <= 0.0f) {
    const int rising_offsets[] = {0, 2, 3, 5};
    const int searching_offsets[] = {0, 1, -1, 2};
    const int* offsets = state.mote_phrase_rising ? rising_offsets : searching_offsets;
    const int note = state.mote_phrase_root + offsets[state.mote_phrase_step];
    const float pan = note_pan(player, world) + (next_random_unit() * 0.22f - 0.11f);
    const float intensity = clamp_float(0.28f + state.signal * 0.34f + close * 0.16f, 0.24f, 0.82f);
    audio_play_mote_note(mote_note_frequency(note), intensity, pan);

    if (state.mote_phrase_step == state.mote_phrase_length - 1 && (close > 0.56f || strong_alignment > 0.55f)) {
      audio_play_mote_note(mote_note_frequency(note + 2), intensity * 0.48f, pan * 0.65f - 0.18f);
    }

    ++state.mote_phrase_step;
    state.mote_phrase_timer = lerp(0.13f, 0.24f, next_random_unit());
    return;
  }

  if (state.mote_phrase_step < state.mote_phrase_length ||
      state.mote_chime_cooldown > 0.0f ||
      state.alignment < kChimeAlignmentThreshold) {
    return;
  }

  const float phrase_chance = 0.54f + strong_alignment * 0.24f + (moving_toward_objective ? 0.18f : 0.0f);
  if (next_random_unit() < phrase_chance) {
    const int root_floor = moving_toward_objective ? 2 : 0;
    const int root_ceiling = close > 0.55f ? 5 : 4;
    const int span = std::max(1, root_ceiling - root_floor);
    state.mote_phrase_root = root_floor + std::min(span - 1, static_cast<int>(next_random_unit() * static_cast<float>(span)));
    state.mote_phrase_rising = moving_toward_objective || strong_alignment > 0.35f;
    state.mote_phrase_length = moving_toward_objective ? 3 : (strong_alignment > 0.55f ? 2 : 1);
    if (close > 0.72f && next_random_unit() > 0.45f) {
      state.mote_phrase_length = std::min(4, state.mote_phrase_length + 1);
    }
    state.mote_phrase_step = 0;
    state.mote_phrase_timer = 0.0f;
  }

  const float cooldown_min = state.alignment >= kStrongChimeAlignment ? 0.82f : 1.15f;
  const float cooldown_max = moving_toward_objective ? 1.95f : 2.75f;
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
  if (!audio_ready_for_gameplay_sound()) {
    return;
  }
  if (!state.owl_appear_sound_played) {
    audio_play_owl_appear();
    state.owl_appear_sound_played = true;
  }
}

void update_footsteps(float dt, const ForestAudioPlayerState& player) {
  if (!audio_ready_for_gameplay_sound()) {
    return;
  }

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
  update_guidance_signal(*player, *world);
  update_mote_chimes(dt, *player, *world);
  update_owl(*world);
  update_footsteps(dt, *player);

  g_forest_audio.previous_distance_to_objective = g_forest_audio.distance_to_objective;
  g_forest_audio.has_previous_distance = true;
}

void forest_audio_shutdown() {
  g_forest_audio = {};
}

}  // namespace voxel
