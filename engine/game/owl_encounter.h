#pragma once

#include "math/vec3.h"

namespace voxel {

class TerrainGenerator;

class OwlEncounter {
 public:
  struct DialogueEvent {
    int line = 0;
    Vec3 target_position = {};
    const char* text = "";
    float seconds = 2.0f;
  };

  void init(const TerrainGenerator& generator);
  bool update(float dt, const TerrainGenerator& generator, Vec3 fox_position, bool interact_pressed);
  bool consume_dialogue_event(DialogueEvent& event);

  Vec3 position() const { return position_; }
  Vec3 perch_position() const { return perch_position_; }
  float heading() const { return heading_; }
  float perch_heading() const;
  float wing_pose() const { return wing_pose_; }
  bool visible() const { return state_ != State::Gone; }
  bool prompt_visible() const { return prompt_visible_; }
  int dialogue_line() const { return dialogue_line_; }
  bool talking() const { return state_ == State::Talking; }
  bool flying() const { return state_ == State::Flying; }
  bool gone() const { return state_ == State::Gone; }

 private:
  enum class State {
    Waiting,
    Talking,
    Flying,
    Gone,
  };

  State state_ = State::Waiting;
  Vec3 position_ = {};
  Vec3 perch_position_ = {};
  float heading_ = 0.0f;
  float wing_pose_ = 0.0f;
  float timer_ = 0.0f;
  int dialogue_line_ = 0;
  int pending_dialogue_line_ = 0;
  bool prompt_visible_ = false;
};

}  // namespace voxel
