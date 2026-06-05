#include "squirrel_quest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/audio.h"
#include "game/firefly_loop.h"
#include "world/generator.h"
#include "world/mesher.h"
#include "world/voxel.h"

namespace voxel {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kSquirrelDiscoverRadius = 104.0f;
constexpr float kLitLanternAcornRadius = 15.5f;
constexpr float kLitLanternSquirrelRadius = 8.2f;
constexpr float kSquirrelRenderDistance = 46.0f;
constexpr float kAcornRenderDistance = 36.0f;
constexpr float kSquirrelInteractRadius = 3.9f;
constexpr float kSquirrelAutoTalkRadius = 10.5f;
constexpr float kAcornPickupRadius = 1.25f;
constexpr float kRewardBurstSeconds = 2.4f;
constexpr float kSquirrelApproachSeconds = 2.65f;
constexpr float kSquirrelApproachDistance = 17.5f;
constexpr int kAcornSlotsPerLitLantern = 14;
constexpr int kMaxVisibleSquirrels = 18;
constexpr int kMaxVisibleAcorns = 46;
constexpr int kWorldDressingStep = 6;
constexpr float kMoonClearingX = 42.0f;
constexpr float kMoonClearingZ = -104.0f;
constexpr float kMoonClearingRadius = 16.0f;

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

float signed_hash01(int x, int z, std::uint32_t seed) {
  return hash01(x, z, seed) * 2.0f - 1.0f;
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

float smoothstep(float value) {
  value = std::max(0.0f, std::min(1.0f, value));
  return value * value * (3.0f - 2.0f * value);
}

float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

float horizontal_distance(Vec3 a, Vec3 b) {
  const float dx = b.x - a.x;
  const float dz = b.z - a.z;
  return std::sqrt(dx * dx + dz * dz);
}

float distance_sq(float ax, float az, float bx, float bz) {
  const float dx = ax - bx;
  const float dz = az - bz;
  return dx * dx + dz * dz;
}

Vec3 horizontal_direction(Vec3 from, Vec3 to, Vec3 fallback) {
  Vec3 direction = {to.x - from.x, 0.0f, to.z - from.z};
  if (length(direction) <= 0.001f) {
    direction = {fallback.x, 0.0f, fallback.z};
  }
  if (length(direction) <= 0.001f) {
    return {1.0f, 0.0f, 0.0f};
  }
  return normalize(direction);
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

bool inside_moon_clearing(float x, float z) {
  return distance_sq(x, z, kMoonClearingX, kMoonClearingZ) <
      kMoonClearingRadius * kMoonClearingRadius;
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

bool has_nearby_tree(const TerrainGenerator& generator, int cell_x, int cell_z, int radius) {
  for (int dz = -radius; dz <= radius; ++dz) {
    for (int dx = -radius; dx <= radius; ++dx) {
      const int sample_x = cell_x + dx;
      const int sample_z = cell_z + dz;
      const int ground_y = generator.terrain_height(sample_x, sample_z);
      for (int y = ground_y + 1; y <= ground_y + 4; ++y) {
        if (generator.voxel_at(sample_x, y, sample_z).type == VoxelType::Bark) {
          return true;
        }
      }
    }
  }
  return false;
}

bool has_nearby_solid_above_ground(const TerrainGenerator& generator, int cell_x, int cell_z, int radius, int height) {
  const int ground_y = generator.terrain_height(cell_x, cell_z);
  for (int dz = -radius; dz <= radius; ++dz) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dz * dz > radius * radius) {
        continue;
      }
      for (int y = ground_y + 1; y <= ground_y + height; ++y) {
        if (is_solid(generator.voxel_at(cell_x + dx, y, cell_z + dz).type)) {
          return true;
        }
      }
    }
  }
  return false;
}

bool find_tree_root_site(const TerrainGenerator& generator,
                         int base_x,
                         int base_z,
                         int search_radius,
                         Vec3& position,
                         float& heading) {
  float best_score = -1.0f;
  int best_x = 0;
  int best_z = 0;
  int best_trunk_x = 0;
  int best_trunk_z = 0;

  for (int dz = -search_radius; dz <= search_radius; ++dz) {
    for (int dx = -search_radius; dx <= search_radius; ++dx) {
      const int trunk_x = base_x + dx;
      const int trunk_z = base_z + dz;
      const int ground_y = generator.terrain_height(trunk_x, trunk_z);
      if (generator.voxel_at(trunk_x, ground_y + 1, trunk_z).type != VoxelType::Bark) {
        continue;
      }

      constexpr int kOffsetCount = 8;
      constexpr int offsets[kOffsetCount][2] = {
          {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1},
      };
      for (int i = 0; i < kOffsetCount; ++i) {
        const int x = trunk_x + offsets[i][0];
        const int z = trunk_z + offsets[i][1];
        if (!is_clear_ground(generator, static_cast<float>(x), static_cast<float>(z), 3)) {
          continue;
        }
        const float score = hash01(x, z, 0x71517172u) -
            0.002f * distance_sq(static_cast<float>(x), static_cast<float>(z),
                                 static_cast<float>(base_x), static_cast<float>(base_z));
        if (score > best_score) {
          best_score = score;
          best_x = x;
          best_z = z;
          best_trunk_x = trunk_x;
          best_trunk_z = trunk_z;
        }
      }
    }
  }

  if (best_score < 0.0f) {
    return false;
  }

  const float jitter_x = signed_hash01(best_x, best_z, 0x726f6f74u) * 0.18f;
  const float jitter_z = signed_hash01(best_x, best_z, 0x73697465u) * 0.18f;
  position = {
      static_cast<float>(best_x) + 0.5f + jitter_x,
      interpolated_terrain_height(generator, static_cast<float>(best_x) + 0.5f, static_cast<float>(best_z) + 0.5f) + 1.0f,
      static_cast<float>(best_z) + 0.5f + jitter_z,
  };
  heading = std::atan2(static_cast<float>(best_trunk_x - best_x), static_cast<float>(best_trunk_z - best_z));
  return true;
}

bool dressing_origin_for_grid(int grid_x, int grid_z, int& world_x, int& world_z, float& seed) {
  seed = hash01(grid_x, grid_z, 0xdec042u);
  const float ox = (hash01(grid_x + 300, grid_z - 300, 0xdec042u) - 0.5f) * 3.6f;
  const float oz = (hash01(grid_x - 300, grid_z + 300, 0xdec042u) - 0.5f) * 3.6f;
  world_x = grid_x + static_cast<int>(std::round(ox));
  world_z = grid_z + static_cast<int>(std::round(oz));
  return !inside_moon_clearing(static_cast<float>(world_x), static_cast<float>(world_z));
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

bool find_dressing_perch(const TerrainGenerator& generator, int base_x, int base_z, Vec3& position, float& heading) {
  float best_score = -1.0f;
  int best_x = 0;
  int best_z = 0;
  float best_seed = 0.0f;
  for (int grid_x = base_x - 18; grid_x <= base_x + 18; grid_x += kWorldDressingStep) {
    for (int grid_z = base_z - 18; grid_z <= base_z + 18; grid_z += kWorldDressingStep) {
      int world_x = 0;
      int world_z = 0;
      float seed = 0.0f;
      if (!dressing_origin_for_grid(grid_x, grid_z, world_x, world_z, seed) || seed <= 0.925f || seed > 0.965f) {
        continue;
      }
      const float score = seed - 0.001f * distance_sq(static_cast<float>(world_x), static_cast<float>(world_z),
                                                      static_cast<float>(base_x), static_cast<float>(base_z));
      if (score > best_score) {
        best_score = score;
        best_x = world_x;
        best_z = world_z;
        best_seed = seed;
      }
    }
  }

  if (best_score < 0.0f) {
    return false;
  }

  const float x = static_cast<float>(best_x);
  const float z = static_cast<float>(best_z);
  const float ground = static_cast<float>(generator.terrain_height(best_x, best_z));
  heading = best_seed > 0.5f ? kPi * 0.5f : 0.0f;
  if (best_seed > 0.94f) {
    const float height = 0.45f + hash01(best_x + 23, best_z - 17, 0x7374756du) * 0.35f;
    position = {x, ground + 1.0f + height + 0.08f, z};
  } else {
    position = {x, ground + 1.0f + 0.66f, z};
  }
  return true;
}

bool find_lit_lantern_squirrel_site(const TerrainGenerator& generator,
                                    Vec3 lantern_position,
                                    int lantern_index,
                                    Vec3& position,
                                    float& heading) {
  const int lantern_x = static_cast<int>(std::round(lantern_position.x));
  const int lantern_z = static_cast<int>(std::round(lantern_position.z));
  float best_score = -1.0f;

  for (int i = 0; i < 14; ++i) {
    const float angle = hash01(lantern_x + i * 31, lantern_z - i * 17, 0x73717131u) * kTwoPi;
    const float radius = 3.4f + hash01(lantern_x - i * 13, lantern_z + i * 19, 0x73717132u) *
        (kLitLanternSquirrelRadius - 3.4f);
    const float x = lantern_position.x + std::cos(angle) * radius;
    const float z = lantern_position.z + std::sin(angle) * radius;
    if (!is_clear_ground(generator, x, z, 3)) {
      continue;
    }
    const int cell_x = static_cast<int>(std::round(x));
    const int cell_z = static_cast<int>(std::round(z));
    if (has_nearby_solid_above_ground(generator, cell_x, cell_z, 1, 3) ||
        near_major_dressing(x, z, 1.25f)) {
      continue;
    }
    const float score = 1.0f - std::fabs(radius - 5.5f) * 0.08f +
        hash01(cell_x + lantern_index * 7, cell_z - lantern_index * 11, 0x73717133u) * 0.18f;
    if (score > best_score) {
      best_score = score;
      position = {x, interpolated_terrain_height(generator, x, z) + 1.0f, z};
    }
  }

  if (best_score < 0.0f) {
    return false;
  }

  heading = std::atan2(lantern_position.x - position.x, lantern_position.z - position.z);
  return true;
}

Vec3 squirrel_approach_start(const TerrainGenerator& generator, Vec3 lantern_position, Vec3 home_position) {
  Vec3 away_from_light = horizontal_direction(lantern_position, home_position, {1.0f, 0.0f, 0.0f});
  for (int i = 0; i < 7; ++i) {
    const float radius = kSquirrelApproachDistance - static_cast<float>(i) * 1.25f;
    const float x = lantern_position.x + away_from_light.x * radius;
    const float z = lantern_position.z + away_from_light.z * radius;
    const int cell_x = static_cast<int>(std::round(x));
    const int cell_z = static_cast<int>(std::round(z));
    if (!is_clear_ground(generator, x, z, 3) ||
        has_nearby_solid_above_ground(generator, cell_x, cell_z, 1, 3) ||
        near_major_dressing(x, z, 1.2f)) {
      continue;
    }
    return {x, interpolated_terrain_height(generator, x, z) + 1.0f, z};
  }

  const float fallback_x = home_position.x - away_from_light.x * 7.5f;
  const float fallback_z = home_position.z - away_from_light.z * 7.5f;
  return {fallback_x, interpolated_terrain_height(generator, fallback_x, fallback_z) + 1.0f, fallback_z};
}

bool near_likely_mushroom(const TerrainGenerator& generator, float x, float z) {
  const int min_x = static_cast<int>(std::floor(x - 5.0f));
  const int max_x = static_cast<int>(std::ceil(x + 5.0f));
  const int min_z = static_cast<int>(std::floor(z - 5.0f));
  const int max_z = static_cast<int>(std::ceil(z + 5.0f));
  for (int gx = floor_div(min_x, 3) * 3; gx <= max_x; gx += 3) {
    for (int gz = floor_div(min_z, 3) * 3; gz <= max_z; gz += 3) {
      const float seed = hash01(gx, gz, 0x6d757368u);
      if (seed > 0.13f) {
        continue;
      }
      const float base_x = static_cast<float>(gx) + (hash01(gx + 211, gz - 137, 0x6d757368u) - 0.5f) * 2.3f;
      const float base_z = static_cast<float>(gz) + (hash01(gx - 149, gz + 197, 0x6d757368u) - 0.5f) * 2.3f;
      if (distance_sq(x, z, base_x, base_z) < 1.15f) {
        if (is_clear_ground(generator, base_x, base_z, 3)) {
          return true;
        }
      }
    }
  }
  return false;
}

bool can_place_acorn_at(const TerrainGenerator& generator, const FireflyLoop& firefly_loop, float x, float z) {
  if (inside_moon_clearing(x, z)) {
    return false;
  }
  const Vec3 position = {x, interpolated_terrain_height(generator, x, z) + 1.0f, z};
  if (!firefly_loop.has_lit_lantern_near(position, kLitLanternAcornRadius)) {
    return false;
  }
  if (!is_clear_ground(generator, x, z, 2)) {
    return false;
  }
  const int cell_x = static_cast<int>(std::round(x));
  const int cell_z = static_cast<int>(std::round(z));
  if (!has_nearby_tree(generator, cell_x, cell_z, 8) &&
      !firefly_loop.has_lit_lantern_near(position, kLitLanternAcornRadius * 0.72f)) {
    return false;
  }
  if (has_nearby_solid_above_ground(generator, cell_x, cell_z, 1, 3)) {
    return false;
  }
  if (near_major_dressing(x, z, 1.35f) || near_likely_mushroom(generator, x, z)) {
    return false;
  }
  if (firefly_loop.blocks_acorn_spawn(position, 3.2f)) {
    return false;
  }
  return true;
}

void add_gameplay_light(std::array<GameplayLight, kMaxGameplayLights>& lights,
                        int& light_count,
                        int light_limit,
                        Vec3 position,
                        Vec3 color,
                        float radius,
                        float intensity) {
  if (light_count >= light_limit ||
      light_count >= static_cast<int>(lights.size()) ||
      intensity <= 0.0f ||
      radius <= 0.0f) {
    return;
  }
  GameplayLight& light = lights[static_cast<std::size_t>(light_count++)];
  light.position = position;
  light.color = color;
  light.radius = radius;
  light.intensity = intensity;
  light.active = true;
}

}  // namespace

void SquirrelQuest::init(const TerrainGenerator& generator, const FireflyLoop& firefly_loop) {
  squirrels_.clear();
  acorns_.clear();
  progress_.clear();
  known_squirrel_ids_.clear();
  known_acorn_ids_.clear();
  collected_acorn_ids_.clear();
  dialogue_events_.clear();
  completion_events_.clear();
  active_squirrel_id_ = 0;
  carried_acorns_ = 0;
  refresh_timer_ = 0.0f;
  refresh_nearby(generator, firefly_loop, {});
}

bool SquirrelQuest::update(float dt,
                           const TerrainGenerator& generator,
                           const FireflyLoop& firefly_loop,
                           Vec3 fox_position,
                           bool allow_dialogue) {
  bool changed = false;
  dt = std::max(0.0f, std::min(dt, 0.10f));
  refresh_timer_ -= dt;
  if (refresh_timer_ <= 0.0f) {
    const std::size_t squirrel_count = squirrels_.size();
    const std::size_t acorn_count = acorns_.size();
    refresh_nearby(generator, firefly_loop, fox_position);
    changed = changed || squirrel_count != squirrels_.size() || acorn_count != acorns_.size();
    refresh_timer_ = 0.85f;
  }

  for (Squirrel& squirrel : squirrels_) {
    const float distance = std::min(horizontal_distance(fox_position, squirrel.home),
                                    horizontal_distance(fox_position, squirrel.position));
    const bool close = distance <= kSquirrelRenderDistance;
    const bool talking = squirrel.id == talking_squirrel_id_;
    const bool approaching = squirrel.approach_timer < squirrel.approach_duration;
    if (!close && squirrel.happy_timer <= 0.0f && !talking && !approaching) {
      continue;
    }

    const Vec3 previous_position = squirrel.position;
    const float previous_happy = squirrel.happy_timer;
    const bool was_approaching = approaching;
    squirrel.animation_timer += dt;
    squirrel.happy_timer = std::max(0.0f, squirrel.happy_timer - dt);
    if (approaching) {
      squirrel.approach_timer = std::min(squirrel.approach_duration, squirrel.approach_timer + dt);
      const float t = smoothstep(squirrel.approach_timer / std::max(0.001f, squirrel.approach_duration));
      const float x = lerp(squirrel.approach_start.x, squirrel.home.x, t);
      const float z = lerp(squirrel.approach_start.z, squirrel.home.z, t);
      const float hop = std::fabs(std::sin(squirrel.approach_timer * 12.5f)) * 0.34f * (1.0f - 0.22f * t);
      squirrel.position = {x, interpolated_terrain_height(generator, x, z) + 1.0f + hop, z};
      squirrel.heading = squirrel.approach_timer < squirrel.approach_duration
          ? std::atan2(squirrel.home.x - squirrel.position.x, squirrel.home.z - squirrel.position.z)
          : squirrel.home_heading;
    } else {
      const float hop = talking
          ? (std::sin(squirrel.animation_timer * 9.0f) > 0.70f ? 1.0f : 0.0f)
          : (std::sin(squirrel.animation_timer * 2.1f + static_cast<float>(squirrel.id & 7u)) > 0.985f
          ? 1.0f
          : 0.0f);
      squirrel.position = squirrel.home + Vec3{0.0f, hop * 0.08f, 0.0f};
      squirrel.heading = squirrel.home_heading;
    }
    QuestProgress& progress = progress_for(squirrel.id);
    const bool arrived = was_approaching && squirrel.approach_timer >= squirrel.approach_duration;
    if (allow_dialogue &&
        !progress.completed &&
        !progress.greeted &&
        (arrived || !was_approaching) &&
        horizontal_distance(fox_position, squirrel.position) <= kSquirrelAutoTalkRadius) {
      DialogueEvent event = {};
      event.squirrel_position = squirrel.position;
      event.squirrel_id = squirrel.id;
      std::snprintf(event.text,
                    sizeof(event.text),
                    "Could you help me find %d acorns?",
                    progress.required_acorns);
      event.seconds = 2.45f;
      dialogue_events_.push_back(event);
      progress.greeted = true;
      active_squirrel_id_ = squirrel.id;
    }
    changed = changed || length(previous_position - squirrel.position) > 0.0005f || previous_happy != squirrel.happy_timer;
  }

  const std::uint32_t pickup_squirrel_id = active_squirrel_id_ != 0
      ? active_squirrel_id_
      : best_incomplete_squirrel_id(fox_position);
  for (Acorn& acorn : acorns_) {
    if (!acorn.active || collected_acorn_ids_.find(acorn.id) != collected_acorn_ids_.end()) {
      continue;
    }

    const float distance = horizontal_distance(fox_position, acorn.position);
    const bool close = distance <= kAcornRenderDistance;
    if (close) {
      acorn.phase += dt;
      const float bob = std::sin(acorn.phase * 2.3f) * 0.10f;
      acorn.position = acorn.home + Vec3{0.0f, bob, 0.0f};
      changed = true;
    }
    if (pickup_squirrel_id != 0 && distance <= kAcornPickupRadius) {
      QuestProgress& progress = progress_for(pickup_squirrel_id);
      if (!progress.completed && progress.collected_acorns < progress.required_acorns) {
        collected_acorn_ids_.insert(acorn.id);
        acorn.active = false;
        ++carried_acorns_;
        ++progress.collected_acorns;
        active_squirrel_id_ = pickup_squirrel_id;
        changed = true;
        if (audio_ready_for_gameplay_sound()) {
          audio_play_mote_chime(0.58f);
        }
        if (progress.collected_acorns >= progress.required_acorns) {
          progress.completed = true;
          carried_acorns_ = std::max(0, carried_acorns_ - progress.required_acorns);
          Vec3 completion_position = fox_position;
          std::uint32_t completion_squirrel_id = pickup_squirrel_id;
          for (Squirrel& squirrel : squirrels_) {
            if (squirrel.id != pickup_squirrel_id) {
              continue;
            }
            squirrel.happy_timer = kRewardBurstSeconds;
            completion_position = squirrel.home;
            completion_squirrel_id = squirrel.id;
            break;
          }
          CompletionEvent event = {};
          event.position = completion_position;
          event.squirrel_position = completion_position;
          event.squirrel_id = completion_squirrel_id;
          std::snprintf(event.text, sizeof(event.text), "Thank you! The forest remembers.");
          event.seconds = 2.4f;
          completion_events_.push_back(event);
          if (audio_ready_for_gameplay_sound()) {
            audio_play_mote_chime(1.0f);
            audio_play_owl_appear();
          }
        }
      }
    }
  }

  return changed;
}

bool SquirrelQuest::conversation_request(Vec3 fox_position, ConversationRequest& request) {
  Squirrel* squirrel = nearest_squirrel(fox_position, kSquirrelInteractRadius);
  if (squirrel == nullptr) {
    return false;
  }

  QuestProgress& progress = progress_for(squirrel->id);
  active_squirrel_id_ = squirrel->id;
  request = {};
  request.squirrel_position = squirrel->position;
  request.squirrel_id = squirrel->id;
  if (progress.completed) {
    std::snprintf(request.text, sizeof(request.text), "Thank you! The forest remembers.");
    request.seconds = 2.4f;
  } else {
    std::snprintf(request.text,
                  sizeof(request.text),
                  "Could you help me find %d acorns?",
                  progress.required_acorns);
    request.seconds = 2.45f;
  }
  progress.greeted = true;
  return true;
}

void SquirrelQuest::set_talking_squirrel(std::uint32_t squirrel_id, bool talking) {
  talking_squirrel_id_ = talking ? squirrel_id : 0;
}

void SquirrelQuest::append_dynamic_mesh(Mesh& mesh, Vec3 fox_position) const {
  int squirrel_count = 0;
  for (const Squirrel& squirrel : squirrels_) {
    if (squirrel_count >= kMaxVisibleSquirrels) {
      break;
    }
    const float distance = std::min(horizontal_distance(fox_position, squirrel.home),
                                    horizontal_distance(fox_position, squirrel.position));
    if (distance > kSquirrelRenderDistance && squirrel.happy_timer <= 0.0f) {
      continue;
    }
    const QuestProgress* progress = progress_for(squirrel.id);
    const bool happy = squirrel.happy_timer > 0.0f || (progress != nullptr && progress->completed);
    const bool talking = squirrel.id == talking_squirrel_id_;
    const bool approaching = squirrel.approach_timer < squirrel.approach_duration;
    const float timer = squirrel.animation_timer + static_cast<float>(squirrel.id & 31u) * 0.17f;
    const float tail = approaching
        ? std::sin(timer * 10.5f)
        : (talking ? std::sin(timer * 11.0f) : std::sin(timer * (happy ? 8.2f : 3.4f)));
    const float head = talking
        ? std::sin(timer * 7.6f) * 0.85f
        : std::sin(timer * 0.72f + 1.3f);
    const float hop = approaching ? 0.0f : (talking
        ? (std::sin(timer * 8.8f) > 0.76f ? 1.0f : 0.0f)
        : (happy ? std::fabs(std::sin(timer * 5.4f)) : (std::sin(timer * 1.9f) > 0.982f ? 1.0f : 0.0f)));
    append_squirrel_mesh(mesh, squirrel.position, squirrel.heading, tail, head, hop, happy);

    if (happy) {
      const float reward_t = squirrel.happy_timer > 0.0f
          ? 1.0f - squirrel.happy_timer / kRewardBurstSeconds
          : 1.0f;
      const int heart_count = squirrel.happy_timer > 0.0f ? 3 : 1;
      for (int i = 0; i < heart_count; ++i) {
        const float seed = static_cast<float>(i) * 1.931f + static_cast<float>(squirrel.id % 19u) * 0.13f;
        const float drift = squirrel.happy_timer > 0.0f ? reward_t : std::sin(timer * 0.52f + seed) * 0.5f + 0.5f;
        const float angle = seed + timer * 0.18f;
        const float radius = heart_count > 1 ? 0.22f + 0.48f * static_cast<float>(i) : 0.0f;
        const Vec3 heart_position = squirrel.home + Vec3{
            std::cos(angle) * radius,
            2.12f + 0.55f * drift + 0.16f * std::sin(timer * 1.4f + seed),
            std::sin(angle) * radius,
        };
        append_heart_mesh(mesh, heart_position, i == 0 ? 0.92f : 0.72f, std::sin(timer * 4.0f + seed) * 0.5f + 0.5f);
      }
    }
    ++squirrel_count;
  }

  int acorn_count = 0;
  for (const Acorn& acorn : acorns_) {
    if (acorn_count >= kMaxVisibleAcorns) {
      break;
    }
    if (!acorn.active || collected_acorn_ids_.find(acorn.id) != collected_acorn_ids_.end()) {
      continue;
    }
    if (horizontal_distance(fox_position, acorn.position) > kAcornRenderDistance) {
      continue;
    }
    append_acorn_mesh(mesh, acorn.position);
    ++acorn_count;
  }
}

void SquirrelQuest::append_gameplay_lights(std::array<GameplayLight, kMaxGameplayLights>& lights,
                                           int& light_count,
                                           int light_limit,
                                           Vec3 fox_position) const {
  const Vec3 burst_color = {0.86f, 1.0f, 0.46f};
  for (const Squirrel& squirrel : squirrels_) {
    if (squirrel.happy_timer <= 0.0f) {
      continue;
    }
    const float t = squirrel.happy_timer / kRewardBurstSeconds;
    add_gameplay_light(lights, light_count, light_limit, squirrel.home + Vec3{0.0f, 1.7f, 0.0f},
                       burst_color, 5.0f, 0.42f * t);
  }
}

int SquirrelQuest::active_collected_acorns() const {
  const QuestProgress* progress = progress_for(active_squirrel_id_);
  return progress != nullptr ? progress->collected_acorns : 0;
}

int SquirrelQuest::active_required_acorns() const {
  const QuestProgress* progress = progress_for(active_squirrel_id_);
  return progress != nullptr ? progress->required_acorns : kRequiredAcorns;
}

bool SquirrelQuest::active_quest_needs_acorns() const {
  const QuestProgress* progress = progress_for(active_squirrel_id_);
  return progress != nullptr && !progress->completed;
}

int SquirrelQuest::completed_squirrels() const {
  int count = 0;
  for (const auto& entry : progress_) {
    if (entry.second.completed) {
      ++count;
    }
  }
  return count;
}

void SquirrelQuest::drain_dialogue_events(std::vector<DialogueEvent>& events) {
  events.insert(events.end(), dialogue_events_.begin(), dialogue_events_.end());
  dialogue_events_.clear();
}

void SquirrelQuest::drain_completion_events(std::vector<CompletionEvent>& events) {
  events.insert(events.end(), completion_events_.begin(), completion_events_.end());
  completion_events_.clear();
}

void SquirrelQuest::refresh_nearby(const TerrainGenerator& generator,
                                   const FireflyLoop& firefly_loop,
                                   Vec3 fox_position) {
  lit_lantern_positions_.clear();
  firefly_loop.lit_lantern_positions(lit_lantern_positions_);

  for (std::size_t i = 0; i < lit_lantern_positions_.size(); ++i) {
    const Vec3 lantern_position = lit_lantern_positions_[i];
    if (horizontal_distance(fox_position, lantern_position) > kSquirrelDiscoverRadius) {
      continue;
    }
    add_squirrel_candidate(generator, lantern_position, static_cast<int>(i));
    for (int slot = 0; slot < kAcornSlotsPerLitLantern; ++slot) {
      add_acorn_candidate(generator, firefly_loop, lantern_position, static_cast<int>(i), slot);
    }
  }
}

bool SquirrelQuest::add_squirrel_candidate(const TerrainGenerator& generator, Vec3 lantern_position, int lantern_index) {
  const int lantern_x = static_cast<int>(std::round(lantern_position.x));
  const int lantern_z = static_cast<int>(std::round(lantern_position.z));
  const std::uint32_t id = hash2(lantern_x + lantern_index * 23, lantern_z - lantern_index * 17, 0x51757221u);
  if (known_squirrel_ids_.find(id) != known_squirrel_ids_.end()) {
    return false;
  }
  known_squirrel_ids_.insert(id);

  Vec3 position = {};
  float heading = 0.0f;
  if (!find_lit_lantern_squirrel_site(generator, lantern_position, lantern_index, position, heading)) {
    return false;
  }

  Squirrel squirrel = {};
  squirrel.id = id;
  squirrel.home = position;
  squirrel.approach_start = squirrel_approach_start(generator, lantern_position, position);
  squirrel.position = squirrel.approach_start;
  squirrel.heading = std::atan2(position.x - squirrel.approach_start.x, position.z - squirrel.approach_start.z);
  squirrel.home_heading = heading;
  squirrel.animation_timer = hash01(lantern_x, lantern_z, 0x616e696du) * kTwoPi;
  squirrel.approach_timer = 0.0f;
  squirrel.approach_duration = kSquirrelApproachSeconds;
  squirrel.prompt_cooldown = 0.0f;
  squirrel.happy_timer = 0.0f;
  squirrel.active = true;
  squirrels_.push_back(squirrel);
  progress_for(id);
  return true;
}

bool SquirrelQuest::add_acorn_candidate(const TerrainGenerator& generator,
                                        const FireflyLoop& firefly_loop,
                                        Vec3 lantern_position,
                                        int lantern_index,
                                        int slot) {
  const int lantern_x = static_cast<int>(std::round(lantern_position.x));
  const int lantern_z = static_cast<int>(std::round(lantern_position.z));
  const std::uint32_t id = hash2(lantern_x + slot * 41, lantern_z - slot * 29 + lantern_index * 13, 0xac04c011u);
  if (known_acorn_ids_.find(id) != known_acorn_ids_.end()) {
    return false;
  }
  if (hash01(lantern_x + slot * 7, lantern_z - slot * 5, 0xac04c011u) > 0.78f) {
    return false;
  }

  const float angle = hash01(lantern_x + slot * 31, lantern_z - slot * 47, 0xac04c011u) * kTwoPi;
  const float radius = 4.1f +
      hash01(lantern_x - slot * 43, lantern_z + slot * 53, 0xac04c011u) * (kLitLanternAcornRadius - 4.1f);
  const float base_x = lantern_position.x + std::cos(angle) * radius;
  const float base_z = lantern_position.z + std::sin(angle) * radius;
  if (!can_place_acorn_at(generator, firefly_loop, base_x, base_z)) {
    return false;
  }
  known_acorn_ids_.insert(id);

  Acorn acorn = {};
  acorn.id = id;
  acorn.home = {
      base_x,
      interpolated_terrain_height(generator, base_x, base_z) + 1.0f,
      base_z,
  };
  acorn.position = acorn.home;
  acorn.phase = hash01(lantern_x + slot * 17, lantern_z - slot * 19, 0x626f6262u) * kTwoPi;
  acorn.active = collected_acorn_ids_.find(id) == collected_acorn_ids_.end();
  acorns_.push_back(acorn);
  return true;
}

SquirrelQuest::QuestProgress& SquirrelQuest::progress_for(std::uint32_t squirrel_id) {
  QuestProgress& progress = progress_[squirrel_id];
  if (progress.required_acorns <= 0) {
    progress.required_acorns = kRequiredAcorns;
  }
  return progress;
}

const SquirrelQuest::QuestProgress* SquirrelQuest::progress_for(std::uint32_t squirrel_id) const {
  const auto found = progress_.find(squirrel_id);
  return found != progress_.end() ? &found->second : nullptr;
}

SquirrelQuest::Squirrel* SquirrelQuest::nearest_squirrel(Vec3 fox_position, float max_distance) {
  Squirrel* nearest = nullptr;
  float nearest_distance = max_distance;
  for (Squirrel& squirrel : squirrels_) {
    if (squirrel.approach_timer < squirrel.approach_duration) {
      continue;
    }
    const float distance = horizontal_distance(fox_position, squirrel.position);
    if (distance <= nearest_distance) {
      nearest_distance = distance;
      nearest = &squirrel;
    }
  }
  return nearest;
}

const SquirrelQuest::Squirrel* SquirrelQuest::nearest_squirrel(Vec3 fox_position, float max_distance) const {
  const Squirrel* nearest = nullptr;
  float nearest_distance = max_distance;
  for (const Squirrel& squirrel : squirrels_) {
    if (squirrel.approach_timer < squirrel.approach_duration) {
      continue;
    }
    const float distance = horizontal_distance(fox_position, squirrel.position);
    if (distance <= nearest_distance) {
      nearest_distance = distance;
      nearest = &squirrel;
    }
  }
  return nearest;
}

std::uint32_t SquirrelQuest::best_incomplete_squirrel_id(Vec3 fox_position) const {
  if (active_squirrel_id_ != 0) {
    const QuestProgress* progress = progress_for(active_squirrel_id_);
    if (progress != nullptr && !progress->completed) {
      return active_squirrel_id_;
    }
  }

  const Squirrel* squirrel = nearest_squirrel(fox_position, 80.0f);
  if (squirrel == nullptr) {
    return 0;
  }
  const QuestProgress* progress = progress_for(squirrel->id);
  return progress == nullptr || progress->completed ? 0 : squirrel->id;
}

}  // namespace voxel
