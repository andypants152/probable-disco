#include "owl_encounter.h"

#include <algorithm>
#include <cmath>

#include "core/audio.h"
#include "core/subtitles.h"
#include "world/mesher.h"

namespace voxel {

namespace {

constexpr float kOwlEncounterRadius = 18.0f;
constexpr float kOwlDefaultHeading = 3.14159265358979323846f;
constexpr float kOwlFlyAwayHeading = 0.0f;
constexpr float kOwlTalkSeconds = 5.35f;
constexpr float kOwlFlySeconds = 2.6f;
constexpr float kOwlSecondLineTime = 1.95f;

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

}  // namespace

void OwlEncounter::init(const TerrainGenerator& generator) {
  state_ = State::Waiting;
  perch_position_ = owl_perch_position(generator);
  position_ = perch_position_;
  heading_ = kOwlDefaultHeading;
  wing_pose_ = 0.0f;
  timer_ = 0.0f;
  dialogue_line_ = 0;
  pending_dialogue_line_ = 0;
  prompt_visible_ = false;
}

bool OwlEncounter::update(float dt, const TerrainGenerator& generator, Vec3 fox_position, bool interact_pressed) {
  const State previous_state = state_;
  const Vec3 previous_position = position_;
  const float previous_heading = heading_;
  const float previous_wing_pose = wing_pose_;

  perch_position_ = owl_perch_position(generator);
  dt = std::max(0.0f, std::min(dt, 0.10f));

  if (state_ == State::Waiting) {
    position_ = perch_position_;
    heading_ = kOwlDefaultHeading;
    wing_pose_ = 0.0f;
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
    wing_pose_ = 0.0f;
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
    if (timer_ >= kOwlFlySeconds) {
      state_ = State::Gone;
      wing_pose_ = 0.0f;
    }
  }

  return previous_state != state_ ||
      length(previous_position - position_) > 0.0005f ||
      std::fabs(previous_heading - heading_) > 0.0005f ||
      std::fabs(previous_wing_pose - wing_pose_) > 0.0005f;
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
  } else {
    event.text = "The little lights are scattered. Bring them home.";
    event.seconds = 3.35f;
  }
  pending_dialogue_line_ = 0;
  return true;
}

float OwlEncounter::perch_heading() const {
  return kOwlDefaultHeading;
}

}  // namespace voxel
