#pragma once

#include "game/fox.h"
#include "game/camera.h"
#include "math/vec3.h"

namespace voxel {

class TerrainGenerator;

class FoxController {
 public:
  void init(const TerrainGenerator& generator);
  bool update(const CameraInput& input, const TerrainGenerator& generator, const Camera& camera);

  Vec3 position() const { return position_; }
  Vec3 forward() const;
  float heading() const { return heading_; }
  float movement_speed() const { return movement_speed_; }
  bool moved_this_frame() const { return moved_this_frame_; }
  bool animation_changed() const { return animation_changed_; }
  const FoxAnimationPose& animation_pose() const { return animation_pose_; }

 private:
  void update_animation(float delta_time);

  Vec3 position_ = {};
  float heading_ = 0.0f;
  float movement_speed_ = 0.0f;
  float walk_blend_ = 0.0f;
  float walk_cycle_ = 0.0f;
  float idle_time_ = 0.0f;
  bool moved_this_frame_ = false;
  bool animation_changed_ = false;
  FoxAnimationPose animation_pose_;
};

}  // namespace voxel
