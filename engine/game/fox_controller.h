#pragma once

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

 private:
  Vec3 position_ = {};
  float heading_ = 0.0f;
  float movement_speed_ = 0.0f;
  bool moved_this_frame_ = false;
};

}  // namespace voxel
