#include "rabbit_burrows.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/firefly_loop.h"
#include "game/squirrel_quest.h"
#include "world/generator.h"
#include "world/mesher.h"
#include "world/voxel.h"

namespace voxel {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kBurrowDiscoverRadius = 110.0f;
constexpr float kBurrowRenderDistance = 54.0f;
constexpr float kBurrowInteractRadius = 2.6f;
constexpr float kBurrowPopSeconds = 0.45f;
constexpr float kBurrowMinSideDistance = 6.0f;
constexpr float kBurrowMaxSideDistance = 12.0f;
constexpr float kBurrowLanternClearance = 5.2f;
constexpr float kBurrowOwlClearance = 11.0f;
constexpr float kBurrowSpacing = 15.0f;
constexpr float kBurrowSquirrelClearance = 4.4f;
constexpr int kBurrowStartLanternIndex = 2;
constexpr int kBurrowLanternStride = 6;
constexpr int kBurrowPlacementAttempts = 7;
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

float hash01_from_id(std::uint32_t id, std::uint32_t seed) {
  return static_cast<float>(hash_u32(id ^ seed) & 0xffffu) / 65535.0f;
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
    direction = fallback;
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
  if (!is_solid(generator.voxel_at(cell_x, ground_y, cell_z).type) ||
      is_solid(generator.voxel_at(cell_x, ground_y + 1, cell_z).type)) {
    return false;
  }

  for (int y = ground_y + 1; y <= ground_y + headroom; ++y) {
    if (is_solid(generator.voxel_at(cell_x, y, cell_z).type)) {
      return false;
    }
  }
  return true;
}

bool has_flat_footprint(const TerrainGenerator& generator, float x, float z) {
  const int cell_x = static_cast<int>(std::round(x));
  const int cell_z = static_cast<int>(std::round(z));
  const int ground_y = generator.terrain_height(cell_x, cell_z);
  for (int dz = -2; dz <= 2; ++dz) {
    for (int dx = -2; dx <= 2; ++dx) {
      const int sample_y = generator.terrain_height(cell_x + dx, cell_z + dz);
      if (std::abs(sample_y - ground_y) > 1) {
        return false;
      }
    }
  }
  return true;
}

bool has_nearby_tree(const TerrainGenerator& generator, int cell_x, int cell_z, int radius) {
  for (int dz = -radius; dz <= radius; ++dz) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dz * dz > radius * radius) {
        continue;
      }
      const int sample_x = cell_x + dx;
      const int sample_z = cell_z + dz;
      const int ground_y = generator.terrain_height(sample_x, sample_z);
      for (int y = ground_y + 1; y <= ground_y + 5; ++y) {
        if (generator.voxel_at(sample_x, y, sample_z).type == VoxelType::Bark) {
          return true;
        }
      }
    }
  }
  return false;
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
  return !inside_moon_clearing(static_cast<float>(world_x), static_cast<float>(world_z));
}

bool near_major_dressing(float x, float z, float clearance) {
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
      if (distance_sq(x, z, static_cast<float>(world_x), static_cast<float>(world_z)) <
          clearance * clearance) {
        return true;
      }
    }
  }
  return false;
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
      if (distance_sq(x, z, base_x, base_z) < 1.9f && is_clear_ground(generator, base_x, base_z, 3)) {
        return true;
      }
    }
  }
  return false;
}

bool can_place_burrow_at(const TerrainGenerator& generator,
                         const FireflyLoop& firefly_loop,
                         const SquirrelQuest& squirrel_quest,
                         float x,
                         float z) {
  if (inside_moon_clearing(x, z)) {
    return false;
  }

  const Vec3 position = {x, interpolated_terrain_height(generator, x, z) + 1.0f, z};
  if (horizontal_distance(position, owl_perch_position(generator)) < kBurrowOwlClearance) {
    return false;
  }
  if (firefly_loop.blocks_acorn_spawn(position, kBurrowLanternClearance)) {
    return false;
  }
  if (squirrel_quest.blocks_landmark_spawn(position, kBurrowSquirrelClearance)) {
    return false;
  }
  if (!is_clear_ground(generator, x, z, 4) || !has_flat_footprint(generator, x, z)) {
    return false;
  }
  const int cell_x = static_cast<int>(std::round(x));
  const int cell_z = static_cast<int>(std::round(z));
  if (has_nearby_tree(generator, cell_x, cell_z, 7) ||
      has_nearby_solid_above_ground(generator, cell_x, cell_z, 2, 4) ||
      near_major_dressing(x, z, 2.4f) ||
      near_likely_mushroom(generator, x, z)) {
    return false;
  }
  return true;
}

}  // namespace

