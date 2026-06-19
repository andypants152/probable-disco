#include "firefly_loop.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#if defined(VOXEL_DEBUG_LANTERN_PLACEMENT)
#include <cstdio>
#endif

#include "core/audio.h"
#include "world/generator.h"
#include "world/mesher.h"

namespace voxel {

namespace {

constexpr float kFireflyBobHeight = 0.52f;
constexpr float kFireflyGlowMin = 0.28f;
constexpr float kFireflyGlowMax = 1.0f;
constexpr float kFireflyTwinkleSpeed = 2.05f;
constexpr float kFireflyTwinkleDepth = 0.96f;
constexpr float kFireflyTwinkleFadeSpeed = 0.36f;
constexpr float kFireflyTwinkleFadeFloor = 0.22f;
constexpr float kFireflyLanternSpreadXz = 3.35f;
constexpr float kFireflyLanternSpreadY = 0.95f;
constexpr float kFireflyWanderXz = 1.35f;
constexpr float kFireflyWanderY = 0.26f;
constexpr float kHeldFireflyOrbitRadius = 2.65f;
constexpr float kHeldFireflyOrbitHeight = 3.05f;
constexpr float kHeldFireflyOrbitBackOffset = 0.72f;
constexpr float kHeldFireflyOrbitSpeed = 1.18f;
constexpr float kHeldFireflyWobbleAmount = 0.32f;
constexpr float kHeldFireflyTwinkleSpeed = 2.65f;
constexpr float kLanternSpawnClearRadius = 8.0f;
constexpr float kLanternLightRadius = 15.0f;
constexpr float kLanternLightIntensity = 0.82f;
constexpr float kLanternLightFalloff = 5.45f;
constexpr float kLanternGroundGlowRadius = 10.0f;
constexpr float kLanternGroundGlowIntensity = 0.16f;
constexpr float kLanternGroundGlowFalloff = 3.8f;
constexpr float kLanternLightPulseDuration = 2.4f;
constexpr float kLanternLightPulseRadius = 4.0f;
constexpr float kLanternLightPulseIntensity = 0.22f;
constexpr float kLanternSquirrelBonusIntensity = 0.04f;
constexpr float kLanternStackIntensityDecay = 0.55f;
constexpr float kSquirrelLanternBonusRange = 54.0f;
constexpr int kMaxSquirrelLanternBonus = 4;
constexpr float kFireflyLightRadius = 6.2f;
constexpr float kFireflyLightIntensityBase = 0.28f;
constexpr float kFireflyLightIntensityGlow = 0.72f;
constexpr float kFireflyLightFalloff = 1.65f;
constexpr float kCarriedFireflyLightRadius = 5.8f;
constexpr float kCarriedFireflyLightIntensityBase = 0.28f;
constexpr float kCarriedFireflyLightIntensityGlow = 0.66f;
constexpr float kFireflyLightFullDistance = 18.0f;
constexpr float kFireflyLightCullDistance = 30.0f;
constexpr float kLanternLightFullDistance = 30.0f;
constexpr float kLanternLightCullDistance = 54.0f;
constexpr int kMaxLitLanternLights = 3;
constexpr int kMaxCarriedFireflyGameplayLights = 3;
constexpr int kMaxFreeFireflyGameplayLights = 12;
constexpr float kFireflyCollectRadius = 1.85f;
constexpr float kFireflyDepositRadius = 3.15f;
constexpr float kFireflyClusterRadius = kLanternSpawnClearRadius;
constexpr float kFireflyHeight = 2.65f;
constexpr float kDepositInterval = 0.16f;
constexpr float kLanternLightPulseSeconds = kLanternLightPulseDuration;
constexpr float kLanternSurfaceHeightOffset = 1.03f;
constexpr float kLanternSpiralAngleStep = 0.78f;
constexpr float kLanternSpiralInitialAngle = -0.68f;
constexpr float kLanternSpiralBaseRadius = 13.5f;
constexpr float kLanternSpiralRadiusStep = 11.5f;
constexpr float kLanternSpiralRadialJitter = 1.8f;
constexpr float kLanternSpiralTangentJitter = 3.1f;
constexpr float kLanternNearbySearchStep = 2.5f;
constexpr int kLanternNearbySearchAttempts = 96;
constexpr float kMinLanternSpacing = 18.5f;
constexpr float kLanternObstacleClearance = 3.15f;
constexpr float kLanternMushroomClearance = 2.45f;
constexpr float kLanternOwlClearance = 7.0f;
constexpr int kLanternMaxLocalSlope = 1;
constexpr int kLanternHeadroomVoxels = 5;
constexpr float kMoonClearingX = 42.0f;
constexpr float kMoonClearingZ = -104.0f;
constexpr float kMoonClearingRadius = 16.0f;
constexpr int kWorldDressingStep = 6;
constexpr int kMushroomCandidateStep = 3;
constexpr float kMushroomSpawnChance = 0.13f;
constexpr float kMushroomClusterChance = 0.24f;
constexpr float kMushroomClusterRadius = 2.35f;
constexpr std::array<Vec3, 12> kFireflyOffsets = {{
    {0.0f, 0.0f, -5.6f},
    {4.8f, 0.0f, -2.7f},
    {4.9f, 0.0f, 2.8f},
    {0.0f, 0.0f, 5.8f},
    {-4.7f, 0.0f, 2.6f},
    {-4.8f, 0.0f, -2.8f},
    {2.3f, 0.0f, -7.1f},
    {6.8f, 0.0f, 0.0f},
    {2.4f, 0.0f, 7.1f},
    {-2.4f, 0.0f, 7.1f},
    {-6.8f, 0.0f, 0.0f},
    {-2.3f, 0.0f, -7.1f},
}};

float smoothstep(float value) {
  value = std::max(0.0f, std::min(1.0f, value));
  return value * value * (3.0f - 2.0f * value);
}

float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

std::uint32_t hash_u32(std::uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return value;
}

float unit_from_hash(std::uint32_t value) {
  return static_cast<float>(hash_u32(value) & 0xffffu) / 65535.0f;
}

float signed_unit_from_hash(std::uint32_t value) {
  return unit_from_hash(value) * 2.0f - 1.0f;
}

int floor_to_int(float value) {
  return static_cast<int>(std::floor(value));
}

int round_to_int(float value) {
  return static_cast<int>(std::round(value));
}

int floor_div(int value, int divisor) {
  int quotient = value / divisor;
  const int remainder = value % divisor;
  if (remainder < 0) {
    --quotient;
  }
  return quotient;
}

std::uint32_t hash2(int x, int z, std::uint32_t seed) {
  std::uint32_t h = seed;
  h ^= static_cast<std::uint32_t>(x) * 0x8da6b343u;
  h ^= static_cast<std::uint32_t>(z) * 0xd8163841u;
  h ^= h >> 13;
  h *= 0x85ebca6bu;
  h ^= h >> 16;
  return h;
}

float hash01(int x, int z, std::uint32_t seed) {
  return static_cast<float>(hash2(x, z, seed) & 0xffffu) / 65535.0f;
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

Vec3 terrain_position(const TerrainGenerator& generator, Vec3 anchor, float height_offset) {
  return {
    anchor.x,
    interpolated_terrain_height(generator, anchor.x, anchor.z) + height_offset,
    anchor.z,
  };
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

float moon_clearing_influence(float world_x, float world_z) {
  const float dx = world_x - kMoonClearingX;
  const float dz = world_z - kMoonClearingZ;
  const float distance = std::sqrt(dx * dx + dz * dz);
  return std::max(0.0f, std::min(1.0f, (kMoonClearingRadius - distance) / 6.0f));
}

float distance_fade(float distance, float full_distance, float cull_distance) {
  if (distance <= full_distance) {
    return 1.0f;
  }
  if (distance >= cull_distance) {
    return 0.0f;
  }
  return 1.0f - smoothstep((distance - full_distance) / (cull_distance - full_distance));
}

Vec3 local_to_world_offset(float local_x, float local_z, float heading) {
  const float s = std::sin(heading);
  const float c = std::cos(heading);
  return {
    local_x * c + local_z * s,
    0.0f,
    -local_x * s + local_z * c,
  };
}

std::uint32_t terrain_seed_fingerprint(const TerrainGenerator& generator) {
  std::uint32_t seed = 0x6c616e74u;
  constexpr std::array<Vec3, 5> kSamples = {{
      {0.0f, 0.0f, 0.0f},
      {17.0f, 0.0f, -23.0f},
      {-31.0f, 0.0f, 11.0f},
      {42.0f, 0.0f, -104.0f},
      {73.0f, 0.0f, -61.0f},
  }};
  for (const Vec3& sample : kSamples) {
    const int height = generator.terrain_height(static_cast<int>(sample.x), static_cast<int>(sample.z));
    seed = hash_u32(seed ^ static_cast<std::uint32_t>((height + 64) * 131 +
                                                       static_cast<int>(sample.x) * 17 -
                                                       static_cast<int>(sample.z) * 29));
  }
  return seed;
}

Vec3 spiral_lantern_anchor(const TerrainGenerator& generator, Vec3 center, int sequence, float& target_radius) {
  const std::uint32_t seed = hash_u32(terrain_seed_fingerprint(generator) ^
                                     (static_cast<std::uint32_t>(sequence) * 0x9e3779b9u + 0x4f574c53u));
  const float angle = kLanternSpiralInitialAngle +
                      static_cast<float>(sequence) * kLanternSpiralAngleStep;
  target_radius = kLanternSpiralBaseRadius +
                  static_cast<float>(sequence) * kLanternSpiralRadiusStep +
                  signed_unit_from_hash(seed ^ 0xa11cu) * kLanternSpiralRadialJitter;
  const float tangent_jitter = signed_unit_from_hash(seed ^ 0x7a6du) * kLanternSpiralTangentJitter;
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  return {
      center.x + c * target_radius - s * tangent_jitter,
      0.0f,
      center.z + s * target_radius + c * tangent_jitter,
  };
}

bool dressing_origin_for_grid(int grid_x, int grid_z, int& world_x, int& world_z, float& seed) {
  seed = hash01(grid_x, grid_z, 0xdec042u);
  const float ox = (hash01(grid_x + 300, grid_z - 300, 0xdec042u) - 0.5f) * 3.6f;
  const float oz = (hash01(grid_x - 300, grid_z + 300, 0xdec042u) - 0.5f) * 3.6f;
  world_x = grid_x + round_to_int(ox);
  world_z = grid_z + round_to_int(oz);
  return moon_clearing_influence(static_cast<float>(world_x), static_cast<float>(world_z)) <= 0.18f;
}

bool near_major_dressing_obstacle(float x, float z, float clearance) {
  const int min_chunk_x = floor_div(floor_to_int(x - clearance - static_cast<float>(kWorldDressingStep)), kChunkSize);
  const int max_chunk_x = floor_div(floor_to_int(x + clearance + static_cast<float>(kWorldDressingStep)), kChunkSize);
  const int min_chunk_z = floor_div(floor_to_int(z - clearance - static_cast<float>(kWorldDressingStep)), kChunkSize);
  const int max_chunk_z = floor_div(floor_to_int(z + clearance + static_cast<float>(kWorldDressingStep)), kChunkSize);

  for (int chunk_z = min_chunk_z; chunk_z <= max_chunk_z; ++chunk_z) {
    for (int chunk_x = min_chunk_x; chunk_x <= max_chunk_x; ++chunk_x) {
      const int min_x = chunk_x * kChunkSize;
      const int min_z = chunk_z * kChunkSize;
      for (int grid_x = min_x + 4; grid_x < min_x + kChunkSize - 4; grid_x += kWorldDressingStep) {
        for (int grid_z = min_z + 4; grid_z < min_z + kChunkSize - 4; grid_z += kWorldDressingStep) {
          int world_x = 0;
          int world_z = 0;
          float seed = 0.0f;
          if (!dressing_origin_for_grid(grid_x, grid_z, world_x, world_z, seed) || seed <= 0.925f) {
            continue;
          }

          if (seed > 0.94f) {
            const float radius = seed > 0.965f ? 1.75f : 1.30f;
            if (distance_sq(x, z, static_cast<float>(world_x), static_cast<float>(world_z)) <
                (clearance + radius) * (clearance + radius)) {
              return true;
            }
            continue;
          }

          const int length = 3 + static_cast<int>(hash01(world_x - 31, world_z + 29, 0x6c6f6775u) * 4.0f);
          const float half_length = static_cast<float>(length - 1) * 0.5f + 0.80f + clearance;
          const float dx = x - static_cast<float>(world_x);
          const float dz = z - static_cast<float>(world_z);
          if (seed > 0.5f) {
            if (std::abs(dx) <= half_length && std::abs(dz) <= clearance + 0.72f) {
              return true;
            }
          } else if (std::abs(dz) <= half_length && std::abs(dx) <= clearance + 0.72f) {
            return true;
          }
        }
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

bool near_potential_mushroom(float x, float z, float clearance) {
  const int min_chunk_x = floor_div(floor_to_int(x - clearance - 6.0f), kChunkSize);
  const int max_chunk_x = floor_div(floor_to_int(x + clearance + 6.0f), kChunkSize);
  const int min_chunk_z = floor_div(floor_to_int(z - clearance - 6.0f), kChunkSize);
  const int max_chunk_z = floor_div(floor_to_int(z + clearance + 6.0f), kChunkSize);

  for (int chunk_z = min_chunk_z; chunk_z <= max_chunk_z; ++chunk_z) {
    for (int chunk_x = min_chunk_x; chunk_x <= max_chunk_x; ++chunk_x) {
      const int min_x = chunk_x * kChunkSize;
      const int min_z = chunk_z * kChunkSize;
      for (int grid_x = min_x + 3; grid_x < min_x + kChunkSize - 3; grid_x += kMushroomCandidateStep) {
        for (int grid_z = min_z + 3; grid_z < min_z + kChunkSize - 3; grid_z += kMushroomCandidateStep) {
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

            const int cell_x = round_to_int(mushroom_x);
            const int cell_z = round_to_int(mushroom_z);
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

bool terrain_allows_lantern(const TerrainGenerator& generator, float x, float z) {
  const int cell_x = round_to_int(x);
  const int cell_z = round_to_int(z);
  const int ground_y = generator.terrain_height(cell_x, cell_z);
  if (!is_solid(generator.voxel_at(cell_x, ground_y, cell_z).type) ||
      is_solid(generator.voxel_at(cell_x, ground_y + 1, cell_z).type)) {
    return false;
  }

  const int slope_radius = 2;
  for (int dz = -slope_radius; dz <= slope_radius; ++dz) {
    for (int dx = -slope_radius; dx <= slope_radius; ++dx) {
      if (std::abs(generator.terrain_height(cell_x + dx, cell_z + dz) - ground_y) > kLanternMaxLocalSlope) {
        return false;
      }
    }
  }

  const int obstacle_radius = static_cast<int>(std::ceil(kLanternObstacleClearance));
  const float obstacle_distance_sq = kLanternObstacleClearance * kLanternObstacleClearance;
  for (int dz = -obstacle_radius; dz <= obstacle_radius; ++dz) {
    for (int dx = -obstacle_radius; dx <= obstacle_radius; ++dx) {
      if (static_cast<float>(dx * dx + dz * dz) > obstacle_distance_sq) {
        continue;
      }
      const int sample_x = cell_x + dx;
      const int sample_z = cell_z + dz;
      const int sample_ground_y = generator.terrain_height(sample_x, sample_z);
      if (std::abs(sample_ground_y - ground_y) > kLanternMaxLocalSlope) {
        return false;
      }
      for (int y = sample_ground_y + 1; y <= sample_ground_y + kLanternHeadroomVoxels; ++y) {
        if (is_solid(generator.voxel_at(sample_x, y, sample_z).type)) {
          return false;
        }
      }
    }
  }

  return true;
}

bool can_place_lantern_anchor(const TerrainGenerator& generator, Vec3 center, Vec3 anchor, float min_radius) {
  if (horizontal_distance(center, anchor) < min_radius) {
    return false;
  }
  if (horizontal_distance(owl_perch_position(generator), anchor) < kLanternOwlClearance) {
    return false;
  }
  if (!terrain_allows_lantern(generator, anchor.x, anchor.z)) {
    return false;
  }
  if (near_major_dressing_obstacle(anchor.x, anchor.z, kLanternObstacleClearance)) {
    return false;
  }
  if (near_potential_mushroom(anchor.x, anchor.z, kLanternMushroomClearance)) {
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
                        float intensity,
                        float falloff = 2.0f) {
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
  light.falloff = falloff;
  light.active = true;
}

}  // namespace

void FireflyLoop::init(const TerrainGenerator& generator) {
  carried_fireflies_ = 0;
  fireflies_unlocked_ = false;
  active_lantern_index_ = 0;
  lantern_sequence_ = 0;
  firefly_orbit_timer_ = 0.0f;
  firefly_chime_cooldown_ = 1.0f;
  deposit_cooldown_ = 0.0f;
  lanterns_.clear();
  lanterns_.reserve(32);
  for (Firefly& firefly : fireflies_) {
    firefly = {};
  }
  activate_lantern(generator, 0);
}

void FireflyLoop::unlock_fireflies(const TerrainGenerator& generator) {
  if (fireflies_unlocked_) {
    return;
  }
  fireflies_unlocked_ = true;
  spawn_fireflies_for_lantern(generator, active_lantern_index_);
}

Vec3 FireflyLoop::lantern_position_for_sequence(const TerrainGenerator& generator, int sequence) const {
  Vec3 center = owl_perch_position(generator);
  center.y = 0.0f;

  float target_radius = 0.0f;
  const Vec3 base_anchor = spiral_lantern_anchor(generator, center, sequence, target_radius);
  const float min_radius = std::max(kLanternSpiralBaseRadius * 0.72f,
                                    target_radius - kLanternNearbySearchStep * 4.0f);

  const auto far_enough_from_lanterns = [this, sequence](Vec3 anchor) {
    const int previous_count = std::min(sequence, static_cast<int>(lanterns_.size()));
    for (int i = 0; i < previous_count; ++i) {
      const Lantern& previous = lanterns_[static_cast<std::size_t>(i)];
      if (horizontal_distance(anchor, previous.position) < kMinLanternSpacing) {
        return false;
      }
    }
    return true;
  };

  const auto valid = [&](Vec3 anchor) {
    return far_enough_from_lanterns(anchor) &&
        can_place_lantern_anchor(generator, center, anchor, min_radius);
  };

  if (valid(base_anchor)) {
#if defined(VOXEL_DEBUG_LANTERN_PLACEMENT)
    std::printf("lantern placement index %d radius %.2f success 1 attempts 0 x %.2f z %.2f\n",
                sequence,
                target_radius,
                base_anchor.x,
                base_anchor.z);
#endif
    return terrain_position(generator, base_anchor, kLanternSurfaceHeightOffset);
  }

  const std::uint32_t search_seed = hash_u32(terrain_seed_fingerprint(generator) ^
                                            (static_cast<std::uint32_t>(sequence) * 0x85ebca6bu + 0x5eec41u));
  const float search_angle_offset = unit_from_hash(search_seed) * 6.28318530718f;
  for (int attempt = 1; attempt <= kLanternNearbySearchAttempts; ++attempt) {
    const int ring = (attempt - 1) / 12 + 1;
    const float search_radius = static_cast<float>(ring) * kLanternNearbySearchStep;
    const float angle = search_angle_offset +
                        static_cast<float>(attempt) * 2.39996322973f;
    const Vec3 anchor = {
        base_anchor.x + std::cos(angle) * search_radius,
        0.0f,
        base_anchor.z + std::sin(angle) * search_radius,
    };
    if (!valid(anchor)) {
      continue;
    }

#if defined(VOXEL_DEBUG_LANTERN_PLACEMENT)
    std::printf("lantern placement index %d radius %.2f success 1 attempts %d x %.2f z %.2f\n",
                sequence,
                target_radius,
                attempt,
                anchor.x,
                anchor.z);
#endif
    return terrain_position(generator, anchor, kLanternSurfaceHeightOffset);
  }

#if defined(VOXEL_DEBUG_LANTERN_PLACEMENT)
  std::printf("lantern placement index %d radius %.2f success 0 attempts %d fallback_x %.2f fallback_z %.2f\n",
              sequence,
              target_radius,
              kLanternNearbySearchAttempts,
              base_anchor.x,
              base_anchor.z);
#endif
  return terrain_position(generator, base_anchor, kLanternSurfaceHeightOffset);
}

void FireflyLoop::activate_lantern(const TerrainGenerator& generator, int sequence) {
  if (sequence < 0) {
    sequence = 0;
  }
  lantern_sequence_ = sequence;
  for (Lantern& lantern : lanterns_) {
    lantern.active = false;
  }
  active_lantern_index_ = sequence;
  if (active_lantern_index_ >= static_cast<int>(lanterns_.size())) {
    lanterns_.resize(static_cast<std::size_t>(active_lantern_index_ + 1));
  }

  Lantern& lantern = lanterns_[active_lantern_index_];
  lantern = {};
  lantern.position = lantern_position_for_sequence(generator, sequence);
  lantern.active = true;
  lantern.lit = false;
  lantern.deposited_fireflies = 0;
  lantern.required_fireflies = 3;
  lantern.glow_intensity = 0.0f;
  lantern.glow_timer = 0.0f;
  lantern.pulse_timer = 0.0f;
  if (fireflies_unlocked_) {
    spawn_fireflies_for_lantern(generator, active_lantern_index_);
  } else {
    for (Firefly& firefly : fireflies_) {
      firefly = {};
    }
  }
}

void FireflyLoop::spawn_fireflies_for_lantern(const TerrainGenerator& generator, int index) {
  for (Firefly& firefly : fireflies_) {
    firefly = {};
  }

  const Lantern& lantern = lanterns_[static_cast<std::size_t>(index)];
  const int count = std::min<int>(kMaxFireflies, lantern.required_fireflies + 3);
  for (int i = 0; i < count; ++i) {
    Firefly& firefly = fireflies_[static_cast<std::size_t>(i)];
    const Vec3 offset = kFireflyOffsets[static_cast<std::size_t>(i)];
    const float offset_distance = std::max(0.001f, horizontal_distance({}, offset));
    const float clamped_radius = std::min(offset_distance, kFireflyClusterRadius);
    const std::uint32_t seed = hash_u32(static_cast<std::uint32_t>(lantern_sequence_ * 97 + i * 131 + 17));
    const float spread_jitter = 0.86f + unit_from_hash(seed ^ 0x15f3u) * 0.34f;
    const float side_jitter = signed_unit_from_hash(seed ^ 0xa523u) * 1.75f;
    const Vec3 tangent = {-offset.z / offset_distance, 0.0f, offset.x / offset_distance};
    const Vec3 readable_offset = {
        offset.x / offset_distance * clamped_radius * kFireflyLanternSpreadXz * spread_jitter +
            tangent.x * side_jitter,
        0.0f,
        offset.z / offset_distance * clamped_radius * kFireflyLanternSpreadXz * spread_jitter +
            tangent.z * side_jitter};
    const Vec3 anchor = {lantern.position.x + readable_offset.x, 0.0f, lantern.position.z + readable_offset.z};
    const float height_offset = kFireflyHeight +
                                signed_unit_from_hash(seed ^ 0x79bdu) * kFireflyLanternSpreadY +
                                0.20f * static_cast<float>(i % 3);
    firefly.home = terrain_position(generator, anchor, height_offset);
    firefly.position = firefly.home;
    firefly.velocity = {0.0f, 0.0f, 0.0f};
    firefly.phase = 6.28318530718f * unit_from_hash(seed ^ 0x349bu);
    firefly.bob_timer = 6.28318530718f * unit_from_hash(seed ^ 0xc0deu);
    firefly.twinkle_phase = 6.28318530718f * unit_from_hash(seed ^ 0x8f71u);
    firefly.glow_intensity = kFireflyGlowMin;
    firefly.collected = false;
    firefly.active = true;
  }
}

int FireflyLoop::active_firefly_count() const {
  int count = 0;
  for (const Firefly& firefly : fireflies_) {
    if (firefly.active && !firefly.collected) {
      ++count;
    }
  }
  return count;
}

int FireflyLoop::deposited_fireflies() const {
  return lanterns_[static_cast<std::size_t>(active_lantern_index_)].deposited_fireflies;
}

int FireflyLoop::required_fireflies() const {
  return lanterns_[static_cast<std::size_t>(active_lantern_index_)].required_fireflies;
}

float FireflyLoop::firefly_glow_intensity() const {
  return kFireflyGlowMax;
}

float FireflyLoop::lantern_light_intensity() const {
  return kLanternLightIntensity;
}

float FireflyLoop::lantern_light_radius() const {
  return kLanternLightRadius;
}

bool FireflyLoop::blocks_acorn_spawn(Vec3 position, float radius) const {
  for (const Lantern& lantern : lanterns_) {
    if (!lantern.active && !lantern.lit && lantern.deposited_fireflies == 0) {
      continue;
    }
    if (horizontal_distance(position, lantern.position) <= radius) {
      return true;
    }
  }
  return false;
}

bool FireflyLoop::has_lit_lantern_near(Vec3 position, float radius) const {
  for (const Lantern& lantern : lanterns_) {
    if (!lantern.lit) {
      continue;
    }
    if (horizontal_distance(position, lantern.position) <= radius) {
      return true;
    }
  }
  return false;
}

void FireflyLoop::lit_lantern_positions(std::vector<Vec3>& positions) const {
  for (const Lantern& lantern : lanterns_) {
    if (lantern.lit) {
      positions.push_back(lantern.position);
    }
  }
}

void FireflyLoop::add_squirrel_completion_bonus(Vec3 position) {
  float nearest_distance = kSquirrelLanternBonusRange;
  Lantern* nearest_lantern = nullptr;
  for (Lantern& lantern : lanterns_) {
    if (!lantern.active && !lantern.lit) {
      continue;
    }
    const float distance = horizontal_distance(position, lantern.position);
    if (distance <= nearest_distance) {
      nearest_distance = distance;
      nearest_lantern = &lantern;
    }
  }

  if (nearest_lantern == nullptr) {
    return;
  }

  nearest_lantern->squirrel_bonus = std::min(kMaxSquirrelLanternBonus, nearest_lantern->squirrel_bonus + 1);
  nearest_lantern->pulse_timer = std::max(nearest_lantern->pulse_timer, kLanternLightPulseSeconds * 0.55f);
}

void FireflyLoop::dev_collect_active_fireflies() {
  for (Firefly& firefly : fireflies_) {
    if (!firefly.active || firefly.collected) {
      continue;
    }
    firefly.collected = true;
    firefly.active = false;
    ++carried_fireflies_;
  }
}

void FireflyLoop::dev_deposit_carried_fireflies(const TerrainGenerator& generator) {
  Lantern& lantern = lanterns_[static_cast<std::size_t>(active_lantern_index_)];
  if (lantern.lit) {
    return;
  }

  const int needed_fireflies = std::max(0, lantern.required_fireflies - lantern.deposited_fireflies);
  if (needed_fireflies <= 0) {
    return;
  }
  if (carried_fireflies_ <= 0) {
    carried_fireflies_ = needed_fireflies;
  }
  const int deposited_now = std::min(carried_fireflies_, needed_fireflies);
  carried_fireflies_ -= deposited_now;
  lantern.deposited_fireflies += deposited_now;
  deposit_cooldown_ = kDepositInterval;

  if (lantern.deposited_fireflies >= lantern.required_fireflies) {
    lantern.lit = true;
    lantern.active = false;
    lantern.glow_intensity = 1.0f;
    lantern.pulse_timer = kLanternLightPulseSeconds;
    carried_fireflies_ = 0;
    activate_lantern(generator, lantern_sequence_ + 1);
  }
}

Vec3 FireflyLoop::objective_position(Vec3 fox_position) const {
  const Lantern& lantern = lanterns_[static_cast<std::size_t>(active_lantern_index_)];
  if (carried_fireflies_ > 0) {
    return lantern.position;
  }

  float nearest_distance = 1000000.0f;
  Vec3 nearest_position = lantern.position;
  for (const Firefly& firefly : fireflies_) {
    if (!firefly.active || firefly.collected) {
      continue;
    }
    const float distance = horizontal_distance(fox_position, firefly.position);
    if (distance < nearest_distance) {
      nearest_distance = distance;
      nearest_position = firefly.position;
    }
  }
  return nearest_position;
}

Vec3 FireflyLoop::farthest_firefly_position(Vec3 fox_position) const {
  float farthest_distance = -1.0f;
  Vec3 farthest_position = objective_position(fox_position);
  for (const Firefly& firefly : fireflies_) {
    if (!firefly.active || firefly.collected) {
      continue;
    }
    const float distance = horizontal_distance(fox_position, firefly.position);
    if (distance > farthest_distance) {
      farthest_distance = distance;
      farthest_position = firefly.position;
    }
  }
  return farthest_position;
}

Vec3 FireflyLoop::carried_firefly_position(Vec3 fox_position, float fox_heading, int index) const {
  const float seed = static_cast<float>(index) * 2.173f + 0.61f;
  const float angle = fox_heading +
                      firefly_orbit_timer_ * kHeldFireflyOrbitSpeed +
                      static_cast<float>(index) * 2.09439510239f +
                      std::sin(firefly_orbit_timer_ * 0.84f + seed) * 0.18f;
  const float radius = kHeldFireflyOrbitRadius +
                       std::sin(firefly_orbit_timer_ * 1.37f + seed * 1.6f) * kHeldFireflyWobbleAmount +
                       0.16f * static_cast<float>(index % 2);
  const Vec3 center = fox_position +
                      local_to_world_offset(0.0f, -kHeldFireflyOrbitBackOffset, fox_heading) +
                      Vec3{0.0f, kHeldFireflyOrbitHeight, 0.0f};
  return center + Vec3{
    std::sin(angle) * radius,
    0.30f * std::sin(firefly_orbit_timer_ * 1.12f + seed) +
        0.18f * (static_cast<float>(index % 3) - 1.0f),
    std::cos(angle) * radius,
  };
}

float FireflyLoop::carried_firefly_glow_intensity(int index) const {
  const float seed = static_cast<float>(index) * 2.173f + 0.61f;
  const float pulse_t = smoothstep(std::sin(firefly_orbit_timer_ * kHeldFireflyTwinkleSpeed + seed * 1.9f) *
                                   0.5f + 0.5f);
  const float slow_fade = lerp(0.58f,
                               1.0f,
                               smoothstep(std::sin(firefly_orbit_timer_ * 0.48f + seed * 1.31f) *
                                          0.5f + 0.5f));
  return std::min(kFireflyGlowMax,
                  (0.36f + (kFireflyGlowMax - 0.36f) * pulse_t) * slow_fade);
}

void FireflyLoop::append_dynamic_mesh(Mesh& mesh, Vec3 fox_position, float fox_heading) const {
  for (const Lantern& lantern : lanterns_) {
    if (!lantern.active && !lantern.lit && lantern.deposited_fireflies == 0) {
      continue;
    }
    append_lantern_mesh(mesh,
                        lantern.position,
                        lantern.deposited_fireflies,
                        lantern.required_fireflies,
                        lantern.lit,
                        lantern.glow_intensity);
    for (int i = 0; i < lantern.squirrel_bonus; ++i) {
      const float seed = static_cast<float>(i) * 2.17f + lantern.position.x * 0.037f + lantern.position.z * 0.021f;
      const float angle = lantern.glow_timer * (0.42f + 0.05f * static_cast<float>(i)) + seed;
      const float radius = 2.2f + 0.45f * static_cast<float>(i % 3);
      const Vec3 position = lantern.position + Vec3{
          std::sin(angle) * radius,
          2.05f + std::sin(lantern.glow_timer * 1.2f + seed) * 0.38f,
          std::cos(angle) * radius,
      };
      append_firefly_mesh(mesh, position, 0.62f + 0.24f * std::sin(lantern.glow_timer * 1.7f + seed), false);
    }
  }
  for (const Firefly& firefly : fireflies_) {
    if (!firefly.active || firefly.collected) {
      continue;
    }
    append_firefly_mesh(mesh, firefly.position, firefly.glow_intensity, false);
  }
  for (int i = 0; i < carried_fireflies_; ++i) {
    append_firefly_mesh(mesh, carried_firefly_position(fox_position, fox_heading, i), carried_firefly_glow_intensity(i), true);
  }
}

void FireflyLoop::append_gameplay_lights(std::array<GameplayLight, kMaxGameplayLights>& lights,
                                         int& light_count,
                                         int light_limit,
                                         Vec3 fox_position,
                                         float fox_heading) const {
  const Vec3 firefly_color = {1.0f, 0.78f, 0.22f};
  const Vec3 mote_color = {0.86f, 1.0f, 0.46f};
  const Vec3 lantern_color = {1.0f, 0.52f, 0.18f};

  std::array<int, kMaxLitLanternLights> closest_lanterns = {};
  std::array<float, kMaxLitLanternLights> closest_distances = {};
  int closest_lantern_count = 0;
  for (std::size_t i = 0; i < lanterns_.size(); ++i) {
    const Lantern& lit_lantern = lanterns_[i];
    if (!lit_lantern.lit) {
      continue;
    }
    const float distance = horizontal_distance(fox_position, lit_lantern.position);
    if (distance >= kLanternLightCullDistance) {
      continue;
    }
    int insert_at = closest_lantern_count;
    while (insert_at > 0 && distance < closest_distances[static_cast<std::size_t>(insert_at - 1)]) {
      if (insert_at < kMaxLitLanternLights) {
        closest_lanterns[static_cast<std::size_t>(insert_at)] =
            closest_lanterns[static_cast<std::size_t>(insert_at - 1)];
        closest_distances[static_cast<std::size_t>(insert_at)] =
            closest_distances[static_cast<std::size_t>(insert_at - 1)];
      }
      --insert_at;
    }
    if (insert_at < kMaxLitLanternLights) {
      closest_lanterns[static_cast<std::size_t>(insert_at)] = static_cast<int>(i);
      closest_distances[static_cast<std::size_t>(insert_at)] = distance;
      closest_lantern_count = std::min(closest_lantern_count + 1, kMaxLitLanternLights);
    }
  }

  for (int i = 0; i < closest_lantern_count; ++i) {
    const Lantern& lit_lantern = lanterns_[static_cast<std::size_t>(closest_lanterns[static_cast<std::size_t>(i)])];
    const float distance = closest_distances[static_cast<std::size_t>(i)];
    const float light_fade = distance_fade(distance, kLanternLightFullDistance, kLanternLightCullDistance);
    if (light_fade <= 0.0f) {
      continue;
    }
    const Vec3 lantern_light_position = lit_lantern.position + Vec3{0.0f, 1.85f, 0.0f};
    const float pulse_t = lit_lantern.pulse_timer > 0.0f
        ? 1.0f - lit_lantern.pulse_timer / kLanternLightPulseDuration
        : 1.0f;
    const float pulse = lit_lantern.pulse_timer > 0.0f ? (1.0f - pulse_t) : 0.0f;
    const float stack_scale = i == 0 ? 1.0f : kLanternStackIntensityDecay;
    add_gameplay_light(lights,
                       light_count,
                       light_limit,
                       lantern_light_position,
                       lantern_color,
                       kLanternLightRadius + pulse * kLanternLightPulseRadius,
                       (kLanternLightIntensity + pulse * kLanternLightPulseIntensity +
                        kLanternSquirrelBonusIntensity * static_cast<float>(lit_lantern.squirrel_bonus)) *
                           light_fade * stack_scale,
                       kLanternLightFalloff);
    add_gameplay_light(lights,
                       light_count,
                       light_limit,
                       lit_lantern.position + Vec3{0.0f, 0.26f, 0.0f},
                       lantern_color,
                       kLanternGroundGlowRadius,
                       (kLanternGroundGlowIntensity +
                        0.012f * static_cast<float>(lit_lantern.squirrel_bonus)) * light_fade * stack_scale,
                       kLanternGroundGlowFalloff);
  }

  const int carried_firefly_light_count = std::min(carried_fireflies_, kMaxCarriedFireflyGameplayLights);
  for (int i = 0; i < carried_firefly_light_count; ++i) {
    const float glow = carried_firefly_glow_intensity(i);
    add_gameplay_light(lights,
                       light_count,
                       light_limit,
                       carried_firefly_position(fox_position, fox_heading, i),
                       mote_color,
                       kCarriedFireflyLightRadius,
                       kCarriedFireflyLightIntensityBase + glow * kCarriedFireflyLightIntensityGlow,
                       kFireflyLightFalloff);
  }

  int free_firefly_light_count = 0;
  for (const Firefly& firefly : fireflies_) {
    if (!firefly.active || firefly.collected) {
      continue;
    }
    if (free_firefly_light_count >= kMaxFreeFireflyGameplayLights) {
      break;
    }
    const float distance = horizontal_distance(fox_position, firefly.position);
    const float light_fade = distance_fade(distance, kFireflyLightFullDistance, kFireflyLightCullDistance);
    if (light_fade <= 0.0f) {
      continue;
    }
    add_gameplay_light(lights,
                       light_count,
                       light_limit,
                       firefly.position,
                       firefly_color,
                       kFireflyLightRadius,
                       (kFireflyLightIntensityBase + firefly.glow_intensity * kFireflyLightIntensityGlow) *
                           light_fade,
                       kFireflyLightFalloff);
    ++free_firefly_light_count;
  }
}

bool FireflyLoop::update(float dt, const TerrainGenerator& generator, Vec3 fox_position, float) {
  bool changed = false;
  dt = std::max(0.0f, std::min(dt, 0.10f));
  firefly_orbit_timer_ += dt;
  firefly_chime_cooldown_ = std::max(0.0f, firefly_chime_cooldown_ - dt);
  deposit_cooldown_ = std::max(0.0f, deposit_cooldown_ - dt);

  for (std::size_t i = 0; i < lanterns_.size(); ++i) {
    if (static_cast<int>(i) == active_lantern_index_ || !lanterns_[i].lit) {
      continue;
    }
    Lantern& lit_lantern = lanterns_[i];
    lit_lantern.glow_timer += dt;
    const float pulse_before = lit_lantern.pulse_timer;
    if (lit_lantern.pulse_timer > 0.0f) {
      lit_lantern.pulse_timer = std::max(0.0f, lit_lantern.pulse_timer - dt);
    }
    const float pulse = lit_lantern.pulse_timer > 0.0f
        ? std::sin((kLanternLightPulseSeconds - lit_lantern.pulse_timer) * 8.0f) * 0.18f + 0.18f
        : 0.0f;
    lit_lantern.glow_intensity = std::min(1.0f, 0.82f + pulse + 0.025f * static_cast<float>(lit_lantern.squirrel_bonus));
    changed = changed || pulse_before != lit_lantern.pulse_timer;
  }

  Lantern& lantern = lanterns_[static_cast<std::size_t>(active_lantern_index_)];
  lantern.glow_timer += dt;
  changed = true;
  const float lantern_pulse_before = lantern.pulse_timer;
  if (lantern.pulse_timer > 0.0f) {
    lantern.pulse_timer = std::max(0.0f, lantern.pulse_timer - dt);
    changed = true;
  }
  const float pulse = lantern.pulse_timer > 0.0f
      ? std::sin((kLanternLightPulseSeconds - lantern.pulse_timer) * 8.0f) * 0.18f + 0.18f
      : 0.0f;
  const float active_marker = lantern.active
      ? 0.18f + 0.12f * (std::sin(lantern.glow_timer * 3.6f + lantern.position.x) * 0.5f + 0.5f)
      : 0.0f;
  const float deposit_fill = lantern.required_fireflies > 0
      ? static_cast<float>(lantern.deposited_fireflies) / static_cast<float>(lantern.required_fireflies)
      : 1.0f;
  const float deposit_proximity = horizontal_distance(fox_position, lantern.position) <= kFireflyDepositRadius + 1.0f
      ? 0.18f
      : 0.0f;
  lantern.glow_intensity = lantern.lit ? std::min(1.0f, 0.78f + pulse) :
      std::min(1.0f, active_marker + deposit_fill * 0.44f + deposit_proximity);
  if (lantern_pulse_before != lantern.pulse_timer) {
    changed = true;
  }

  bool near_uncollected_firefly = false;
  for (Firefly& firefly : fireflies_) {
    if (!firefly.active || firefly.collected) {
      continue;
    }

    firefly.phase += dt * (0.42f + 0.10f * std::sin(firefly.phase + firefly.twinkle_phase));
    firefly.bob_timer += dt;
    const float drift_x = (std::sin(firefly.phase * 1.17f + firefly.twinkle_phase) * 0.72f +
                           std::cos(firefly.phase * 0.61f + firefly.twinkle_phase * 1.7f) * 0.34f) *
                          kFireflyWanderXz;
    const float drift_z = (std::cos(firefly.phase * 1.03f + firefly.twinkle_phase * 0.8f) * 0.72f +
                           std::sin(firefly.phase * 0.73f + firefly.twinkle_phase * 1.3f) * 0.34f) *
                          kFireflyWanderXz;
    const float bob = std::sin(firefly.bob_timer * 1.85f + firefly.twinkle_phase) *
                      (kFireflyBobHeight + kFireflyWanderY);
    firefly.position = firefly.home + Vec3{drift_x, bob, drift_z};
    const float distance = horizontal_distance(fox_position, firefly.position);
    const float proximity_boost = distance <= 4.8f ? 0.22f : 0.0f;
    const float pulse_speed = distance <= kFireflyCollectRadius + 1.0f ? 7.2f : 3.8f;
    const float twinkle_speed = kFireflyTwinkleSpeed + 0.17f * std::sin(firefly.twinkle_phase * 1.9f);
    const float active_bonus = lantern.squirrel_bonus > 0 &&
                               horizontal_distance(firefly.home, lantern.position) <= kSquirrelLanternBonusRange
        ? 0.22f * static_cast<float>(lantern.squirrel_bonus)
        : 0.0f;
    const float pulse_t = smoothstep(std::sin(firefly.bob_timer * twinkle_speed + firefly.twinkle_phase) *
                                     0.5f + 0.5f);
    const float shimmer_t = smoothstep(std::sin(firefly.bob_timer * pulse_speed + firefly.twinkle_phase * 2.1f) *
                                       0.5f + 0.5f);
    const float slow_fade = lerp(kFireflyTwinkleFadeFloor,
                                 1.0f,
                                 smoothstep(std::sin(firefly.bob_timer * kFireflyTwinkleFadeSpeed +
                                                     firefly.twinkle_phase * 1.37f) *
                                            0.5f + 0.5f));
    const float twinkle = std::min(1.0f, pulse_t * 0.76f + shimmer_t * 0.24f);
    firefly.glow_intensity = std::min(kFireflyGlowMax,
                                      (kFireflyGlowMin +
                                       (kFireflyGlowMax - kFireflyGlowMin) *
                                           twinkle * kFireflyTwinkleDepth) *
                                          slow_fade +
                                          proximity_boost +
                                          active_bonus * 0.08f);
    changed = true;

    if (distance < 9.5f) {
      near_uncollected_firefly = true;
    }
    if (distance <= kFireflyCollectRadius) {
      firefly.collected = true;
      firefly.active = false;
      ++carried_fireflies_;
      changed = true;
      if (audio_ready_for_gameplay_sound()) {
        audio_play_mote_chime(0.72f);
      }
    }
  }

  if (near_uncollected_firefly && firefly_chime_cooldown_ <= 0.0f && audio_ready_for_gameplay_sound()) {
    audio_play_mote_chime(0.24f);
    firefly_chime_cooldown_ = 2.1f;
  }

  if (carried_fireflies_ > 0 &&
      !lantern.lit &&
      deposit_cooldown_ <= 0.0f &&
      horizontal_distance(fox_position, lantern.position) <= kFireflyDepositRadius) {
    const int needed_fireflies = std::max(0, lantern.required_fireflies - lantern.deposited_fireflies);
    const int deposited_now = std::min(carried_fireflies_, needed_fireflies);
    carried_fireflies_ -= deposited_now;
    lantern.deposited_fireflies += deposited_now;
    deposit_cooldown_ = kDepositInterval;
    changed = true;
    if (audio_ready_for_gameplay_sound()) {
      audio_play_mote_chime(0.88f);
    }

    if (lantern.deposited_fireflies >= lantern.required_fireflies) {
      lantern.lit = true;
      lantern.active = false;
      lantern.glow_intensity = 1.0f;
      lantern.pulse_timer = kLanternLightPulseSeconds;
      carried_fireflies_ = 0;
      if (audio_ready_for_gameplay_sound()) {
        audio_play_owl_appear();
        audio_play_mote_chime(1.0f);
      }
      activate_lantern(generator, lantern_sequence_ + 1);
    }
  }

  return changed;
}

}  // namespace voxel
