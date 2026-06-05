#pragma once

#include <cstdint>

#include "game/camera.h"
#include "math/vec3.h"

namespace voxel {

class ConversationController {
 public:
  enum class Shot {
    OverShoulder,
    SpeakerCloseUp,
    LookAtFocus,
    FollowSpeaker,
  };

  struct Request {
    Vec3 speaker_position = {};
    Vec3 listener_position = {};
    Vec3 focus_position = {};
    std::uint32_t speaker_id = 0;
    const char* text = "";
    float seconds = 2.0f;
    Shot shot = Shot::OverShoulder;
    bool show_subtitle = true;
  };

  void begin(const Camera& camera, const Request& request);
  void replace_line(const Camera& camera, const Request& request);
  void set_speaker_position(Vec3 speaker_position);
  bool update(float dt, bool confirm_pressed, Camera& camera);

  bool active() const { return phase_ != Phase::Idle; }
  bool locks_input() const { return active(); }
  bool talking() const { return show_subtitle_ && (phase_ == Phase::EaseIn || phase_ == Phase::Talking); }
  std::uint32_t speaker_id() const { return active() ? speaker_id_ : 0; }

 private:
  enum class Phase {
    Idle,
    EaseIn,
    Talking,
    EaseOut,
  };

  struct CameraPose {
    Vec3 position = {};
    float yaw = 0.0f;
    float pitch = 0.0f;
  };

  static CameraPose pose_from_camera(const Camera& camera);
  static void apply_pose(Camera& camera, CameraPose pose);
  static CameraPose conversation_pose(const Camera& camera, const Request& request);
  static CameraPose over_shoulder_pose(const Camera& camera, Vec3 fox_position, Vec3 speaker_position);
  static CameraPose speaker_close_up_pose(const Camera& camera, Vec3 listener_position, Vec3 speaker_position);
  static CameraPose look_at_focus_pose(const Camera& camera, Vec3 listener_position, Vec3 focus_position);
  static CameraPose follow_speaker_pose(const Camera& camera, Vec3 listener_position, Vec3 speaker_position);
  static CameraPose mix_pose(CameraPose a, CameraPose b, float t);

  void begin_internal(const Camera& camera, const Request& request, bool reset_return_pose);
  void begin_return(const Camera& camera, bool clear_subtitle);

  Phase phase_ = Phase::Idle;
  CameraPose start_pose_ = {};
  CameraPose target_pose_ = {};
  CameraPose return_pose_ = {};
  CameraPose return_start_pose_ = {};
  Vec3 speaker_position_ = {};
  Vec3 listener_position_ = {};
  Vec3 focus_position_ = {};
  std::uint32_t speaker_id_ = 0;
  Shot shot_ = Shot::OverShoulder;
  bool show_subtitle_ = false;
  float phase_timer_ = 0.0f;
  float subtitle_timer_ = 0.0f;
  float subtitle_seconds_ = 0.0f;
};

}  // namespace voxel
