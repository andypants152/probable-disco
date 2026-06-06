#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <GLES3/gl3.h>

#include "core/platform.h"
#include "render/bitmap_font.h"

namespace voxel {

class GlesRenderer : public Renderer {
 public:
  bool init() override;
  void begin_frame_stats() override;
  void upload_mesh(const Mesh& mesh) override;
  bool supports_separate_meshes() const override { return true; }
  void upload_static_mesh(const Mesh& mesh) override;
  void upload_dynamic_mesh(const Mesh& mesh) override;
  void render_frame(const RenderFrame& frame) override;
  void shutdown() override;

  struct FrameStats {
    std::uint64_t total_ns = 0;
    std::uint64_t wait_ns = 0;
    std::uint64_t acquire_ns = 0;
    std::uint64_t command_record_ns = 0;
    std::uint64_t clear_ns = 0;
    std::uint64_t draw_ns = 0;
    std::uint64_t present_ns = 0;
    std::uint64_t static_upload_ns = 0;
    std::uint64_t dynamic_upload_ns = 0;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
  };

  const FrameStats& frame_stats() const { return frame_stats_; }

 protected:
  virtual bool create_context() = 0;
  virtual void make_context_current() = 0;
  virtual void framebuffer_size(int& width, int& height) = 0;
  virtual void present() {}
  virtual void destroy_context() = 0;

 private:
  struct GpuVertex {
    float position[3];
    float normal[3];
    unsigned char color[4];
    float micro_position[3];
  };

  struct OverlayVertex {
    float position[2];
    float tex_coord[2];
    unsigned char color[4];
  };

  struct MeshBuffer {
    GLuint vao = 0;
    GLuint vertex_buffer = 0;
    GLuint index_buffer = 0;
    GLsizeiptr vertex_capacity_bytes = 0;
    GLsizeiptr index_capacity_bytes = 0;
    GLsizei index_count = 0;
    std::size_t vertex_count = 0;
  };

  GLuint compile_shader(GLenum type, const char* source);
  bool create_program();
  bool create_overlay_program();
  void create_font_texture();
  void upload_buffer(MeshBuffer& buffer, const Mesh& mesh, GLenum usage);
  void ensure_mesh_buffer(MeshBuffer& buffer);
  void upload_overlay_batch(const QuadBatch& batch);
  void ensure_overlay_buffer();
  void draw_buffer(const MeshBuffer& buffer);
  void draw_overlay_batch(int framebuffer_width, int framebuffer_height);
  void destroy_buffer(MeshBuffer& buffer);
  void destroy_buffers();
  void destroy_gl_resources();

  GLuint program_ = 0;
  GLuint overlay_program_ = 0;
  GLuint font_texture_ = 0;
  GLuint overlay_vao_ = 0;
  GLuint overlay_vertex_buffer_ = 0;
  GLuint overlay_index_buffer_ = 0;
  GLsizeiptr overlay_vertex_capacity_bytes_ = 0;
  GLsizeiptr overlay_index_capacity_bytes_ = 0;
  GLsizei overlay_index_count_ = 0;
  MeshBuffer static_mesh_;
  MeshBuffer dynamic_mesh_;
  GLint view_uniform_ = -1;
  GLint projection_uniform_ = -1;
  GLint light_count_uniform_ = -1;
  GLint light_position_radius_uniform_ = -1;
  GLint light_color_intensity_uniform_ = -1;
  GLint overlay_viewport_uniform_ = -1;
  GLint overlay_texture_uniform_ = -1;
  float light_position_radius_[kMaxRendererGameplayLights * 4] = {};
  float light_color_intensity_[kMaxRendererGameplayLights * 4] = {};
  int light_count_ = 0;
  BitmapFontAtlas font_atlas_;
  QuadBatch overlay_batch_;
  std::vector<GpuVertex> mesh_vertices_;
  std::vector<OverlayVertex> overlay_vertices_;
  FrameStats frame_stats_;
};

}  // namespace voxel
