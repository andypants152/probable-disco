#include "switch_renderer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
extern const unsigned char voxel_vsh_dksh[];
extern const unsigned int voxel_vsh_dksh_size;
extern const unsigned char voxel_fsh_dksh[];
extern const unsigned int voxel_fsh_dksh_size;
}

namespace voxel {

namespace {

constexpr u32 kFramebufferWidth = 1280;
constexpr u32 kFramebufferHeight = 720;
constexpr u32 kFramebufferCount = 2;
constexpr u32 kFrameResourceCount = 2;
constexpr u32 kShaderMemorySize = 64 * 1024;
constexpr u32 kCommandMemorySize = 1024 * 1024;
constexpr u32 kUniformBufferSize = DK_UNIFORM_BUF_ALIGNMENT;
constexpr float kFogDensity = 0.014f;
constexpr float kFogMax = 0.92f;

struct TransformUniform {
  Mat4 view_projection;
  float camera_position[4];
  float camera_forward[4];
  float fog_color[4];
  float fog_params[4];
};

u64 ticks_to_ns(u64 start, u64 end) {
  return armTicksToNs(end - start);
}

u32 align_up(u32 value, u32 alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

u32 grow_capacity(u32 required) {
  u32 capacity = static_cast<u32>(DK_MEMBLOCK_ALIGNMENT);
  while (capacity < required) {
    capacity *= 2;
  }
  return capacity;
}

#if !defined(VOXEL_DEKO_SMOKE)
void decode_color(PackedColor packed, unsigned char* out) {
  out[0] = static_cast<unsigned char>((packed >> 24) & 0xffu);
  out[1] = static_cast<unsigned char>((packed >> 16) & 0xffu);
  out[2] = static_cast<unsigned char>((packed >> 8) & 0xffu);
  out[3] = static_cast<unsigned char>(packed & 0xffu);
}
#endif

DkMemBlock create_memblock(DkDevice device, u32 size, u32 flags) {
  DkMemBlockMaker maker;
  dkMemBlockMakerDefaults(&maker,
                          device,
                          align_up(std::max(size, static_cast<u32>(DK_MEMBLOCK_ALIGNMENT)),
                                   static_cast<u32>(DK_MEMBLOCK_ALIGNMENT)));
  maker.flags = flags;
  return dkMemBlockCreate(&maker);
}

#if defined(VOXEL_SWITCH_PROFILE)
void debug_callback(void*, const char* context, DkResult result, const char* message) {
  std::printf("deko3d debug [%s] result=%d: %s\n",
              context != nullptr ? context : "unknown",
              static_cast<int>(result),
              message != nullptr ? message : "");
}

void profile_log(const char* message) {
  std::printf("[switch-renderer] %s\n", message);
}
#else
void profile_log(const char*) {}
#endif

}  // namespace

bool SwitchRenderer::init() {
  profile_log("init: deko resources");
  if (!create_device() ||
      !create_framebuffers() ||
      !create_shaders() ||
      !create_frame_resources()) {
    shutdown();
    return false;
  }

  initialized_ = true;
  profile_log("init: complete");
  return true;
}

void SwitchRenderer::upload_mesh(const Mesh& mesh) {
  upload_static_mesh(mesh);
  clear_dynamic_buffer(dynamic_mesh_);
  pending_dynamic_upload_ns_ = 0;
}

void SwitchRenderer::upload_static_mesh(const Mesh& mesh) {
  const u64 start = armGetSystemTick();
  upload_buffer(static_mesh_, mesh);
  vertex_count_ = static_mesh_.vertex_count + dynamic_mesh_.vertex_count;
  index_count_ = static_mesh_.index_count + dynamic_mesh_.index_count;
  pending_static_upload_ns_ = ticks_to_ns(start, armGetSystemTick());
}

void SwitchRenderer::upload_dynamic_mesh(const Mesh& mesh) {
  const u64 start = armGetSystemTick();
  upload_dynamic_buffer(mesh);
  vertex_count_ = static_mesh_.vertex_count + dynamic_mesh_.vertex_count;
  index_count_ = static_mesh_.index_count + dynamic_mesh_.index_count;
  pending_dynamic_upload_ns_ = ticks_to_ns(start, armGetSystemTick());
}

void SwitchRenderer::render_frame(const Camera& camera) {
  if (!initialized_) {
    return;
  }

  const u64 static_upload_ns = pending_static_upload_ns_;
  const u64 dynamic_upload_ns = pending_dynamic_upload_ns_;
  pending_static_upload_ns_ = 0;
  pending_dynamic_upload_ns_ = 0;
  frame_stats_ = {};
  frame_stats_.static_upload_ns = static_upload_ns;
  frame_stats_.dynamic_upload_ns = dynamic_upload_ns;
  frame_stats_.vertices = vertex_count_;
  frame_stats_.triangles = index_count_ / 3;

  const u64 frame_start = armGetSystemTick();
  const u32 frame_index = current_frame_;
  FrameResource& frame = frame_resources_[frame_index];

  const u64 wait_start = armGetSystemTick();
  if (frame.fence_pending) {
    dkFenceWait(&frame.fence, -1);
    frame.fence_pending = false;
  }
  frame_stats_.wait_ns = ticks_to_ns(wait_start, armGetSystemTick());

  copy_dynamic_upload(frame_index);

  const u64 acquire_start = armGetSystemTick();
  const int slot = dkQueueAcquireImage(queue_, swapchain_);
  frame_stats_.acquire_ns = ticks_to_ns(acquire_start, armGetSystemTick());

  TransformUniform transform = {};
  transform.view_projection = camera.projection_matrix() * camera.view_matrix();
  transform.camera_position[0] = camera.position.x;
  transform.camera_position[1] = camera.position.y;
  transform.camera_position[2] = camera.position.z;
  transform.camera_position[3] = 1.0f;
  const Vec3 camera_forward = camera.forward();
  transform.camera_forward[0] = camera_forward.x;
  transform.camera_forward[1] = camera_forward.y;
  transform.camera_forward[2] = camera_forward.z;
  transform.camera_forward[3] = 0.0f;
  transform.fog_color[0] = 0.114f;
  transform.fog_color[1] = 0.169f;
  transform.fog_color[2] = 0.153f;
  transform.fog_color[3] = 1.0f;
  transform.fog_params[0] = kFogDensity;
  transform.fog_params[1] = kFogMax;
  transform.fog_params[2] = kFogMax;
  transform.fog_params[3] = 0.0f;

  if (frame.command_buffer_used) {
    dkCmdBufClear(frame.command_buffer);
  }
  dkCmdBufAddMemory(frame.command_buffer, frame.command_mem, 0, kCommandMemorySize);

  const u64 record_start = armGetSystemTick();
  dkCmdBufPushConstants(frame.command_buffer,
                        frame.uniform_addr,
                        kUniformBufferSize,
                        0,
                        sizeof(transform),
                        &transform);

  DkImageView color_view;
  DkImageView depth_view;
  dkImageViewDefaults(&color_view, &framebuffers_[slot]);
  dkImageViewDefaults(&depth_view, &depth_buffer_);
  dkCmdBufBindRenderTarget(frame.command_buffer, &color_view, &depth_view);

  DkViewport viewport = {0.0f, 0.0f, static_cast<float>(kFramebufferWidth), static_cast<float>(kFramebufferHeight), 0.0f, 1.0f};
  DkScissor scissor = {0, 0, kFramebufferWidth, kFramebufferHeight};
  DkShader const* shaders[] = {&vertex_shader_, &fragment_shader_};

  DkRasterizerState rasterizer_state;
  DkColorState color_state;
  DkColorWriteState color_write_state;
  DkDepthStencilState depth_stencil_state;
  dkRasterizerStateDefaults(&rasterizer_state);
  dkColorStateDefaults(&color_state);
  dkColorWriteStateDefaults(&color_write_state);
  dkDepthStencilStateDefaults(&depth_stencil_state);
  rasterizer_state.cullMode = DkFace_None;
  depth_stencil_state.depthCompareOp = DkCompareOp_Less;

  const DkVtxAttribState attribs[] = {
      {0, 0, static_cast<u32>(offsetof(GpuVertex, position)), DkVtxAttribSize_3x32, DkVtxAttribType_Float, 0},
      {0, 0, static_cast<u32>(offsetof(GpuVertex, normal)), DkVtxAttribSize_3x32, DkVtxAttribType_Float, 0},
      {0, 0, static_cast<u32>(offsetof(GpuVertex, color)), DkVtxAttribSize_4x8, DkVtxAttribType_Unorm, 0},
      {0, 0, static_cast<u32>(offsetof(GpuVertex, micro_position)), DkVtxAttribSize_3x32, DkVtxAttribType_Float, 0},
  };
  const DkVtxBufferState vertex_buffer_state = {sizeof(GpuVertex), 0};

  dkCmdBufSetViewports(frame.command_buffer, 0, &viewport, 1);
  dkCmdBufSetScissors(frame.command_buffer, 0, &scissor, 1);

  const u64 clear_start = armGetSystemTick();
  dkCmdBufClearColorFloat(frame.command_buffer, 0, DkColorMask_RGBA, 0.114f, 0.169f, 0.153f, 1.0f);
  dkCmdBufClearDepthStencil(frame.command_buffer, true, 1.0f, 0xff, 0);
  frame_stats_.clear_ns = ticks_to_ns(clear_start, armGetSystemTick());

  dkCmdBufBindShaders(frame.command_buffer, DkStageFlag_GraphicsMask, shaders, 2);
  dkCmdBufBindUniformBuffer(frame.command_buffer, DkStage_Vertex, 0, frame.uniform_addr, kUniformBufferSize);
  dkCmdBufBindUniformBuffer(frame.command_buffer, DkStage_Fragment, 0, frame.uniform_addr, kUniformBufferSize);
  dkCmdBufBindRasterizerState(frame.command_buffer, &rasterizer_state);
  dkCmdBufBindColorState(frame.command_buffer, &color_state);
  dkCmdBufBindColorWriteState(frame.command_buffer, &color_write_state);
  dkCmdBufBindDepthStencilState(frame.command_buffer, &depth_stencil_state);
  dkCmdBufBindVtxAttribState(frame.command_buffer, attribs, 4);
  dkCmdBufBindVtxBufferState(frame.command_buffer, &vertex_buffer_state, 1);

  const u64 draw_start = armGetSystemTick();
  draw_buffer(frame.command_buffer, static_mesh_);
  draw_dynamic_buffer(frame.command_buffer, dynamic_mesh_, frame_index);
  frame_stats_.draw_ns = ticks_to_ns(draw_start, armGetSystemTick());
  dkCmdBufSignalFence(frame.command_buffer, &frame.fence, true);
  frame_stats_.command_record_ns = ticks_to_ns(record_start, armGetSystemTick());

  DkCmdList command_list = dkCmdBufFinishList(frame.command_buffer);
  frame.command_buffer_used = true;
  frame.fence_pending = true;
  dkQueueSubmitCommands(queue_, command_list);

  const u64 present_start = armGetSystemTick();
  dkQueuePresentImage(queue_, swapchain_, slot);
  frame_stats_.present_ns += ticks_to_ns(present_start, armGetSystemTick());
  frame_stats_.total_ns = ticks_to_ns(frame_start, armGetSystemTick());
  current_frame_ = (current_frame_ + 1) % kFrameResourceCount;
}

void SwitchRenderer::shutdown() {
  if (queue_ != nullptr) {
    dkQueueWaitIdle(queue_);
  }

  clear_buffer(static_mesh_);
  clear_dynamic_buffer(dynamic_mesh_);

  destroy_frame_resources();
  if (shader_mem_ != nullptr) {
    dkMemBlockDestroy(shader_mem_);
    shader_mem_ = nullptr;
  }

  destroy_framebuffers();

  if (queue_ != nullptr) {
    dkQueueDestroy(queue_);
    queue_ = nullptr;
  }
  if (device_ != nullptr) {
    dkDeviceDestroy(device_);
    device_ = nullptr;
  }
  vertex_count_ = 0;
  index_count_ = 0;
  current_frame_ = 0;
  initialized_ = false;
}

bool SwitchRenderer::create_device() {
  profile_log("create_device");
  DkDeviceMaker device_maker;
  dkDeviceMakerDefaults(&device_maker);
#if defined(VOXEL_SWITCH_PROFILE)
  device_maker.cbDebug = debug_callback;
#endif
  device_ = dkDeviceCreate(&device_maker);
  if (device_ == nullptr) {
    std::printf("Failed to create deko3d device.\n");
    return false;
  }

  DkQueueMaker queue_maker;
  dkQueueMakerDefaults(&queue_maker, device_);
  queue_maker.flags = DkQueueFlags_Graphics;
  queue_ = dkQueueCreate(&queue_maker);
  if (queue_ == nullptr) {
    std::printf("Failed to create deko3d graphics queue.\n");
    return false;
  }

  return true;
}

bool SwitchRenderer::create_framebuffers() {
  profile_log("create_framebuffers");
  DkImageLayoutMaker color_layout_maker;
  dkImageLayoutMakerDefaults(&color_layout_maker, device_);
  color_layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
  color_layout_maker.format = DkImageFormat_RGBA8_Unorm;
  color_layout_maker.dimensions[0] = kFramebufferWidth;
  color_layout_maker.dimensions[1] = kFramebufferHeight;

  DkImageLayout color_layout;
  dkImageLayoutInitialize(&color_layout, &color_layout_maker);
  const u32 framebuffer_size = align_up(dkImageLayoutGetSize(&color_layout), dkImageLayoutGetAlignment(&color_layout));
  framebuffer_mem_ = create_memblock(device_,
                                     framebuffer_size * kFramebufferCount,
                                     DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
  if (framebuffer_mem_ == nullptr) {
    std::printf("Failed to allocate deko3d framebuffer memory.\n");
    return false;
  }

  DkImage const* swapchain_images[kFramebufferCount] = {};
  for (u32 i = 0; i < kFramebufferCount; ++i) {
    dkImageInitialize(&framebuffers_[i], &color_layout, framebuffer_mem_, i * framebuffer_size);
    swapchain_images[i] = &framebuffers_[i];
  }

  DkSwapchainMaker swapchain_maker;
  dkSwapchainMakerDefaults(&swapchain_maker, device_, nwindowGetDefault(), swapchain_images, kFramebufferCount);
  swapchain_ = dkSwapchainCreate(&swapchain_maker);
  if (swapchain_ == nullptr) {
    std::printf("Failed to create deko3d swapchain.\n");
    return false;
  }

  DkImageLayoutMaker depth_layout_maker;
  dkImageLayoutMakerDefaults(&depth_layout_maker, device_);
  depth_layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_HwCompression;
  depth_layout_maker.format = DkImageFormat_Z24S8;
  depth_layout_maker.dimensions[0] = kFramebufferWidth;
  depth_layout_maker.dimensions[1] = kFramebufferHeight;

  DkImageLayout depth_layout;
  dkImageLayoutInitialize(&depth_layout, &depth_layout_maker);
  depth_mem_ = create_memblock(device_,
                               dkImageLayoutGetSize(&depth_layout),
                               DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
  if (depth_mem_ == nullptr) {
    std::printf("Failed to allocate deko3d depth memory.\n");
    return false;
  }
  dkImageInitialize(&depth_buffer_, &depth_layout, depth_mem_, 0);
  return true;
}

bool SwitchRenderer::create_shaders() {
  profile_log("create_shaders");
  shader_mem_ = create_memblock(device_,
                                kShaderMemorySize,
                                DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
  if (shader_mem_ == nullptr) {
    std::printf("Failed to allocate deko3d shader memory.\n");
    return false;
  }

  shader_mem_offset_ = 0;
  return load_shader(vertex_shader_, voxel_vsh_dksh, voxel_vsh_dksh_size, "voxel_vsh.dksh") &&
         load_shader(fragment_shader_, voxel_fsh_dksh, voxel_fsh_dksh_size, "voxel_fsh.dksh");
}

bool SwitchRenderer::create_frame_resources() {
  profile_log("create_frame_resources");
  for (u32 i = 0; i < kFrameResourceCount; ++i) {
    FrameResource& frame = frame_resources_[i];
    frame.command_mem = create_memblock(device_,
                                        kCommandMemorySize,
                                        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (frame.command_mem == nullptr) {
      std::printf("Failed to allocate deko3d command memory.\n");
      return false;
    }

    DkCmdBufMaker command_maker;
    dkCmdBufMakerDefaults(&command_maker, device_);
    frame.command_buffer = dkCmdBufCreate(&command_maker);
    if (frame.command_buffer == nullptr) {
      std::printf("Failed to create deko3d command buffer.\n");
      return false;
    }

    frame.uniform_mem = create_memblock(device_,
                                        kUniformBufferSize,
                                        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (frame.uniform_mem == nullptr) {
      std::printf("Failed to allocate deko3d uniform memory.\n");
      return false;
    }
    frame.uniform_addr = dkMemBlockGetGpuAddr(frame.uniform_mem);
    if (frame.uniform_addr == DK_GPU_ADDR_INVALID) {
      return false;
    }
  }
  return true;
}

bool SwitchRenderer::load_shader(DkShader& shader, const unsigned char* data, u32 size, const char* name) {
  if (data == nullptr || size == 0) {
    std::printf("Shader %s is empty.\n", name);
    return false;
  }

  const u32 code_offset = shader_mem_offset_;
  shader_mem_offset_ += align_up(size, DK_SHADER_CODE_ALIGNMENT);
  if (shader_mem_offset_ > kShaderMemorySize) {
    std::printf("Shader memory exhausted while loading %s.\n", name);
    return false;
  }

  auto* destination = static_cast<unsigned char*>(dkMemBlockGetCpuAddr(shader_mem_)) + code_offset;
  std::memcpy(destination, data, size);

  DkShaderMaker shader_maker;
  dkShaderMakerDefaults(&shader_maker, shader_mem_, code_offset);
  dkShaderInitialize(&shader, &shader_maker);
  return true;
}

void SwitchRenderer::upload_buffer(MeshBuffer& buffer, const Mesh& mesh) {
#if defined(VOXEL_DEKO_SMOKE)
  clear_buffer(buffer);
  return;
#else
  const u32 vertex_bytes = static_cast<u32>(mesh.vertices.size() * sizeof(GpuVertex));
  const u32 index_bytes = static_cast<u32>(mesh.indices.size() * sizeof(Index));
  if (queue_ != nullptr) {
    // Static terrain changes are rare and may replace data used by in-flight frames.
    dkQueueWaitIdle(queue_);
  }

  if (mesh.vertices.empty() || mesh.indices.empty()) {
    buffer.vertex_count = 0;
    buffer.index_count = 0;
    buffer.vertex_bytes = 0;
    buffer.index_bytes = 0;
    return;
  }

  std::vector<GpuVertex> gpu_vertices;
  gpu_vertices.reserve(mesh.vertices.size());
  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    const Vec3 position = mesh.vertices[i];
    const Vec3 normal = i < mesh.normals.size() ? mesh.normals[i] : Vec3{0.0f, 1.0f, 0.0f};
    const PackedColor color = i < mesh.colors.size() ? mesh.colors[i] : pack_rgba(255, 0, 255);

    GpuVertex vertex = {};
    vertex.position[0] = position.x;
    vertex.position[1] = position.y;
    vertex.position[2] = position.z;
    vertex.normal[0] = normal.x;
    vertex.normal[1] = normal.y;
    vertex.normal[2] = normal.z;
    decode_color(color, vertex.color);
    const Vec3 micro_position = i < mesh.micro_positions.size() ? mesh.micro_positions[i] : Vec3{0.0f, 0.0f, 0.0f};
    vertex.micro_position[0] = micro_position.x;
    vertex.micro_position[1] = micro_position.y;
    vertex.micro_position[2] = micro_position.z;
    gpu_vertices.push_back(vertex);
  }

  if (!ensure_buffer_capacity(buffer, vertex_bytes, index_bytes)) {
    return;
  }

  buffer.vertex_count = static_cast<u32>(gpu_vertices.size());
  buffer.index_count = static_cast<u32>(mesh.indices.size());
  buffer.vertex_bytes = vertex_bytes;
  buffer.index_bytes = index_bytes;
  std::memcpy(dkMemBlockGetCpuAddr(buffer.vertex_mem), gpu_vertices.data(), buffer.vertex_bytes);
  std::memcpy(dkMemBlockGetCpuAddr(buffer.index_mem), mesh.indices.data(), buffer.index_bytes);
#endif
}

void SwitchRenderer::upload_dynamic_buffer(const Mesh& mesh) {
#if defined(VOXEL_DEKO_SMOKE)
  clear_dynamic_buffer(dynamic_mesh_);
  return;
#else
  const u32 vertex_bytes = static_cast<u32>(mesh.vertices.size() * sizeof(GpuVertex));
  const u32 index_bytes = static_cast<u32>(mesh.indices.size() * sizeof(Index));

  if (mesh.vertices.empty() || mesh.indices.empty()) {
    dynamic_mesh_.pending_vertices.clear();
    dynamic_mesh_.pending_indices.clear();
    dynamic_mesh_.vertex_count = 0;
    dynamic_mesh_.index_count = 0;
    dynamic_mesh_.vertex_bytes = 0;
    dynamic_mesh_.index_bytes = 0;
    for (u32 i = 0; i < kFrameResourceCount; ++i) {
      dynamic_mesh_.slot_dirty[i] = false;
    }
    return;
  }

  if (!ensure_dynamic_capacity(vertex_bytes, index_bytes)) {
    dynamic_mesh_.pending_vertices.clear();
    dynamic_mesh_.pending_indices.clear();
    dynamic_mesh_.vertex_count = 0;
    dynamic_mesh_.index_count = 0;
    dynamic_mesh_.vertex_bytes = 0;
    dynamic_mesh_.index_bytes = 0;
    return;
  }

  dynamic_mesh_.pending_vertices.clear();
  dynamic_mesh_.pending_vertices.reserve(mesh.vertices.size());
  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    const Vec3 position = mesh.vertices[i];
    const Vec3 normal = i < mesh.normals.size() ? mesh.normals[i] : Vec3{0.0f, 1.0f, 0.0f};
    const PackedColor color = i < mesh.colors.size() ? mesh.colors[i] : pack_rgba(255, 0, 255);

    GpuVertex vertex = {};
    vertex.position[0] = position.x;
    vertex.position[1] = position.y;
    vertex.position[2] = position.z;
    vertex.normal[0] = normal.x;
    vertex.normal[1] = normal.y;
    vertex.normal[2] = normal.z;
    decode_color(color, vertex.color);
    const Vec3 micro_position = i < mesh.micro_positions.size() ? mesh.micro_positions[i] : Vec3{0.0f, 0.0f, 0.0f};
    vertex.micro_position[0] = micro_position.x;
    vertex.micro_position[1] = micro_position.y;
    vertex.micro_position[2] = micro_position.z;
    dynamic_mesh_.pending_vertices.push_back(vertex);
  }
  dynamic_mesh_.pending_indices.assign(mesh.indices.begin(), mesh.indices.end());
  dynamic_mesh_.vertex_count = static_cast<u32>(dynamic_mesh_.pending_vertices.size());
  dynamic_mesh_.index_count = static_cast<u32>(dynamic_mesh_.pending_indices.size());
  dynamic_mesh_.vertex_bytes = vertex_bytes;
  dynamic_mesh_.index_bytes = index_bytes;

  for (u32 i = 0; i < kFrameResourceCount; ++i) {
    dynamic_mesh_.slot_dirty[i] = true;
  }
#endif
}

bool SwitchRenderer::ensure_buffer_capacity(MeshBuffer& buffer, u32 vertex_bytes, u32 index_bytes) {
  if (buffer.vertex_mem != nullptr &&
      buffer.index_mem != nullptr &&
      buffer.vertex_capacity_bytes >= vertex_bytes &&
      buffer.index_capacity_bytes >= index_bytes) {
    return true;
  }

  clear_buffer(buffer);
  buffer.vertex_capacity_bytes = grow_capacity(vertex_bytes);
  buffer.index_capacity_bytes = grow_capacity(index_bytes);
  buffer.vertex_mem = create_memblock(device_,
                                      buffer.vertex_capacity_bytes,
                                      DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
  buffer.index_mem = create_memblock(device_,
                                     buffer.index_capacity_bytes,
                                     DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
  if (buffer.vertex_mem == nullptr || buffer.index_mem == nullptr) {
    clear_buffer(buffer);
    std::printf("Failed to allocate deko3d mesh buffers.\n");
    return false;
  }

  buffer.vertex_addr = dkMemBlockGetGpuAddr(buffer.vertex_mem);
  buffer.index_addr = dkMemBlockGetGpuAddr(buffer.index_mem);
  return buffer.vertex_addr != DK_GPU_ADDR_INVALID && buffer.index_addr != DK_GPU_ADDR_INVALID;
}

bool SwitchRenderer::ensure_dynamic_capacity(u32 vertex_bytes, u32 index_bytes) {
  if (dynamic_mesh_.vertex_mem[0] != nullptr &&
      dynamic_mesh_.index_mem[0] != nullptr &&
      dynamic_mesh_.vertex_capacity_bytes >= vertex_bytes &&
      dynamic_mesh_.index_capacity_bytes >= index_bytes) {
    return true;
  }

  if (queue_ != nullptr) {
    dkQueueWaitIdle(queue_);
  }
  clear_dynamic_buffer(dynamic_mesh_);
  dynamic_mesh_.vertex_capacity_bytes = grow_capacity(vertex_bytes);
  dynamic_mesh_.index_capacity_bytes = grow_capacity(index_bytes);

  for (u32 i = 0; i < kFrameResourceCount; ++i) {
    dynamic_mesh_.vertex_mem[i] = create_memblock(device_,
                                                 dynamic_mesh_.vertex_capacity_bytes,
                                                 DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    dynamic_mesh_.index_mem[i] = create_memblock(device_,
                                                dynamic_mesh_.index_capacity_bytes,
                                                DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (dynamic_mesh_.vertex_mem[i] == nullptr || dynamic_mesh_.index_mem[i] == nullptr) {
      clear_dynamic_buffer(dynamic_mesh_);
      std::printf("Failed to allocate deko3d dynamic mesh buffers.\n");
      return false;
    }
    dynamic_mesh_.vertex_addr[i] = dkMemBlockGetGpuAddr(dynamic_mesh_.vertex_mem[i]);
    dynamic_mesh_.index_addr[i] = dkMemBlockGetGpuAddr(dynamic_mesh_.index_mem[i]);
    if (dynamic_mesh_.vertex_addr[i] == DK_GPU_ADDR_INVALID ||
        dynamic_mesh_.index_addr[i] == DK_GPU_ADDR_INVALID) {
      clear_dynamic_buffer(dynamic_mesh_);
      return false;
    }
  }

  return true;
}

void SwitchRenderer::copy_dynamic_upload(u32 frame_index) {
  if (!dynamic_mesh_.slot_dirty[frame_index] ||
      dynamic_mesh_.vertex_count == 0 ||
      dynamic_mesh_.index_count == 0) {
    return;
  }

  const u64 start = armGetSystemTick();
  std::memcpy(dkMemBlockGetCpuAddr(dynamic_mesh_.vertex_mem[frame_index]),
              dynamic_mesh_.pending_vertices.data(),
              dynamic_mesh_.vertex_bytes);
  std::memcpy(dkMemBlockGetCpuAddr(dynamic_mesh_.index_mem[frame_index]),
              dynamic_mesh_.pending_indices.data(),
              dynamic_mesh_.index_bytes);
  dynamic_mesh_.slot_dirty[frame_index] = false;
  frame_stats_.dynamic_upload_ns += ticks_to_ns(start, armGetSystemTick());
}

void SwitchRenderer::clear_buffer(MeshBuffer& buffer) {
  if (buffer.vertex_mem != nullptr) {
    dkMemBlockDestroy(buffer.vertex_mem);
  }
  if (buffer.index_mem != nullptr) {
    dkMemBlockDestroy(buffer.index_mem);
  }
  buffer = {};
  buffer.vertex_addr = DK_GPU_ADDR_INVALID;
  buffer.index_addr = DK_GPU_ADDR_INVALID;
}

void SwitchRenderer::clear_dynamic_buffer(DynamicMeshBuffer& buffer) {
  for (u32 i = 0; i < kFrameResourceCount; ++i) {
    if (buffer.vertex_mem[i] != nullptr) {
      dkMemBlockDestroy(buffer.vertex_mem[i]);
    }
    if (buffer.index_mem[i] != nullptr) {
      dkMemBlockDestroy(buffer.index_mem[i]);
    }
  }
  buffer = {};
  for (u32 i = 0; i < kFrameResourceCount; ++i) {
    buffer.vertex_addr[i] = DK_GPU_ADDR_INVALID;
    buffer.index_addr[i] = DK_GPU_ADDR_INVALID;
  }
}

void SwitchRenderer::destroy_frame_resources() {
  for (u32 i = 0; i < kFrameResourceCount; ++i) {
    FrameResource& frame = frame_resources_[i];
    if (frame.command_buffer != nullptr) {
      dkCmdBufDestroy(frame.command_buffer);
    }
    if (frame.command_mem != nullptr) {
      dkMemBlockDestroy(frame.command_mem);
    }
    if (frame.uniform_mem != nullptr) {
      dkMemBlockDestroy(frame.uniform_mem);
    }
    frame = {};
    frame.uniform_addr = DK_GPU_ADDR_INVALID;
  }
}

void SwitchRenderer::destroy_framebuffers() {
  if (swapchain_ != nullptr) {
    dkSwapchainDestroy(swapchain_);
    swapchain_ = nullptr;
  }
  if (depth_mem_ != nullptr) {
    dkMemBlockDestroy(depth_mem_);
    depth_mem_ = nullptr;
  }
  if (framebuffer_mem_ != nullptr) {
    dkMemBlockDestroy(framebuffer_mem_);
    framebuffer_mem_ = nullptr;
  }
}

void SwitchRenderer::draw_buffer(DkCmdBuf command_buffer, const MeshBuffer& buffer) {
  if (buffer.vertex_addr == DK_GPU_ADDR_INVALID ||
      buffer.index_addr == DK_GPU_ADDR_INVALID ||
      buffer.index_count == 0) {
    return;
  }

  dkCmdBufBindVtxBuffer(command_buffer, 0, buffer.vertex_addr, buffer.vertex_bytes);
  dkCmdBufBindIdxBuffer(command_buffer, DkIdxFormat_Uint32, buffer.index_addr);
  dkCmdBufDrawIndexed(command_buffer, DkPrimitive_Triangles, buffer.index_count, 1, 0, 0, 0);
}

void SwitchRenderer::draw_dynamic_buffer(DkCmdBuf command_buffer,
                                         const DynamicMeshBuffer& buffer,
                                         u32 frame_index) {
  if (buffer.vertex_addr[frame_index] == DK_GPU_ADDR_INVALID ||
      buffer.index_addr[frame_index] == DK_GPU_ADDR_INVALID ||
      buffer.index_count == 0) {
    return;
  }

  dkCmdBufBindVtxBuffer(command_buffer, 0, buffer.vertex_addr[frame_index], buffer.vertex_bytes);
  dkCmdBufBindIdxBuffer(command_buffer, DkIdxFormat_Uint32, buffer.index_addr[frame_index]);
  dkCmdBufDrawIndexed(command_buffer, DkPrimitive_Triangles, buffer.index_count, 1, 0, 0, 0);
}

}  // namespace voxel
