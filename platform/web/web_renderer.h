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
  void render_frame(const Camera& camera) override;
  void shutdown() override;

 private:
  struct GpuVertex {
    float position[3];
    float normal[3];
    unsigned char color[4];
    float micro_position[3];
  };

  GLuint compile_shader(GLenum type, const char* source);
  bool create_program();
  void destroy_buffers();

  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context_ = 0;
  GLuint program_ = 0;
  GLuint vao_ = 0;
  GLuint vertex_buffer_ = 0;
  GLuint index_buffer_ = 0;
  GLsizei index_count_ = 0;
  GLint view_uniform_ = -1;
  GLint projection_uniform_ = -1;
};

}  // namespace voxel