void RabbitBurrows::init(const TerrainGenerator& generator,
                         const FireflyLoop& firefly_loop,
                         const SquirrelQuest& squirrel_quest) {
  burrows_.clear();
  known_burrow_ids_.clear();
  lit_lantern_positions_.clear();
  dialogue_events_.clear();
  refresh_timer_ = 0.0f;
  refresh_nearby(generator, firefly_loop, squirrel_quest, {});
}

RabbitBurrows::UpdateResult RabbitBurrows::update(float dt,
                                                  const TerrainGenerator& generator,
                                                  const FireflyLoop& firefly_loop,
                                                  const SquirrelQuest& squirrel_quest,
                                                  Vec3 fox_position,
                                                  bool interact_pressed,
                                                  bool allow_dialogue) {
  UpdateResult result = {};
  dt = std::max(0.0f, std::min(dt, 0.10f));
  refresh_timer_ -= dt;
  if (refresh_timer_ <= 0.0f) {
    const std::size_t burrow_count = burrows_.size();
    refresh_nearby(generator, firefly_loop, squirrel_quest, fox_position);
    result.structural_changed = result.structural_changed || burrow_count != burrows_.size();
    refresh_timer_ = 0.90f;
  }

  if (interact_pressed) {
    Burrow* burrow = nearest_knockable_burrow(fox_position, kBurrowInteractRadius);
    if (burrow != nullptr && !burrow->popped_out) {
      burrow->popped_out = true;
      burrow->pop_progress = 0.0f;
      burrow->second_line_delay = 2.35f;
      queue_dialogue(*burrow, "oh! do you have any carrots?", 2.35f);
      burrow->first_dialogue_played = true;
      result.structural_changed = true;
    }
  }

  for (Burrow& burrow : burrows_) {
    if (!burrow.active) {
      continue;
    }
    const float previous_pop = burrow.pop_progress;
    burrow.animation_timer += dt;
    if (burrow.popped_out) {
      burrow.pop_progress = std::min(1.0f, burrow.pop_progress + dt / kBurrowPopSeconds);
      if (burrow.first_dialogue_played && !burrow.second_dialogue_played) {
        burrow.second_line_delay = std::max(0.0f, burrow.second_line_delay - dt);
        if (allow_dialogue && burrow.second_line_delay <= 0.0f) {
          queue_dialogue(burrow, "we're saving up for soup.", 2.25f);
          burrow.second_dialogue_played = true;
        }
      }
    }
    if (previous_pop != burrow.pop_progress) {
      result.animation_changed = true;
    }
  }

  return result;
}

void RabbitBurrows::append_dynamic_mesh(Mesh& mesh, Vec3 fox_position) const {
  for (const Burrow& burrow : burrows_) {
    if (!burrow.active || horizontal_distance(fox_position, burrow.position) > kBurrowRenderDistance) {
      continue;
    }
    append_burrow_mesh(mesh, burrow.position, burrow.heading);
    if (burrow.popped_out || burrow.pop_progress > 0.0f) {
      append_rabbit_mesh(mesh, burrow.position, burrow.heading, burrow.pop_progress);
    }
  }
}

bool RabbitBurrows::knock_prompt_visible(Vec3 fox_position) const {
  return nearest_knockable_burrow(fox_position, kBurrowInteractRadius) != nullptr;
}

bool RabbitBurrows::rabbit_position(std::uint32_t rabbit_id, Vec3& position) const {
  for (const Burrow& burrow : burrows_) {
    if (burrow.id != rabbit_id) {
      continue;
    }
    const float t = smoothstep(burrow.pop_progress);
    const float bounce = std::sin(t * kTwoPi) * 0.08f * (1.0f - t);
    position = burrow.position + Vec3{0.0f, -0.55f + 0.55f * t + bounce, 0.0f};
    return true;
  }
  return false;
}

void RabbitBurrows::drain_dialogue_events(std::vector<DialogueEvent>& events) {
  events.insert(events.end(), dialogue_events_.begin(), dialogue_events_.end());
  dialogue_events_.clear();
}

void RabbitBurrows::refresh_nearby(const TerrainGenerator& generator,
                                   const FireflyLoop& firefly_loop,
                                   const SquirrelQuest& squirrel_quest,
                                   Vec3 fox_position) {
  lit_lantern_positions_.clear();
  firefly_loop.lit_lantern_positions(lit_lantern_positions_);

  for (std::size_t i = 0; i < lit_lantern_positions_.size(); ++i) {
    const int lantern_index = static_cast<int>(i);
    if (lantern_index < kBurrowStartLanternIndex ||
        (lantern_index - kBurrowStartLanternIndex) % kBurrowLanternStride != 0) {
      continue;
    }
    const Vec3 lantern_position = lit_lantern_positions_[i];
    if (horizontal_distance(fox_position, lantern_position) > kBurrowDiscoverRadius) {
      continue;
    }
    add_burrow_candidate(generator, firefly_loop, squirrel_quest, lit_lantern_positions_, lantern_index);
  }
}

