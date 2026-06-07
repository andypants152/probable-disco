#pragma once

#include <array>
#include <vector>

#include "math/vec3.h"
#include "render/lights.h"
#include "render/mesh.h"

namespace voxel {

class TerrainGenerator;

class FireflyLoop {
 public:
  void init(const TerrainGenerator& generator);
  void unlock_fireflies(const TerrainGenerator& generator);
  bool update(float dt, const TerrainGenerator& generator, Vec3 fox_position, float fox_heading);

  void append_dynamic_mesh(Mesh& mesh, Vec3 fox_position, float fox_heading) const;
  void append_gameplay_lights(std::array<GameplayLight, kMaxGameplayLights>& lights,
                              int& light_count,
                              int light_limit,
                              Vec3 fox_position,
                              float fox_heading) const;

  Vec3 objective_position(Vec3 fox_position) const;
  Vec3 farthest_firefly_position(Vec3 fox_position) const;

  int carried_fireflies() const { return carried_fireflies_; }
  int active_lantern_index() const { return lantern_sequence_; }
  int deposited_fireflies() const;
  int required_fireflies() const;
  int active_firefly_count() const;
  bool fireflies_unlocked() const { return fireflies_unlocked_; }

  float firefly_glow_intensity() const;
  float lantern_light_intensity() const;
  float lantern_light_radius() const;
  bool blocks_acorn_spawn(Vec3 position, float radius) const;
  bool has_lit_lantern_near(Vec3 position, float radius) const;
  void lit_lantern_positions(std::vector<Vec3>& positions) const;

  void dev_collect_active_fireflies();
  void dev_deposit_carried_fireflies(const TerrainGenerator& generator);
  void add_squirrel_completion_bonus(Vec3 position);

 private:
  static constexpr int kMaxFireflies = 12;

  struct Firefly {
    Vec3 home = {};
    Vec3 position = {};
    Vec3 velocity = {};
    float phase = 0.0f;
    float bob_timer = 0.0f;
    float twinkle_phase = 0.0f;
    float glow_intensity = 1.0f;
    bool collected = false;
    bool active = false;
  };

  struct Lantern {
    Vec3 position = {};
    int required_fireflies = 3;
    int deposited_fireflies = 0;
    bool lit = false;
    bool active = false;
    float glow_intensity = 0.0f;
    float glow_timer = 0.0f;
    float pulse_timer = 0.0f;
    int squirrel_bonus = 0;
  };

  void activate_lantern(const TerrainGenerator& generator, int sequence);
  Vec3 lantern_position_for_sequence(const TerrainGenerator& generator, int sequence) const;
  void spawn_fireflies_for_lantern(const TerrainGenerator& generator, int index);
  Vec3 carried_firefly_position(Vec3 fox_position, float fox_heading, int index) const;
  float carried_firefly_glow_intensity(int index) const;

  std::vector<Lantern> lanterns_;
  std::array<Firefly, kMaxFireflies> fireflies_ = {};
  int active_lantern_index_ = 0;
  int lantern_sequence_ = 0;
  int carried_fireflies_ = 0;
  bool fireflies_unlocked_ = false;
  float firefly_orbit_timer_ = 0.0f;
  float firefly_chime_cooldown_ = 1.0f;
  float deposit_cooldown_ = 0.0f;
};

}  // namespace voxel
