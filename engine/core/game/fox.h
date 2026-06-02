#pragma once

#include "math/vec3.h"
#include "render/mesh.h"

namespace voxel {

void append_fox_mesh(Mesh& mesh, Vec3 ground_center, float heading_radians);

}  // namespace voxel
