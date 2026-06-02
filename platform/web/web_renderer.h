#pragma once

#include <cstddef>

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
  void upload_buffer(MeshBuffer& buffer, const Mesh& mesh, GLenum usage);
  void draw_buffer(const MeshBuffer& buffer);
  void destroy_buffer(MeshBuffer& buffer);
  void destroy_buffers();

  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context_ = 0;
  GLuint program_ = 0;
  MeshBuffer static_mesh_;
  MeshBuffer dynamic_mesh_;
  GLint view_uniform_ = -1;
  GLint projection_uniform_ = -1;
};

}  // namespace voxel
