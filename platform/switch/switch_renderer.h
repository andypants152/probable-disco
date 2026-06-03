#pragma once

#include <cstddef>
#include <cstdint>

#include <SDL2/SDL.h>

#include "render/gles_renderer.h"

namespace voxel {

class SwitchRenderer final : public GlesRenderer {
 public:
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
  bool create_context() override;
  void make_context_current() override;
  void framebuffer_size(int& width, int& height) override;
  void present() override;
  void destroy_context() override;

 private:
  SDL_Window* window_ = nullptr;
  SDL_GLContext context_ = nullptr;
  FrameStats frame_stats_;
};

}  // namespace voxel
