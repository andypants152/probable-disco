#include "conversation_controller.h"

#include <algorithm>
#include <cmath>

#include "core/subtitles.h"

namespace voxel {

namespace {

constexpr float kEaseInSeconds = 0.40f;
constexpr float kEaseOutSeconds = 0.30f;

float smoothstep(float value) {
  value = std::max(0.0f, std::min(1.0f, value));
  return value * value * (3.0f - 2.0f * value);
}

float lerp(float a, float b, float t) {
  return a + (b - a) * t;
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

Vec3 horizontal_direction(Vec3 from, Vec3 to, Vec3 fallback) {
  Vec3 direction = {to.x - from.x, 0.0f, to.z - from.z};
  if (length(direction) <= 0.001f) {
    direction = {fallback.x, 0.0f, fallback.z};
  }
  if (length(direction) <= 0.001f) {
    return {0.0f, 0.0f, 1.0f};
  }
  return normalize(direction);
}

}  // namespace

void ConversationController::begin(const Camera& camera, const Request& request) {
  begin_internal(camera, request, true);
}

void ConversationController::replace_line(const Camera& camera, const Request& request) {
  if (phase_ == Phase::Idle) {
    begin_internal(camera, request, true);
    return;
  }
  begin_internal(camera, request, false);
}

void ConversationController::set_speaker_position(Vec3 speaker_position) {
  speaker_position_ = speaker_position;
}

void ConversationController::begin_internal(const Camera& camera, const Request& request, bool reset_return_pose) {
  phase_ = Phase::EaseIn;
  start_pose_ = pose_from_camera(camera);
  target_pose_ = conversation_pose(camera, request);
  if (reset_return_pose) {
    return_pose_ = start_pose_;
  }
  return_start_pose_ = start_pose_;
  speaker_position_ = request.speaker_position;
  listener_position_ = request.listener_position;
  focus_position_ = request.focus_position;
  speaker_id_ = request.speaker_id;
  shot_ = request.shot;
  show_subtitle_ = request.show_subtitle && request.text != nullptr && request.text[0] != '\0';
  phase_timer_ = 0.0f;
  subtitle_timer_ = 0.0f;
  subtitle_seconds_ = std::max(0.2f, request.seconds);
  if (show_subtitle_) {
    subtitles_show(request.text, subtitle_seconds_);
  }
}

bool ConversationController::update(float dt, bool confirm_pressed, Camera& camera) {
  if (phase_ == Phase::Idle) {
    return false;
  }

  const CameraPose previous_pose = pose_from_camera(camera);
  dt = std::max(0.0f, std::min(dt, 0.10f));

  if ((phase_ == Phase::EaseIn || phase_ == Phase::Talking) && confirm_pressed) {
    begin_return(camera, true);
  }

  if (shot_ == Shot::FollowSpeaker) {
    Request request = {};
    request.speaker_position = speaker_position_;
    request.listener_position = listener_position_;
    request.focus_position = focus_position_;
    request.shot = shot_;
    target_pose_ = conversation_pose(camera, request);
  }

  if (phase_ == Phase::EaseIn) {
    phase_timer_ += dt;
    subtitle_timer_ += dt;
    const float t = smoothstep(phase_timer_ / kEaseInSeconds);
    apply_pose(camera, mix_pose(start_pose_, target_pose_, t));
    if (phase_timer_ >= kEaseInSeconds) {
      phase_ = Phase::Talking;
      phase_timer_ = 0.0f;
    }
  } else if (phase_ == Phase::Talking) {
    subtitle_timer_ += dt;
    apply_pose(camera, target_pose_);
    if (subtitle_timer_ >= subtitle_seconds_) {
      begin_return(camera, false);
    }
  } else if (phase_ == Phase::EaseOut) {
    phase_timer_ += dt;
    const float t = smoothstep(phase_timer_ / kEaseOutSeconds);
    apply_pose(camera, mix_pose(return_start_pose_, return_pose_, t));
    if (phase_timer_ >= kEaseOutSeconds) {
      apply_pose(camera, return_pose_);
      phase_ = Phase::Idle;
      speaker_id_ = 0;
      show_subtitle_ = false;
    }
  }

  const CameraPose current_pose = pose_from_camera(camera);
  return length(current_pose.position - previous_pose.position) > 0.0005f ||
      std::fabs(current_pose.yaw - previous_pose.yaw) > 0.0005f ||
      std::fabs(current_pose.pitch - previous_pose.pitch) > 0.0005f;
}

ConversationController::CameraPose ConversationController::pose_from_camera(const Camera& camera) {
  return {camera.position, camera.yaw, camera.pitch};
}

void ConversationController::apply_pose(Camera& camera, CameraPose pose) {
  camera.position = pose.position;
  camera.yaw = pose.yaw;
  camera.pitch = pose.pitch;
}

ConversationController::CameraPose ConversationController::conversation_pose(const Camera& camera,
                                                                             const Request& request) {
  if (request.shot == Shot::SpeakerCloseUp) {
    return speaker_close_up_pose(camera, request.listener_position, request.speaker_position);
  }
  if (request.shot == Shot::LookAtFocus) {
    return look_at_focus_pose(camera, request.listener_position, request.focus_position);
  }
  if (request.shot == Shot::FollowSpeaker) {
    return follow_speaker_pose(camera, request.listener_position, request.speaker_position);
  }
  return over_shoulder_pose(camera, request.listener_position, request.speaker_position);
}

ConversationController::CameraPose ConversationController::over_shoulder_pose(const Camera& camera,
                                                                              Vec3 fox_position,
                                                                              Vec3 speaker_position) {
  const Vec3 fox_to_speaker = horizontal_direction(fox_position, speaker_position, camera.forward());
  Vec3 shoulder = normalize(cross({0.0f, 1.0f, 0.0f}, fox_to_speaker));
  const Vec3 midpoint = fox_position * 0.5f + speaker_position * 0.5f;
  const Vec3 camera_side = {camera.position.x - midpoint.x, 0.0f, camera.position.z - midpoint.z};
  if (dot(camera_side, shoulder) < 0.0f) {
    shoulder = shoulder * -1.0f;
  }
  const Vec3 camera_position = fox_position -
      fox_to_speaker * 2.35f +
      shoulder * 3.15f +
      Vec3{0.0f, 2.05f, 0.0f};
  const Vec3 look_target = fox_position * 0.28f + speaker_position * 0.72f + Vec3{0.0f, 1.02f, 0.0f};
  const Vec3 look_direction = normalize(look_target - camera_position);
  const float yaw = std::atan2(look_direction.z, look_direction.x);
  const float pitch = std::asin(std::max(-1.0f, std::min(1.0f, look_direction.y)));
  return {camera_position, yaw, pitch};
}

ConversationController::CameraPose ConversationController::speaker_close_up_pose(const Camera& camera,
                                                                                 Vec3 listener_position,
                                                                                 Vec3 speaker_position) {
  const Vec3 listener_to_speaker = horizontal_direction(listener_position, speaker_position, camera.forward());
  Vec3 shoulder = normalize(cross({0.0f, 1.0f, 0.0f}, listener_to_speaker));
  const Vec3 camera_side = {camera.position.x - speaker_position.x, 0.0f, camera.position.z - speaker_position.z};
  if (dot(camera_side, shoulder) < 0.0f) {
    shoulder = shoulder * -1.0f;
  }
  const Vec3 camera_position = speaker_position -
      listener_to_speaker * 2.35f +
      shoulder * 1.05f +
      Vec3{0.0f, 1.18f, 0.0f};
  const Vec3 look_target = speaker_position + Vec3{0.0f, 0.80f, 0.0f};
  const Vec3 look_direction = normalize(look_target - camera_position);
  const float yaw = std::atan2(look_direction.z, look_direction.x);
  const float pitch = std::asin(std::max(-1.0f, std::min(1.0f, look_direction.y)));
  return {camera_position, yaw, pitch};
}

ConversationController::CameraPose ConversationController::look_at_focus_pose(const Camera& camera,
                                                                              Vec3 listener_position,
                                                                              Vec3 focus_position) {
  const Vec3 listener_to_focus = horizontal_direction(listener_position, focus_position, camera.forward());
  Vec3 shoulder = normalize(cross({0.0f, 1.0f, 0.0f}, listener_to_focus));
  const Vec3 camera_side = {camera.position.x - listener_position.x, 0.0f, camera.position.z - listener_position.z};
  if (dot(camera_side, shoulder) < 0.0f) {
    shoulder = shoulder * -1.0f;
  }
  const Vec3 camera_position = listener_position -
      listener_to_focus * 2.6f +
      shoulder * 2.15f +
      Vec3{0.0f, 1.95f, 0.0f};
  const Vec3 look_target = focus_position + Vec3{0.0f, 0.18f, 0.0f};
  const Vec3 look_direction = normalize(look_target - camera_position);
  const float yaw = std::atan2(look_direction.z, look_direction.x);
  const float pitch = std::asin(std::max(-1.0f, std::min(1.0f, look_direction.y)));
  return {camera_position, yaw, pitch};
}

ConversationController::CameraPose ConversationController::follow_speaker_pose(const Camera& camera,
                                                                               Vec3 listener_position,
                                                                               Vec3 speaker_position) {
  const Vec3 listener_to_speaker = horizontal_direction(listener_position, speaker_position, camera.forward());
  Vec3 shoulder = normalize(cross({0.0f, 1.0f, 0.0f}, listener_to_speaker));
  const Vec3 midpoint = listener_position * 0.45f + speaker_position * 0.55f;
  const Vec3 camera_side = {camera.position.x - midpoint.x, 0.0f, camera.position.z - midpoint.z};
  if (dot(camera_side, shoulder) < 0.0f) {
    shoulder = shoulder * -1.0f;
  }
  const Vec3 camera_position = listener_position -
      listener_to_speaker * 2.15f +
      shoulder * 3.35f +
      Vec3{0.0f, 2.0f, 0.0f};
  const Vec3 look_target = speaker_position + Vec3{0.0f, 0.74f, 0.0f};
  const Vec3 look_direction = normalize(look_target - camera_position);
  const float yaw = std::atan2(look_direction.z, look_direction.x);
  const float pitch = std::asin(std::max(-1.0f, std::min(1.0f, look_direction.y)));
  return {camera_position, yaw, pitch};
}

ConversationController::CameraPose ConversationController::mix_pose(CameraPose a, CameraPose b, float t) {
  t = std::max(0.0f, std::min(1.0f, t));
  return {
      a.position + (b.position - a.position) * t,
      a.yaw + shortest_angle_delta(a.yaw, b.yaw) * t,
      lerp(a.pitch, b.pitch, t),
  };
}

void ConversationController::begin_return(const Camera& camera, bool clear_subtitle) {
  if (clear_subtitle) {
    subtitles_clear();
  }
  phase_ = Phase::EaseOut;
  phase_timer_ = 0.0f;
  return_start_pose_ = pose_from_camera(camera);
}

}  // namespace voxel
