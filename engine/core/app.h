#pragma once

#include <cstdint>

#include "game/camera.h"
#include "platform.h"
#include "render/mesh.h"
#include "world/generator.h"

namespace voxel {

class App {
 public:
  bool init(Renderer& renderer);
  void frame(Renderer& renderer, const CameraInput& input);
  void shutdown(Renderer& renderer);

  const Mesh& mesh() const { return mesh_; }
  const Mesh& terrain_mesh() const { return terrain_mesh_; }
  const Mesh& fox_mesh() const { return fox_mesh_; }
  Camera& camera() { return camera_; }
  const Camera& camera() const { return camera_; }

  struct FrameStats {
    std::uint64_t total_ns = 0;
    std::uint64_t update_ns = 0;
    std::uint64_t world_rebuild_ns = 0;
    std::uint64_t fox_rebuild_ns = 0;
    std::uint64_t scene_rebuild_ns = 0;
    std::uint64_t upload_ns = 0;
    std::uint64_t render_ns = 0;
    bool fox_moved = false;
    bool chunk_changed = false;
  };

  const FrameStats& frame_stats() const { return frame_stats_; }

 private:
  void rebuild_world_mesh();
  void rebuild_fox_mesh();
  void rebuild_scene_mesh();
  bool update_fox(const CameraInput& input);
  void update_camera(const CameraInput& input);

  TerrainGenerator generator_;
  Mesh terrain_mesh_;
  Mesh fox_mesh_;
  Mesh mesh_;
  Camera camera_;
  Vec3 fox_position_ = {};
  float fox_heading_ = 0.0f;
  int world_center_chunk_x_ = 0;
  int world_center_chunk_z_ = 0;
  bool initialized_ = false;
  FrameStats frame_stats_;
};

}  // namespace voxel
