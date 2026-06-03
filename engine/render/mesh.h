#pragma once

#include <vector>

#include "math/vec3.h"
#include "render/render_types.h"

namespace voxel {

struct Mesh {
  std::vector<Vec3> vertices;
  std::vector<Vec3> normals;
  std::vector<PackedColor> colors;
  std::vector<Vec3> micro_positions;
  std::vector<Index> indices;

  void clear() {
    vertices.clear();
    normals.clear();
    colors.clear();
    micro_positions.clear();
    indices.clear();
  }
};

}  // namespace voxel
