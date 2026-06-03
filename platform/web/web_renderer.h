#pragma once

#include <emscripten/html5.h>

#include "render/gles_renderer.h"

namespace voxel {

class WebRenderer final : public GlesRenderer {
 protected:
  bool create_context() override;
  void make_context_current() override;
  void framebuffer_size(int& width, int& height) override;
  void destroy_context() override;

 private:
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context_ = 0;
};

}  // namespace voxel
