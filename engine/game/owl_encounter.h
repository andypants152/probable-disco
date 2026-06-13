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
    bool show_subtitle = true;
    bool focus_target = false;
  };

  void init(const TerrainGenerator& generator);
  bool update(float dt, const TerrainGenerator& generator, Vec3 fox_position, bool interact_pressed);
  bool schedule_return(const TerrainGenerator& generator,
                       Vec3 anchor_position,
                       Vec3 fox_position,
                       Vec3 avoid_position);
  bool consume_dialogue_event(DialogueEvent& event);

  Vec3 position() const { return position_; }
  Vec3 perch_position() const { return perch_position_; }
  Vec3 return_perch_position() const { return return_perch_position_; }
  float heading() const { return heading_; }
  float perch_heading() const;
  float return_perch_heading() const { return return_heading_; }
  float wing_pose() const { return wing_pose_; }
  float head_yaw() const { return head_yaw_; }
  float head_pitch() const { return head_pitch_; }
  float head_roll() const { return head_roll_; }
  float body_bob() const { return body_bob_; }
  float blink() const { return blink_; }
  bool visible() const { return state_ != State::Gone; }
  bool return_perch_visible() const { return return_perch_visible_; }
  bool prompt_visible() const { return prompt_visible_; }
  int dialogue_line() const { return dialogue_line_; }
  bool talking() const { return state_ == State::Talking || state_ == State::ReturnTalking; }
  bool flying() const { return state_ == State::Flying || state_ == State::ReturnArriving; }
  bool gone() const { return state_ == State::Gone; }
  bool return_completed() const { return return_completed_; }

 private:
  enum class State {
    Waiting,
    Talking,
    Flying,
    Gone,
    ReturnArriving,
    ReturnTalking,
    ReturnPerched,
  };

  State state_ = State::Waiting;
  Vec3 position_ = {};
  Vec3 perch_position_ = {};
  Vec3 return_perch_position_ = {};
  Vec3 return_start_position_ = {};
  Vec3 return_lantern_position_ = {};
  float heading_ = 0.0f;
  float return_heading_ = 0.0f;
  float wing_pose_ = 0.0f;
  float head_yaw_ = 0.0f;
  float head_pitch_ = 0.0f;
  float head_roll_ = 0.0f;
  float body_bob_ = 0.0f;
  float blink_ = 0.0f;
  float idle_timer_ = 0.0f;
  float timer_ = 0.0f;
  int dialogue_line_ = 0;
  int pending_dialogue_line_ = 0;
  bool prompt_visible_ = false;
  bool return_perch_visible_ = false;
  bool return_completed_ = false;
};

}  // namespace voxel
