#pragma once

#include <array>
#include <vector>

#include "game/camera.h"
#include "render/lights.h"

namespace voxel {

struct SubtitleFrame;

enum class RenderCommandType {
  DrawStaticMesh,
  DrawDynamicMesh,
  DrawSubtitle,
};

struct RenderCommand {
  RenderCommandType type = RenderCommandType::DrawStaticMesh;
};

struct RenderFrame {
  Camera camera = {};
  std::array<GameplayLight, kMaxRendererGameplayLights> lights = {};
  int light_count = 0;
  const SubtitleFrame* subtitle = nullptr;
  std::vector<RenderCommand> commands;

  void clear() {
    light_count = 0;
    subtitle = nullptr;
    commands.clear();
  }
};

}  // namespace voxel
