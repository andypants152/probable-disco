#pragma once

#include <cstddef>
#include <vector>

#include <deko3d.h>
#include <switch.h>

#include "math/mat4.h"
#include "math/vec3.h"
#include "platform.h"
#include "render/render_types.h"

namespace voxel {

class SwitchRenderer final : public Renderer {
 public:
  bool init() override;
  void upload_mesh(const Mesh& mesh) override;
  bool supports_separate_meshes() const override { return true; }
  void upload_static_mesh(const Mesh& mesh) override;
  void upload_dynamic_mesh(const Mesh& mesh) override;
  void render_frame(const Camera& camera) override;
  void shutdown() override;

  std::size_t vertex_count() const { return vertex_count_; }
  std::size_t index_count() const { return index_count_; }

  struct FrameStats {
    u64 total_ns = 0;
    u64 wait_ns = 0;
    u64 acquire_ns = 0;
    u64 command_record_ns = 0;
    u64 clear_ns = 0;
    u64 present_ns = 0;
    u64 draw_ns = 0;
    u64 static_upload_ns = 0;
    u64 dynamic_upload_ns = 0;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
  };

  const FrameStats& frame_stats() const { return frame_stats_; }

 private:
  struct GpuVertex {
    float position[3];
    float normal[3];
    unsigned char color[4];
    float micro_position[3];
  };

  struct MeshBuffer {
    DkMemBlock vertex_mem = nullptr;
    DkMemBlock index_mem = nullptr;
    DkGpuAddr vertex_addr = DK_GPU_ADDR_INVALID;
    DkGpuAddr index_addr = DK_GPU_ADDR_INVALID;
    u32 vertex_count = 0;
    u32 index_count = 0;
    u32 vertex_bytes = 0;
    u32 index_bytes = 0;
    u32 vertex_capacity_bytes = 0;
    u32 index_capacity_bytes = 0;
  };

  struct DynamicMeshBuffer {
    DkMemBlock vertex_mem[2] = {};
    DkMemBlock index_mem[2] = {};
    DkGpuAddr vertex_addr[2] = {DK_GPU_ADDR_INVALID, DK_GPU_ADDR_INVALID};
    DkGpuAddr index_addr[2] = {DK_GPU_ADDR_INVALID, DK_GPU_ADDR_INVALID};
    bool slot_dirty[2] = {};
    u32 vertex_count = 0;
    u32 index_count = 0;
    u32 vertex_bytes = 0;
    u32 index_bytes = 0;
    u32 vertex_capacity_bytes = 0;
    u32 index_capacity_bytes = 0;
    std::vector<GpuVertex> pending_vertices;
    std::vector<Index> pending_indices;
  };

  struct FrameResource {
    DkMemBlock command_mem = nullptr;
    DkCmdBuf command_buffer = nullptr;
    DkMemBlock uniform_mem = nullptr;
    DkGpuAddr uniform_addr = DK_GPU_ADDR_INVALID;
    DkFence fence = {};
    bool command_buffer_used = false;
    bool fence_pending = false;
  };

  bool create_device();
  bool create_framebuffers();
  bool create_shaders();
  bool create_frame_resources();
  bool load_shader(DkShader& shader, const unsigned char* data, u32 size, const char* name);
  void upload_buffer(MeshBuffer& buffer, const Mesh& mesh);
  void upload_dynamic_buffer(const Mesh& mesh);
  bool ensure_buffer_capacity(MeshBuffer& buffer, u32 vertex_bytes, u32 index_bytes);
  bool ensure_dynamic_capacity(u32 vertex_bytes, u32 index_bytes);
  void copy_dynamic_upload(u32 frame_index);
  void clear_buffer(MeshBuffer& buffer);
  void clear_dynamic_buffer(DynamicMeshBuffer& buffer);
  void destroy_frame_resources();
  void destroy_framebuffers();
  void draw_buffer(DkCmdBuf command_buffer, const MeshBuffer& buffer);
  void draw_dynamic_buffer(DkCmdBuf command_buffer, const DynamicMeshBuffer& buffer, u32 frame_index);

  DkDevice device_ = nullptr;
  DkQueue queue_ = nullptr;
  DkSwapchain swapchain_ = nullptr;
  DkMemBlock framebuffer_mem_ = nullptr;
  DkMemBlock depth_mem_ = nullptr;
  DkImage framebuffers_[2] = {};
  DkImage depth_buffer_ = {};

  DkMemBlock shader_mem_ = nullptr;
  u32 shader_mem_offset_ = 0;
  DkShader vertex_shader_ = {};
  DkShader fragment_shader_ = {};

  FrameResource frame_resources_[2] = {};
  MeshBuffer static_mesh_;
  DynamicMeshBuffer dynamic_mesh_;
  u32 current_frame_ = 0;
  bool initialized_ = false;
  std::size_t vertex_count_ = 0;
  std::size_t index_count_ = 0;
  u64 pending_static_upload_ns_ = 0;
  u64 pending_dynamic_upload_ns_ = 0;
  FrameStats frame_stats_;
};

}  // namespace voxel