bool RabbitBurrows::add_burrow_candidate(const TerrainGenerator& generator,
                                         const FireflyLoop& firefly_loop,
                                         const SquirrelQuest& squirrel_quest,
                                         const std::vector<Vec3>& lantern_positions,
                                         int lantern_index) {
  const Vec3 lantern_position = lantern_positions[static_cast<std::size_t>(lantern_index)];
  const int lantern_x = static_cast<int>(std::round(lantern_position.x));
  const int lantern_z = static_cast<int>(std::round(lantern_position.z));
  const std::uint32_t raw_id = hash2(lantern_x + lantern_index * 43, lantern_z - lantern_index * 31, 0x72616262u);
  const std::uint32_t id = raw_id | 0x80000000u;
  if (known_burrow_ids_.find(id) != known_burrow_ids_.end()) {
    return false;
  }
  known_burrow_ids_.insert(id);

  const Vec3 previous = lantern_index > 0
      ? lantern_positions[static_cast<std::size_t>(lantern_index - 1)]
      : lantern_position + Vec3{-1.0f, 0.0f, 0.0f};
  const Vec3 next = lantern_index + 1 < static_cast<int>(lantern_positions.size())
      ? lantern_positions[static_cast<std::size_t>(lantern_index + 1)]
      : lantern_position + (lantern_position - previous);
  const Vec3 tangent = horizontal_direction(previous, next, {1.0f, 0.0f, 0.0f});
  Vec3 side = {-tangent.z, 0.0f, tangent.x};
  if (hash01_from_id(id, 0x73696465u) > 0.5f) {
    side = side * -1.0f;
  }

  for (int attempt = 0; attempt < kBurrowPlacementAttempts; ++attempt) {
    const float side_distance = kBurrowMinSideDistance +
        hash01(lantern_x + attempt * 17, lantern_z - attempt * 23, 0x72616263u) *
            (kBurrowMaxSideDistance - kBurrowMinSideDistance);
    const float along = (hash01(lantern_x - attempt * 29, lantern_z + attempt * 13, 0x72616264u) - 0.5f) * 5.0f;
    const float side_jitter =
        (hash01(lantern_x + attempt * 41, lantern_z - attempt * 37, 0x72616265u) - 0.5f) * 2.0f;
    const float x = lantern_position.x + side.x * (side_distance + side_jitter) + tangent.x * along;
    const float z = lantern_position.z + side.z * (side_distance + side_jitter) + tangent.z * along;
    bool near_existing_burrow = false;
    const Vec3 candidate_position = {x, interpolated_terrain_height(generator, x, z) + 1.0f, z};
    for (const Burrow& burrow : burrows_) {
      if (horizontal_distance(candidate_position, burrow.position) < kBurrowSpacing) {
        near_existing_burrow = true;
        break;
      }
    }
    if (near_existing_burrow || !can_place_burrow_at(generator, firefly_loop, squirrel_quest, x, z)) {
      continue;
    }

    Burrow burrow = {};
    burrow.id = id;
    burrow.position = candidate_position;
    burrow.heading = std::atan2(lantern_position.x - burrow.position.x,
                                lantern_position.z - burrow.position.z);
    burrow.lantern_index = lantern_index;
    burrow.animation_timer = hash01_from_id(id, 0x616e696du) * kTwoPi;
    burrow.active = true;
    burrows_.push_back(burrow);
    return true;
  }

  return false;
}

RabbitBurrows::Burrow* RabbitBurrows::nearest_knockable_burrow(Vec3 fox_position, float max_distance) {
  Burrow* nearest = nullptr;
  float nearest_distance = max_distance;
  for (Burrow& burrow : burrows_) {
    if (!burrow.active || burrow.popped_out) {
      continue;
    }
    const float distance = horizontal_distance(fox_position, burrow.position);
    if (distance <= nearest_distance) {
      nearest_distance = distance;
      nearest = &burrow;
    }
  }
  return nearest;
}

const RabbitBurrows::Burrow* RabbitBurrows::nearest_knockable_burrow(Vec3 fox_position, float max_distance) const {
  const Burrow* nearest = nullptr;
  float nearest_distance = max_distance;
  for (const Burrow& burrow : burrows_) {
    if (!burrow.active || burrow.popped_out) {
      continue;
    }
    const float distance = horizontal_distance(fox_position, burrow.position);
    if (distance <= nearest_distance) {
      nearest_distance = distance;
      nearest = &burrow;
    }
  }
  return nearest;
}

void RabbitBurrows::queue_dialogue(Burrow& burrow, const char* text, float seconds) {
  DialogueEvent event = {};
  event.rabbit_position = burrow.position;
  event.rabbit_id = burrow.id;
  std::snprintf(event.text, sizeof(event.text), "%s", text);
  event.seconds = seconds;
  dialogue_events_.push_back(event);
}

}  // namespace voxel
