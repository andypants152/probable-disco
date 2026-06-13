#include "owl_encounter.h"

#include <algorithm>
#include <cmath>

#include "core/audio.h"
#include "world/generator.h"
#include "core/subtitles.h"
#include "world/mesher.h"
#include "world/voxel.h"

namespace voxel {

namespace {

constexpr float kOwlEncounterRadius = 18.0f;
constexpr float kOwlDefaultHeading = 3.14159265358979323846f;
constexpr float kOwlFlyAwayHeading = 0.0f;
constexpr float kOwlTalkSeconds = 5.35f;
constexpr float kOwlFlySeconds = 2.6f;
constexpr float kOwlSecondLineTime = 1.95f;
constexpr float kOwlReturnArriveSeconds = 1.90f;
constexpr float kOwlReturnLineSeconds = 2.45f;
constexpr float kOwlReturnFlySeconds = 1.85f;
constexpr float kOwlReturnPerchHeight = 1.78f;
constexpr float kOwlReturnMinLanternDistance = 8.8f;
constexpr float kOwlReturnMaxLanternDistance = 14.2f;
constexpr float kOwlReturnFoxClearance = 5.0f;
constexpr int kOwlReturnCandidateCount = 40;
constexpr int kWorldDressingStep = 6;
constexpr int kMushroomCandidateStep = 3;
constexpr float kMushroomSpawnChance = 0.13f;
constexpr float kMushroomClusterChance = 0.24f;
constexpr float kMushroomClusterRadius = 2.35f;
constexpr int kReturnArrivalLine = 100;
constexpr int kReturnFlybyLine = 101;
constexpr float kOwlMaxHeadYaw = 3.14159265358979323846f;
constexpr float kOwlMaxHeadPitch = 0.34f;
constexpr float kOwlIdleRufflePeriod = 6.4f;
constexpr float kOwlBlinkPeriod = 4.8f;

float smoothstep(float value) {
  value = std::max(0.0f, std::min(1.0f, value));
  return value * value * (3.0f - 2.0f * value);
}

float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

float clamped(float value, float min_value, float max_value) {
  return std::max(min_value, std::min(max_value, value));
}

float shortest_angle_delta(float from, float to) {
  constexpr float kPi = 3.14159265358979323846f;
  constexpr float kTwoPi = 6.28318530717958647692f;
  float delta = to - from;
  while (delta > kPi) {
    delta -= kTwoPi;
  }
  while (delta < -kPi) {
    delta += kTwoPi;
  }
  return delta;
}

Vec3 rotate_y(Vec3 v, float heading) {
  const float s = std::sin(heading);
  const float c = std::cos(heading);
  return {
    v.x * c + v.z * s,
    v.y,
    -v.x * s + v.z * c,
  };
}

float horizontal_distance(Vec3 a, Vec3 b) {
  const float dx = b.x - a.x;
  const float dz = b.z - a.z;
  return std::sqrt(dx * dx + dz * dz);
}

std::uint32_t hash_u32(std::uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return value;
}

std::uint32_t hash2(int x, int z, std::uint32_t seed) {
  std::uint32_t h = seed;
  h ^= static_cast<std::uint32_t>(x) * 0x8da6b343u;
  h ^= static_cast<std::uint32_t>(z) * 0xd8163841u;
  return hash_u32(h);
}

float hash01(int x, int z, std::uint32_t seed) {
  return static_cast<float>(hash2(x, z, seed) & 0xffffu) / 65535.0f;
}

int floor_to_int(float value) {
  return static_cast<int>(std::floor(value));
}

int floor_div(int value, int divisor) {
  int quotient = value / divisor;
  const int remainder = value % divisor;
  if (remainder < 0) {
    --quotient;
  }
  return quotient;
}

float distance_sq(float ax, float az, float bx, float bz) {
  const float dx = ax - bx;
  const float dz = az - bz;
  return dx * dx + dz * dz;
}

float interpolated_terrain_height(const TerrainGenerator& generator, float x, float z) {
  const int x0 = floor_to_int(x);
  const int z0 = floor_to_int(z);
  const float tx = smoothstep(x - static_cast<float>(x0));
  const float tz = smoothstep(z - static_cast<float>(z0));

  const float h00 = static_cast<float>(generator.terrain_height(x0, z0));
  const float h10 = static_cast<float>(generator.terrain_height(x0 + 1, z0));
  const float h01 = static_cast<float>(generator.terrain_height(x0, z0 + 1));
  const float h11 = static_cast<float>(generator.terrain_height(x0 + 1, z0 + 1));
  return lerp(lerp(h00, h10, tx), lerp(h01, h11, tx), tz);
}

bool is_clear_ground(const TerrainGenerator& generator, float x, float z, int headroom) {
  const int cell_x = static_cast<int>(std::round(x));
  const int cell_z = static_cast<int>(std::round(z));
  const int ground_y = generator.terrain_height(cell_x, cell_z);
  if (!is_solid(generator.voxel_at(cell_x, ground_y, cell_z).type)) {
    return false;
  }
  for (int y = ground_y + 1; y <= ground_y + headroom; ++y) {
    if (is_solid(generator.voxel_at(cell_x, y, cell_z).type)) {
      return false;
    }
  }
  return true;
}

bool has_nearby_solid_above_ground(const TerrainGenerator& generator, int cell_x, int cell_z, int radius, int height) {
  for (int dz = -radius; dz <= radius; ++dz) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dz * dz > radius * radius) {
        continue;
      }
      const int sample_x = cell_x + dx;
      const int sample_z = cell_z + dz;
      const int ground_y = generator.terrain_height(sample_x, sample_z);
      for (int y = ground_y + 1; y <= ground_y + height; ++y) {
        if (is_solid(generator.voxel_at(sample_x, y, sample_z).type)) {
          return true;
        }
      }
    }
  }
  return false;
}

