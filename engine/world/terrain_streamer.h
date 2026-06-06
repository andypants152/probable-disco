#pragma once

#include <vector>

#include "math/vec3.h"
#include "render/mesh.h"

namespace voxel {

class TerrainGenerator;

class TerrainStreamer {
 public:
  struct Stats {
    int visible_chunks = 0;
    int rebuilt_chunks = 0;
    std::size_t visible_vertices = 0;
    std::size_t visible_triangles = 0;
    std::size_t largest_chunk_vertices = 0;
    std::size_t largest_chunk_triangles = 0;
    std::size_t rebuilt_surface_columns = 0;
    std::size_t skipped_terrain_voxel_samples = 0;
  };

  void init(const TerrainGenerator& generator, Vec3 focus_position);
  bool update(const TerrainGenerator& generator, Vec3 focus_position);

  const Mesh& mesh() const { return terrain_mesh_; }
  const Stats& stats() const { return stats_; }
  bool chunk_changed() const { return chunk_changed_; }
  int center_chunk_x() const { return center_chunk_x_; }
  int center_chunk_z() const { return center_chunk_z_; }

 private:
  struct CachedTerrainChunk {
    int chunk_x = 0;
    int chunk_z = 0;
    int visual_detail_level = 0;
    Mesh mesh;
  };

  void rebuild(const TerrainGenerator& generator);

  std::vector<CachedTerrainChunk> chunk_cache_;
  Mesh terrain_mesh_;
  Stats stats_;
  int center_chunk_x_ = 0;
  int center_chunk_z_ = 0;
  bool chunk_changed_ = false;
};

}  // namespace voxel
