#include "switch_renderer.h"

#include <cstdio>

namespace voxel {

bool SwitchRenderer::create_context() {
  if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
      std::printf("SDL video init failed: %s\n", SDL_GetError());
      return false;
    }
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  window_ = SDL_CreateWindow("Probable Disco",
                             SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED,
                             1280,
                             720,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
  if (window_ == nullptr) {
    std::printf("SDL window creation failed: %s\n", SDL_GetError());
    return false;
  }

  context_ = SDL_GL_CreateContext(window_);
  if (context_ == nullptr) {
    std::printf("SDL GL context creation failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(window_);
    window_ = nullptr;
    return false;
  }

  SDL_GL_MakeCurrent(window_, context_);
  SDL_GL_SetSwapInterval(1);
  return true;
}

void SwitchRenderer::make_context_current() {
  if (window_ != nullptr && context_ != nullptr) {
    SDL_GL_MakeCurrent(window_, context_);
  }
}

void SwitchRenderer::framebuffer_size(int& width, int& height) {
  if (window_ == nullptr) {
    width = 1280;
    height = 720;
    return;
  }
  SDL_GL_GetDrawableSize(window_, &width, &height);
}

void SwitchRenderer::present() {
  if (window_ != nullptr) {
    SDL_GL_SwapWindow(window_);
  }
}

void SwitchRenderer::destroy_context() {
  if (context_ != nullptr) {
    SDL_GL_DeleteContext(context_);
    context_ = nullptr;
  }
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
}

}  // namespace voxel
