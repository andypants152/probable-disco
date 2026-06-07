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

}  // namespace

void OwlEncounter::init(const TerrainGenerator& generator) {
  state_ = State::Waiting;
  perch_position_ = owl_perch_position(generator);
  position_ = perch_position_;
  heading_ = kOwlDefaultHeading;
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
  }

  if (state_ == State::Waiting || state_ == State::Talking) {
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
  if (state_ == State::Waiting || state_ == State::Talking) {
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
