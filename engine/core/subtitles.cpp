#include "subtitles.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace voxel {

namespace {

struct CachedTextFrame {
  std::string text;
  SubtitleFrame frame;
};

struct SubtitleState {
  bool initialized = false;
  CachedTextFrame subtitle;
  CachedTextFrame hud;
  CachedTextFrame fps;
  float remaining_seconds = 0.0f;
  float total_seconds = 0.0f;
};

SubtitleState g_subtitles;

void sync_frame(CachedTextFrame& cache, bool visible, bool compact, SubtitlePlacement placement) {
  cache.frame.text = cache.text.c_str();
  cache.frame.alpha = 1.0f;
  cache.frame.visible = visible && !cache.text.empty();
  cache.frame.compact = compact;
  cache.frame.placement = placement;
  ++cache.frame.generation;
}

void clear_frame(CachedTextFrame& cache) {
  cache.text.clear();
  sync_frame(cache, false, cache.frame.compact, cache.frame.placement);
}

}  // namespace

bool subtitles_init() {
  if (g_subtitles.initialized) {
    return true;
  }
  g_subtitles.initialized = true;
  g_subtitles.subtitle.frame.placement = SubtitlePlacement::BottomCenter;
  g_subtitles.hud.frame.placement = SubtitlePlacement::TopCenter;
  g_subtitles.fps.frame.placement = SubtitlePlacement::TopRight;
  return true;
}

void subtitles_shutdown() {
  g_subtitles = {};
}

void subtitles_show(const char* text, float seconds) {
  if (!g_subtitles.initialized || text == nullptr || text[0] == '\0') {
    return;
  }

  g_subtitles.subtitle.text = text;
  sync_frame(g_subtitles.subtitle, true, false, SubtitlePlacement::BottomCenter);
  g_subtitles.total_seconds = std::max(0.1f, seconds);
  g_subtitles.remaining_seconds = g_subtitles.total_seconds;
}

void subtitles_clear() {
  if (!g_subtitles.initialized) {
    return;
  }
  clear_frame(g_subtitles.subtitle);
  g_subtitles.remaining_seconds = 0.0f;
  g_subtitles.total_seconds = 0.0f;
}

void subtitles_update(float dt) {
  if (!g_subtitles.initialized || !g_subtitles.subtitle.frame.visible) {
    return;
  }

  g_subtitles.remaining_seconds -= std::max(0.0f, dt);
  if (g_subtitles.remaining_seconds <= 0.0f) {
    clear_frame(g_subtitles.subtitle);
    return;
  }

  const float fade_window = std::min(0.35f, g_subtitles.total_seconds * 0.25f);
  g_subtitles.subtitle.frame.alpha = g_subtitles.remaining_seconds < fade_window
      ? std::max(0.0f, g_subtitles.remaining_seconds / fade_window)
      : 1.0f;
}

bool subtitles_visible() {
  return g_subtitles.initialized && g_subtitles.subtitle.frame.visible;
}

void subtitles_set_fps(float fps) {
  if (!g_subtitles.initialized) {
    return;
  }

  char text[32] = {};
  std::snprintf(text, sizeof(text), "FPS %.1f", static_cast<double>(fps));
  g_subtitles.fps.text = text;
  sync_frame(g_subtitles.fps, true, true, SubtitlePlacement::TopRight);
}

void subtitles_set_hud_text(const char* text) {
  if (!g_subtitles.initialized) {
    return;
  }

  if (text == nullptr || text[0] == '\0') {
    clear_frame(g_subtitles.hud);
    return;
  }

  g_subtitles.hud.text = text;
  sync_frame(g_subtitles.hud, true, true, SubtitlePlacement::TopCenter);
}

const SubtitleFrame& subtitles_frame() {
  return g_subtitles.subtitle.frame;
}

const SubtitleFrame& subtitles_hud_frame() {
  return g_subtitles.hud.frame;
}

const SubtitleFrame& subtitles_fps_frame() {
  return g_subtitles.fps.frame;
}

}  // namespace voxel
