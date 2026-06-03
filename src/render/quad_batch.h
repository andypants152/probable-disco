#pragma once

#include <cstdint>
#include <vector>

#include "render/render_types.h"

namespace voxel {

struct QuadVertex {
  float position[2] = {};
  float tex_coord[2] = {};
  PackedColor color = pack_rgba(255, 255, 255, 255);
};

class QuadBatch {
 public:
  void clear();
  void add(float x,
           float y,
           float width,
           float height,
           float u0,
           float v0,
           float u1,
           float v1,
           PackedColor color);

  const std::vector<QuadVertex>& vertices() const { return vertices_; }
  const std::vector<Index>& indices() const { return indices_; }
  bool empty() const { return indices_.empty(); }

 private:
  std::vector<QuadVertex> vertices_;
  std::vector<Index> indices_;
};

}  // namespace voxel