bool dressing_origin_for_grid(int grid_x, int grid_z, int& world_x, int& world_z, float& seed) {
  seed = hash01(grid_x, grid_z, 0xdec042u);
  const float ox = (hash01(grid_x + 300, grid_z - 300, 0xdec042u) - 0.5f) * 3.6f;
  const float oz = (hash01(grid_x - 300, grid_z + 300, 0xdec042u) - 0.5f) * 3.6f;
  world_x = grid_x + static_cast<int>(std::round(ox));
  world_z = grid_z + static_cast<int>(std::round(oz));
  return true;
}

bool near_major_dressing(float x, float z, float radius) {
  const int min_x = static_cast<int>(std::floor(x - 16.0f));
  const int max_x = static_cast<int>(std::ceil(x + 16.0f));
  const int min_z = static_cast<int>(std::floor(z - 16.0f));
  const int max_z = static_cast<int>(std::ceil(z + 16.0f));
  for (int grid_x = floor_div(min_x, kWorldDressingStep) * kWorldDressingStep;
       grid_x <= max_x;
       grid_x += kWorldDressingStep) {
    for (int grid_z = floor_div(min_z, kWorldDressingStep) * kWorldDressingStep;
         grid_z <= max_z;
         grid_z += kWorldDressingStep) {
      int world_x = 0;
      int world_z = 0;
      float seed = 0.0f;
      if (!dressing_origin_for_grid(grid_x, grid_z, world_x, world_z, seed) || seed <= 0.925f) {
        continue;
      }
      if (distance_sq(x, z, static_cast<float>(world_x), static_cast<float>(world_z)) < radius * radius) {
        return true;
      }
    }
  }
  return false;
}

float mushroom_scale_for(int world_x, int world_z, int variant) {
  const float size_seed = hash01(world_x + variant * 31, world_z - variant * 23, 0x6d757368u);
  if (hash01(world_x - variant * 11, world_z + variant * 17, 0x6d756267u) > 0.88f) {
    return 1.80f + size_seed * 0.75f;
  }
  return 1.15f + size_seed * 0.65f;
}

