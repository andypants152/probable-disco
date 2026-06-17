#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "math/vec3.h"
#include "render/mesh.h"

namespace voxel {

class FireflyLoop;
class SquirrelQuest;
class TerrainGenerator;

class RabbitBurrows {
 public:
  struct DialogueEvent {
    Vec3 rabbit_position = {};
    std::uint32_t rabbit_id = 0;
    char text[96] = {};
    float seconds = 2.0f;
  };

  struct UpdateResult {
    bool structural_changed = false;
    bool animation_changed = false;
  };

  void init(const TerrainGenerator& generator,
            const FireflyLoop& firefly_loop,
            const SquirrelQuest& squirrel_quest);
  UpdateResult update(float dt,
                      const TerrainGenerator& generator,
                      const FireflyLoop& firefly_loop,
                      const SquirrelQuest& squirrel_quest,
                      Vec3 fox_position,
                      bool interact_pressed,
                      bool allow_dialogue);

  void append_dynamic_mesh(Mesh& mesh, Vec3 fox_position) const;
  bool knock_prompt_visible(Vec3 fox_position) const;
  bool rabbit_position(std::uint32_t rabbit_id, Vec3& position) const;
  void drain_dialogue_events(std::vector<DialogueEvent>& events);

 private:
  struct Burrow {
    std::uint32_t id = 0;
    Vec3 position = {};
    float heading = 0.0f;
    int lantern_index = -1;
    float animation_timer = 0.0f;
    float pop_progress = 0.0f;
    float second_line_delay = 0.0f;
    bool popped_out = false;
    bool first_dialogue_played = false;
    bool second_dialogue_played = false;
    bool active = false;
  };

  void refresh_nearby(const TerrainGenerator& generator,
                      const FireflyLoop& firefly_loop,
                      const SquirrelQuest& squirrel_quest,
                      Vec3 fox_position);
  bool add_burrow_candidate(const TerrainGenerator& generator,
                            const FireflyLoop& firefly_loop,
                            const SquirrelQuest& squirrel_quest,
                            const std::vector<Vec3>& lantern_positions,
                            int lantern_index);
  Burrow* nearest_knockable_burrow(Vec3 fox_position, float max_distance);
  const Burrow* nearest_knockable_burrow(Vec3 fox_position, float max_distance) const;
  void queue_dialogue(Burrow& burrow, const char* text, float seconds);

  std::vector<Burrow> burrows_;
  std::unordered_set<std::uint32_t> known_burrow_ids_;
  std::vector<Vec3> lit_lantern_positions_;
  std::vector<DialogueEvent> dialogue_events_;
  float refresh_timer_ = 0.0f;
};

}  // namespace voxel
