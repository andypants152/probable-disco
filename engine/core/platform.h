#pragma once

#include "game/camera.h"
#include "math/vec3.h"
#include "render/mesh.h"

namespace voxel {

struct SubtitleFrame;
struct GameplayLight {
  Vec3 position = {};
  Vec3 color = {1.0f, 1.0f, 1.0f};
  float radius = 1.0f;
  float intensity = 1.0f;
  bool active = false;
};

constexpr int kMaxGameplayLights = 32;
constexpr int kMaxRendererGameplayLights = 8;

struct Renderer {
  virtual ~Renderer() = default;
  virtual bool init() = 0;
  virtual void upload_mesh(const Mesh& mesh) = 0;
  virtual bool supports_separate_meshes() const { return false; }
  virtual void upload_static_mesh(const Mesh& mesh) { upload_mesh(mesh); }
  virtual void upload_dynamic_mesh(const Mesh& mesh) { upload_mesh(mesh); }
  virtual void upload_subtitle(const SubtitleFrame&) {}
  virtual void upload_gameplay_lights(const GameplayLight*, int) {}
  virtual void render_frame(const Camera& camera) = 0;
  virtual void shutdown() = 0;
};

}  // namespace voxel
