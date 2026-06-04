#include "quad_batch.h"

namespace voxel {

void QuadBatch::clear() {
  vertices_.clear();
  indices_.clear();
}

void QuadBatch::add(float x,
                    float y,
                    float width,
                    float height,
                    float u0,
                    float v0,
                    float u1,
                    float v1,
                    PackedColor color) {
  const Index base = static_cast<Index>(vertices_.size());
  vertices_.push_back({{x, y}, {u0, v0}, color});
  vertices_.push_back({{x + width, y}, {u1, v0}, color});
  vertices_.push_back({{x + width, y + height}, {u1, v1}, color});
  vertices_.push_back({{x, y + height}, {u0, v1}, color});

  indices_.push_back(base + 0);
  indices_.push_back(base + 1);
  indices_.push_back(base + 2);
  indices_.push_back(base + 0);
  indices_.push_back(base + 2);
  indices_.push_back(base + 3);
}

}  // namespace voxel
