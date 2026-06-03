#include "web_renderer.h"

#include <cstdio>

namespace voxel {

bool WebRenderer::create_context() {
  EmscriptenWebGLContextAttributes attributes;
  emscripten_webgl_init_context_attributes(&attributes);
  attributes.alpha = EM_FALSE;
  attributes.depth = EM_TRUE;
  attributes.stencil = EM_FALSE;
  attributes.antialias = EM_TRUE;
  attributes.majorVersion = 2;
  attributes.minorVersion = 0;

  context_ = emscripten_webgl_create_context("#canvas", &attributes);
  if (context_ <= 0) {
    std::printf("Failed to create WebGL2 context.\n");
    return false;
  }

  emscripten_webgl_make_context_current(context_);
  return true;
}

void WebRenderer::make_context_current() {
  if (context_ > 0) {
    emscripten_webgl_make_context_current(context_);
  }
}

void WebRenderer::framebuffer_size(int& width, int& height) {
  emscripten_get_canvas_element_size("#canvas", &width, &height);
}

void WebRenderer::destroy_context() {
  if (context_ > 0) {
    emscripten_webgl_destroy_context(context_);
    context_ = 0;
  }
}

}  // namespace voxel
