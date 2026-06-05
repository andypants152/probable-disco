#include "firefly_loop.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

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
constexpr float kLanternLightRadius = 18.0f;
constexpr float kLanternLightIntensity = 2.0f;
constexpr float kLanternGroundGlowRadius = 9.0f;
constexpr float kLanternLightPulseDuration = 2.4f;
constexpr float kLanternLightPulseRadius = 22.0f;
constexpr float kFireflyLightRadius = 4.4f;
constexpr float kCarriedFireflyLightRadius = 4.0f;
constexpr float kFireflyLightFullDistance = 18.0f;
constexpr float kFireflyLightCullDistance = 30.0f;
constexpr float kLanternLightFullDistance = 30.0f;
constexpr float kLanternLightCullDistance = 52.0f;
constexpr int kMaxLitLanternLights = 3;
constexpr float kFireflyCollectRadius = 1.85f;
constexpr float kFireflyDepositRadius = 3.15f;
constexpr float kFireflyClusterRadius = kLanternSpawnClearRadius;
constexpr float kFireflyHeight = 2.65f;
constexpr float kDepositInterval = 0.16f;
constexpr float kLanternLightPulseSeconds = kLanternLightPulseDuration;
constexpr std::array<Vec3, 4> kStarterLanternAnchors = {{
    {7.0f, 0.0f, -14.0f},
    {30.0f, 0.0f, -38.0f},
    {54.0f, 0.0f, -70.0f},
    {22.0f, 0.0f, -103.0f},
}};
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

Vec3 lantern_anchor_for_sequence(int sequence) {
  if (sequence < static_cast<int>(kStarterLanternAnchors.size())) {
    return kStarterLanternAnchors[static_cast<std::size_t>(sequence)];
  }

  const float step = static_cast<float>(sequence);
  const float forward = -102.0f - static_cast<float>(sequence - 3) * 27.0f;
  const float weave = std::sin(step * 1.31f) * 38.0f + std::sin(step * 0.47f) * 14.0f;
  return {weave, 0.0f, forward};
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

void FireflyLoop::init(const TerrainGenerator& generator) {
  carried_fireflies_ = 0;
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
  lantern.position = terrain_position(generator, lantern_anchor_for_sequence(sequence), 0.10f);
  lantern.active = true;
  lantern.lit = false;
  lantern.deposited_fireflies = 0;
  lantern.required_fireflies = 3;
  lantern.glow_intensity = 0.0f;
  lantern.glow_timer = 0.0f;
  lantern.pulse_timer = 0.0f;
  spawn_fireflies_for_lantern(generator, active_lantern_index_);
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

  for (int i = 0; i < carried_fireflies_; ++i) {
    const float glow = carried_firefly_glow_intensity(i);
    add_gameplay_light(lights,
                       light_count,
                       light_limit,
                       carried_firefly_position(fox_position, fox_heading, i),
                       mote_color,
                       kCarriedFireflyLightRadius,
                       0.24f + glow * 0.36f);
  }

  for (const Firefly& firefly : fireflies_) {
    if (!firefly.active || firefly.collected) {
      continue;
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
                       (0.30f + firefly.glow_intensity * 0.44f) * light_fade);
  }

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
    const float pulse = lit_lantern.pulse_timer > 0.0f ? (1.0f - pulse_t) * 0.8f : 0.0f;
    add_gameplay_light(lights,
                       light_count,
                       light_limit,
                       lantern_light_position,
                       lantern_color,
                       kLanternLightRadius + pulse * kLanternLightPulseRadius,
                       (kLanternLightIntensity + pulse) * light_fade);
    add_gameplay_light(lights,
                       light_count,
                       light_limit,
                       lit_lantern.position + Vec3{0.0f, 0.26f, 0.0f},
                       lantern_color,
                       kLanternGroundGlowRadius,
                       0.42f * light_fade);
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
    lit_lantern.glow_intensity = std::min(1.0f, 0.82f + pulse);
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
                                          proximity_boost);
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
