#pragma once

#include <cstdint>

namespace voxel {

struct SubtitleFrame {
  const char* text = "";
  float alpha = 1.0f;
  std::uint32_t generation = 0;
  bool visible = false;
  bool compact = false;
};

bool subtitles_init();
void subtitles_shutdown();
void subtitles_show(const char* text, float seconds);
void subtitles_clear();
void subtitles_update(float dt);
bool subtitles_visible();
void subtitles_set_fps(float fps);

const SubtitleFrame& subtitles_frame();
const SubtitleFrame& subtitles_hud_frame();

}  // namespace voxel
