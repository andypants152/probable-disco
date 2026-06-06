#pragma once

#include "game/camera.h"
#include "render/mesh.h"
#include "render/lights.h"
#include "render/render_commands.h"

namespace voxel {

struct SubtitleFrame;

struct Renderer {
  virtual ~Renderer() = default;
  virtual bool init() = 0;
  virtual void begin_frame_stats() {}
  virtual void upload_mesh(const Mesh& mesh) = 0;
  virtual bool supports_separate_meshes() const { return false; }
  virtual void upload_static_mesh(const Mesh& mesh) { upload_mesh(mesh); }
  virtual void upload_dynamic_mesh(const Mesh& mesh) { upload_mesh(mesh); }
  virtual void render_frame(const RenderFrame& frame) = 0;
  virtual void shutdown() = 0;
};

}  // namespace voxel
