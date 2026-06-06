#pragma once

#include <SDL2/SDL.h>

#include "render/gles_renderer.h"

namespace voxel {

class SwitchRenderer final : public GlesRenderer {
 protected:
  bool create_context() override;
  void make_context_current() override;
  void framebuffer_size(int& width, int& height) override;
  void present() override;
  void destroy_context() override;

 private:
  SDL_Window* window_ = nullptr;
  SDL_GLContext context_ = nullptr;
};

}  // namespace voxel
