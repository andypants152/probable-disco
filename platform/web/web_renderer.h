#pragma once

#include <cstddef>
#include <cstdint>

#include <GLES3/gl3.h>
#include <emscripten/html5.h>

#include "platform.h"

namespace voxel {

class WebRenderer final : public Renderer {
 public:
  bool init() override;
  void upload_mesh(const Mesh& mesh) override;
  bool supports_separate_meshes() const override { return true; }
  void upload_static_mesh(const Mesh& mesh) override;
  void upload_dynamic_mesh(const Mesh& mesh) override;
  void upload_subtitle(const SubtitleFrame& subtitle) override;
  void upload_gameplay_lights(const GameplayLight* lights, int count) override;
  void render_frame(const Camera& camera) override;
  void shutdown() override;

 private:
  struct GpuVertex {
    float position[3];
    float normal[3];
    unsigned char color[4];
    float micro_position[3];
  };

  struct MeshBuffer {
    GLuint vao = 0;
    GLuint vertex_buffer = 0;
    GLuint index_buffer = 0;
    GLsizei index_count = 0;
  };

  GLuint compile_shader(GLenum type, const char* source);
  bool create_program();
  bool create_overlay_program();
  void upload_buffer(MeshBuffer& buffer, const Mesh& mesh, GLenum usage);
  void draw_buffer(const MeshBuffer& buffer);
  void draw_subtitle(int framebuffer_width, int framebuffer_height);
  void destroy_buffer(MeshBuffer& buffer);
  void destroy_buffers();

  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context_ = 0;
  GLuint program_ = 0;
  GLuint overlay_program_ = 0;
  GLuint overlay_vao_ = 0;
  GLuint subtitle_texture_ = 0;
  std::uint32_t subtitle_generation_ = 0;
  int subtitle_width_ = 0;
  int subtitle_height_ = 0;
  float subtitle_alpha_ = 1.0f;
  bool subtitle_visible_ = false;
  MeshBuffer static_mesh_;
  MeshBuffer dynamic_mesh_;
  GLint view_uniform_ = -1;
  GLint projection_uniform_ = -1;
  GLint light_count_uniform_ = -1;
  GLint light_position_radius_uniform_ = -1;
  GLint light_color_intensity_uniform_ = -1;
  float light_position_radius_[kMaxRendererGameplayLights * 4] = {};
  float light_color_intensity_[kMaxRendererGameplayLights * 4] = {};
  int light_count_ = 0;
};

}  // namespace voxel