bool near_likely_mushroom(float x, float z, float clearance) {
  const int min_chunk_x = floor_div(floor_to_int(x - clearance - 6.0f), 16);
  const int max_chunk_x = floor_div(floor_to_int(x + clearance + 6.0f), 16);
  const int min_chunk_z = floor_div(floor_to_int(z - clearance - 6.0f), 16);
  const int max_chunk_z = floor_div(floor_to_int(z + clearance + 6.0f), 16);

  for (int chunk_z = min_chunk_z; chunk_z <= max_chunk_z; ++chunk_z) {
    for (int chunk_x = min_chunk_x; chunk_x <= max_chunk_x; ++chunk_x) {
      const int min_x = chunk_x * 16;
      const int min_z = chunk_z * 16;
      for (int grid_x = min_x + 3; grid_x < min_x + 13; grid_x += kMushroomCandidateStep) {
        for (int grid_z = min_z + 3; grid_z < min_z + 13; grid_z += kMushroomCandidateStep) {
          const float seed = hash01(grid_x, grid_z, 0x6d757368u);
          if (seed > kMushroomSpawnChance) {
            continue;
          }

          const float base_x = static_cast<float>(grid_x) +
              (hash01(grid_x + 211, grid_z - 137, 0x6d757368u) - 0.5f) * 2.3f;
          const float base_z = static_cast<float>(grid_z) +
              (hash01(grid_x - 149, grid_z + 197, 0x6d757368u) - 0.5f) * 2.3f;
          const bool cluster = hash01(grid_x + 59, grid_z - 83, 0x6d757368u) < kMushroomClusterChance;
          const int count = cluster
              ? 2 + static_cast<int>(hash01(grid_x - 71, grid_z + 43, 0x6d757368u) * 4.0f)
              : 1;

          for (int i = 0; i < count; ++i) {
            float mushroom_x = base_x;
            float mushroom_z = base_z;
            if (cluster) {
              const float angle = hash01(grid_x + i * 17, grid_z - i * 29, 0x6d757368u) * 6.2831853f;
              const float radius = (0.35f + hash01(grid_x - i * 31, grid_z + i * 23, 0x6d757368u) * 0.65f) *
                  kMushroomClusterRadius;
              mushroom_x += std::cos(angle) * radius;
              mushroom_z += std::sin(angle) * radius;
            }

            const int cell_x = static_cast<int>(std::round(mushroom_x));
            const int cell_z = static_cast<int>(std::round(mushroom_z));
            const float radius = 0.42f * mushroom_scale_for(cell_x, cell_z, i);
            if (distance_sq(x, z, mushroom_x, mushroom_z) < (clearance + radius) * (clearance + radius)) {
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

bool can_place_return_perch(const TerrainGenerator& generator,
                            Vec3 anchor_position,
                            Vec3 fox_position,
                            float x,
                            float z) {
  const Vec3 candidate = {x, 0.0f, z};
  const float anchor_distance = horizontal_distance(candidate, anchor_position);
  if (anchor_distance < kOwlReturnMinLanternDistance || anchor_distance > kOwlReturnMaxLanternDistance) {
    return false;
  }
  if (horizontal_distance(candidate, fox_position) < kOwlReturnFoxClearance) {
    return false;
  }
  if (!is_clear_ground(generator, x, z, 5)) {
    return false;
  }
  const int cell_x = static_cast<int>(std::round(x));
  const int cell_z = static_cast<int>(std::round(z));
  if (has_nearby_solid_above_ground(generator, cell_x, cell_z, 2, 5)) {
    return false;
  }
  if (near_major_dressing(x, z, 2.0f) || near_likely_mushroom(x, z, 1.8f)) {
    return false;
  }
  return true;
}

Vec3 perched_position(const TerrainGenerator& generator, float x, float z) {
  return {x, interpolated_terrain_height(generator, x, z) + kOwlReturnPerchHeight, z};
}

}  // namespace

void OwlEncounter::init(const TerrainGenerator& generator) {
  state_ = State::Waiting;
  perch_position_ = owl_perch_position(generator);
  position_ = perch_position_;
  return_perch_position_ = {};
  return_start_position_ = {};
  return_fly_away_position_ = {};
  return_line_ = "";
  heading_ = kOwlDefaultHeading;
  return_heading_ = kOwlDefaultHeading;
  wing_pose_ = 0.0f;
  head_yaw_ = 0.0f;
  head_pitch_ = 0.0f;
  head_roll_ = 0.0f;
  body_bob_ = 0.0f;
  blink_ = 0.0f;
  idle_timer_ = 0.0f;
  timer_ = 0.0f;
  dialogue_line_ = 0;
  pending_dialogue_line_ = 0;
  prompt_visible_ = false;
  return_perch_visible_ = false;
  return_completed_ = false;
}

bool OwlEncounter::update(float dt, const TerrainGenerator& generator, Vec3 fox_position, bool interact_pressed) {
  const State previous_state = state_;
  const Vec3 previous_position = position_;
  const float previous_heading = heading_;
  const float previous_wing_pose = wing_pose_;
  const float previous_head_yaw = head_yaw_;
  const float previous_head_pitch = head_pitch_;
  const float previous_head_roll = head_roll_;
  const float previous_body_bob = body_bob_;
  const float previous_blink = blink_;

  perch_position_ = owl_perch_position(generator);
  dt = std::max(0.0f, std::min(dt, 0.10f));

  if (state_ == State::Waiting) {
    position_ = perch_position_;
    heading_ = kOwlDefaultHeading;
    idle_timer_ += dt;
    timer_ = 0.0f;
    dialogue_line_ = 0;
    const bool near_owl = horizontal_distance(fox_position, position_) <= kOwlEncounterRadius;
    prompt_visible_ = near_owl;
    if (near_owl && !subtitles_visible()) {
      subtitles_show("Press A to talk", 0.45f);
    }
    if (interact_pressed && audio_ready_for_gameplay_sound() && near_owl) {
      state_ = State::Talking;
      prompt_visible_ = false;
      dialogue_line_ = 1;
      pending_dialogue_line_ = 1;
    }
  } else if (state_ == State::Talking) {
    position_ = perch_position_;
    heading_ = kOwlDefaultHeading;
    idle_timer_ += dt;
    timer_ += dt;
    if (dialogue_line_ == 1 && timer_ >= kOwlSecondLineTime) {
      dialogue_line_ = 2;
      pending_dialogue_line_ = 2;
    }
    if (timer_ >= kOwlTalkSeconds) {
      state_ = State::Flying;
      timer_ = 0.0f;
    }
  } else if (state_ == State::Flying) {
    timer_ += dt;
    const float raw_t = std::max(0.0f, std::min(1.0f, timer_ / kOwlFlySeconds));
    const float t = smoothstep(raw_t);
    const float turn_t = smoothstep(std::max(0.0f, std::min(1.0f, raw_t / 0.35f)));
    position_ = perch_position_ + Vec3{-2.0f * t, 1.35f * t + 4.8f * t * t, -13.0f * t};
    heading_ = lerp(kOwlDefaultHeading, kOwlFlyAwayHeading, turn_t);
    wing_pose_ = 0.25f + 0.75f * std::fabs(std::sin(timer_ * 18.0f));
    body_bob_ = 0.0f;
    blink_ = 0.0f;
    head_roll_ = 0.0f;
    if (timer_ >= kOwlFlySeconds) {
      state_ = State::Gone;
      wing_pose_ = 0.0f;
    }
  } else if (state_ == State::ReturnArriving) {
    timer_ += dt;
    const float raw_t = std::max(0.0f, std::min(1.0f, timer_ / kOwlReturnArriveSeconds));
    const float t = smoothstep(raw_t);
    const Vec3 landing = return_perch_position_;
    position_ = return_start_position_ + (landing - return_start_position_) * t +
        Vec3{0.0f, std::sin(raw_t * 3.14159265358979323846f) * 1.35f, 0.0f};
    heading_ = return_heading_;
    wing_pose_ = 0.34f + 0.66f * std::fabs(std::sin(timer_ * 18.0f));
    body_bob_ = 0.0f;
    blink_ = 0.0f;
    head_roll_ = 0.0f;
    if (timer_ >= kOwlReturnArriveSeconds) {
      state_ = State::ReturnTalking;
      timer_ = 0.0f;
      position_ = return_perch_position_;
      wing_pose_ = 0.0f;
      dialogue_line_ = kReturnFlybyLine;
      pending_dialogue_line_ = kReturnFlybyLine;
    }
  } else if (state_ == State::ReturnTalking) {
    position_ = return_perch_position_;
    heading_ = return_heading_;
    idle_timer_ += dt;
    timer_ += dt;
    if (timer_ >= kOwlReturnLineSeconds) {
      state_ = State::ReturnFlying;
      timer_ = 0.0f;
      dialogue_line_ = 0;
    }
  } else if (state_ == State::ReturnFlying) {
    timer_ += dt;
    const float raw_t = std::max(0.0f, std::min(1.0f, timer_ / kOwlReturnFlySeconds));
    const float t = smoothstep(raw_t);
    position_ = return_perch_position_ + (return_fly_away_position_ - return_perch_position_) * t +
        Vec3{0.0f, 2.7f * t * t, 0.0f};
    heading_ = lerp(return_heading_, kOwlFlyAwayHeading, smoothstep(raw_t));
    wing_pose_ = 0.30f + 0.70f * std::fabs(std::sin(timer_ * 18.0f));
    body_bob_ = 0.0f;
    blink_ = 0.0f;
    head_roll_ = 0.0f;
    if (timer_ >= kOwlReturnFlySeconds) {
      state_ = State::Gone;
      return_perch_visible_ = false;
      return_completed_ = true;
      wing_pose_ = 0.0f;
    }
  }

  if (state_ == State::Waiting || state_ == State::Talking ||
      state_ == State::ReturnTalking) {
    const float breath = 0.5f + 0.5f * std::sin(idle_timer_ * 1.35f);
    body_bob_ = 0.012f + 0.026f * breath;
    head_roll_ = std::sin(idle_timer_ * 0.78f + 0.65f) * 0.025f;

    const float blink_phase = std::fmod(idle_timer_ + 1.25f, kOwlBlinkPeriod);
    blink_ = blink_phase < 0.16f
        ? std::sin((blink_phase / 0.16f) * 3.14159265358979323846f)
        : 0.0f;

    const float ruffle_phase = std::fmod(idle_timer_ + 2.2f, kOwlIdleRufflePeriod);
    float ruffle = 0.0f;
    if (ruffle_phase < 0.52f) {
      const float t = ruffle_phase / 0.52f;
      ruffle = std::sin(t * 3.14159265358979323846f) * (0.55f + 0.45f * std::sin(t * 3.14159265358979323846f * 5.0f));
    }
    wing_pose_ = ruffle * 0.18f;
  } else if (state_ == State::Gone) {
    body_bob_ = 0.0f;
    blink_ = 0.0f;
    head_roll_ = 0.0f;
  }

  float target_head_yaw = 0.0f;
  float target_head_pitch = 0.0f;
  if (state_ == State::Waiting || state_ == State::Talking ||
      state_ == State::ReturnTalking) {
    const Vec3 head_position = position_ + Vec3{0.0f, 1.16f, 0.0f};
    const Vec3 to_fox = fox_position + Vec3{0.0f, 1.0f, 0.0f} - head_position;
    const Vec3 local_to_fox = rotate_y(to_fox, -heading_);
    const float horizontal = std::sqrt(local_to_fox.x * local_to_fox.x + local_to_fox.z * local_to_fox.z);
    if (horizontal > 0.001f) {
      target_head_yaw = clamped(std::atan2(-local_to_fox.x, -local_to_fox.z),
                                -kOwlMaxHeadYaw,
                                kOwlMaxHeadYaw);
      target_head_pitch = clamped(std::atan2(local_to_fox.y, horizontal),
                                  -kOwlMaxHeadPitch,
                                  kOwlMaxHeadPitch);
    }
  }
  const float head_follow_t = smoothstep(clamped(dt * 6.5f, 0.0f, 1.0f));
  head_yaw_ += shortest_angle_delta(head_yaw_, target_head_yaw) * head_follow_t;
  const float idle_head_nod =
      (state_ == State::Waiting || state_ == State::Talking) ? std::sin(idle_timer_ * 1.08f) * 0.025f : 0.0f;
  head_pitch_ = lerp(head_pitch_, target_head_pitch + idle_head_nod, head_follow_t);

  return previous_state != state_ ||
      length(previous_position - position_) > 0.0005f ||
      std::fabs(previous_heading - heading_) > 0.0005f ||
      std::fabs(previous_wing_pose - wing_pose_) > 0.0005f ||
      std::fabs(previous_head_yaw - head_yaw_) > 0.0005f ||
      std::fabs(previous_head_pitch - head_pitch_) > 0.0005f ||
      std::fabs(previous_head_roll - head_roll_) > 0.0005f ||
      std::fabs(previous_body_bob - body_bob_) > 0.0005f ||
      std::fabs(previous_blink - blink_) > 0.0005f;
}

bool OwlEncounter::schedule_return(const TerrainGenerator& generator,
                                   Vec3 anchor_position,
                                   Vec3 fox_position,
                                   const char* line) {
  if (state_ != State::Gone || return_perch_visible_ || line == nullptr || line[0] == '\0') {
    return false;
  }

  const int anchor_x = static_cast<int>(std::round(anchor_position.x));
  const int anchor_z = static_cast<int>(std::round(anchor_position.z));
  float best_score = -100000.0f;
  Vec3 best_position = {};
  for (int i = 0; i < kOwlReturnCandidateCount; ++i) {
    const float angle = hash01(anchor_x + i * 37, anchor_z - i * 19, 0x0f11cafeu) * 6.28318530718f;
    const float radius = kOwlReturnMinLanternDistance +
        hash01(anchor_x - i * 29, anchor_z + i * 41, 0x0f11f00du) *
            (kOwlReturnMaxLanternDistance - kOwlReturnMinLanternDistance);
    const float x = anchor_position.x + std::cos(angle) * radius;
    const float z = anchor_position.z + std::sin(angle) * radius;
    if (!can_place_return_perch(generator, anchor_position, fox_position, x, z)) {
      continue;
    }
    const Vec3 candidate = perched_position(generator, x, z);
    const float fox_distance = horizontal_distance(candidate, fox_position);
    const float score = 1.0f - std::fabs(radius - 11.2f) * 0.08f +
        std::min(fox_distance, 22.0f) * 0.015f +
        hash01(static_cast<int>(std::round(x)), static_cast<int>(std::round(z)), 0x0f114441u) * 0.18f;
    if (score > best_score) {
      best_score = score;
      best_position = candidate;
    }
  }

  if (best_score < -99999.0f) {
    return false;
  }

  return_perch_position_ = best_position;
  return_line_ = line;
  return_heading_ = std::atan2(fox_position.x - return_perch_position_.x,
                               fox_position.z - return_perch_position_.z);
  const Vec3 away_from_fox = normalize(Vec3{
      return_perch_position_.x - fox_position.x,
      0.0f,
      return_perch_position_.z - fox_position.z,
  });
  const Vec3 arrival_direction = length(away_from_fox) > 0.001f ? away_from_fox : Vec3{0.0f, 0.0f, -1.0f};
  return_start_position_ = return_perch_position_ + arrival_direction * 11.0f + Vec3{0.0f, 4.2f, 0.0f};
  return_fly_away_position_ = return_perch_position_ + arrival_direction * 10.0f + Vec3{0.0f, 2.4f, 0.0f};
  position_ = return_start_position_;
  heading_ = return_heading_;
  state_ = State::ReturnArriving;
  timer_ = 0.0f;
  dialogue_line_ = kReturnArrivalLine;
  pending_dialogue_line_ = kReturnArrivalLine;
  prompt_visible_ = false;
  return_perch_visible_ = true;
  return_completed_ = false;
  return true;
}

bool OwlEncounter::consume_dialogue_event(DialogueEvent& event) {
  if (pending_dialogue_line_ == 0) {
    return false;
  }

  event = {};
  event.line = pending_dialogue_line_;
  event.target_position = position_;
  if (pending_dialogue_line_ == 1) {
    event.text = "Oh good, you're awake.";
    event.seconds = kOwlSecondLineTime;
  } else if (pending_dialogue_line_ == 2) {
    event.text = "The little lights are scattered. Bring them home.";
    event.seconds = 3.35f;
  } else if (pending_dialogue_line_ == kReturnArrivalLine) {
    event.text = "";
    event.seconds = kOwlReturnArriveSeconds;
    event.show_subtitle = false;
    event.target_position = return_perch_position_;
  } else if (pending_dialogue_line_ == kReturnFlybyLine) {
    event.text = return_line_;
    event.seconds = kOwlReturnLineSeconds;
    event.target_position = return_perch_position_;
  }
  pending_dialogue_line_ = 0;
  return true;
}

float OwlEncounter::perch_heading() const {
  return kOwlDefaultHeading;
}

}  // namespace voxel
