#include "subtitles.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace voxel {

namespace {

#if defined(__SWITCH__)
constexpr const char* kFontPath = "romfs:/assets/fonts/subtitle.ttf";
constexpr int kFontSize = 32;
constexpr int kHudFontSize = 24;
#else
constexpr const char* kFontPath = "assets/fonts/subtitle.ttf";
constexpr int kFontSize = 28;
constexpr int kHudFontSize = 18;
#endif

constexpr int kMaxTextWidth = 900;
constexpr int kPaddingX = 28;
constexpr int kPaddingY = 16;
constexpr int kHudMaxTextWidth = 240;
constexpr int kHudPaddingX = 10;
constexpr int kHudPaddingY = 6;
constexpr int kTextureAlignment = 4;

struct CachedTextFrame {
  std::vector<unsigned char> pixels;
  SubtitleFrame frame;
};

struct SubtitleState {
  bool initialized = false;
  bool ttf_owned = false;
  TTF_Font* font = nullptr;
  TTF_Font* hud_font = nullptr;
  CachedTextFrame subtitle;
  CachedTextFrame hud;
  float remaining_seconds = 0.0f;
  float total_seconds = 0.0f;
};

SubtitleState g_subtitles;

void subtitle_log(const char* format, ...) {
#if defined(__SWITCH__) && (defined(VOXEL_SWITCH_TIMING) || defined(VOXEL_SWITCH_PROFILE))
  std::printf("[subtitles] ");
  va_list args;
  va_start(args, format);
  std::vprintf(format, args);
  va_end(args);
  std::printf("\n");
#else
  (void)format;
#endif
}

int aligned(int value) {
  return (value + kTextureAlignment - 1) & ~(kTextureAlignment - 1);
}

void clear_frame(CachedTextFrame& cache) {
  cache.pixels.clear();
  cache.frame.pixels = nullptr;
  cache.frame.width = 0;
  cache.frame.height = 0;
  cache.frame.alpha = 1.0f;
  cache.frame.visible = false;
  ++cache.frame.generation;
}

bool render_text(CachedTextFrame& cache,
                 TTF_Font* font,
                 const char* text,
                 int max_text_width,
                 int padding_x,
                 int padding_y,
                 bool compact) {
  if (font == nullptr || text == nullptr || text[0] == '\0') {
    clear_frame(cache);
    return false;
  }

  const SDL_Color text_color = {242, 238, 220, 255};
  SDL_Surface* text_surface = TTF_RenderUTF8_Blended_Wrapped(font,
                                                             text,
                                                             text_color,
                                                             max_text_width);
  if (text_surface == nullptr) {
    std::printf("Subtitle render failed: %s\n", TTF_GetError());
    clear_frame(cache);
    return false;
  }

  SDL_Surface* rgba_text = SDL_ConvertSurfaceFormat(text_surface, SDL_PIXELFORMAT_RGBA32, 0);
  SDL_FreeSurface(text_surface);
  if (rgba_text == nullptr) {
    std::printf("Subtitle format conversion failed: %s\n", SDL_GetError());
    clear_frame(cache);
    return false;
  }

  const int width = aligned(rgba_text->w + padding_x * 2);
  const int height = aligned(rgba_text->h + padding_y * 2);
  cache.pixels.assign(static_cast<std::size_t>(width * height * 4), 0);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int edge = std::min(std::min(x, width - 1 - x), std::min(y, height - 1 - y));
      const bool border = !compact && edge < 3;
      unsigned char* dst = &cache.pixels[static_cast<std::size_t>((y * width + x) * 4)];
      dst[0] = border ? 96 : 10;
      dst[1] = border ? 210 : 13;
      dst[2] = border ? 198 : 12;
      dst[3] = compact ? 184 : (border ? 220 : 190);
    }
  }

  const int text_x = (width - rgba_text->w) / 2;
  const int text_y = (height - rgba_text->h) / 2;
  const auto* src_pixels = static_cast<const unsigned char*>(rgba_text->pixels);
  for (int y = 0; y < rgba_text->h; ++y) {
    for (int x = 0; x < rgba_text->w; ++x) {
      const unsigned char* src = src_pixels + static_cast<std::size_t>(y * rgba_text->pitch + x * 4);
      const unsigned char src_alpha = src[3];
      if (src_alpha == 0) {
        continue;
      }

      unsigned char* dst = &cache.pixels[static_cast<std::size_t>(((text_y + y) * width + text_x + x) * 4)];
      const float alpha = static_cast<float>(src_alpha) / 255.0f;
      dst[0] = static_cast<unsigned char>(static_cast<float>(dst[0]) * (1.0f - alpha) + static_cast<float>(src[0]) * alpha);
      dst[1] = static_cast<unsigned char>(static_cast<float>(dst[1]) * (1.0f - alpha) + static_cast<float>(src[1]) * alpha);
      dst[2] = static_cast<unsigned char>(static_cast<float>(dst[2]) * (1.0f - alpha) + static_cast<float>(src[2]) * alpha);
      dst[3] = static_cast<unsigned char>(std::max<int>(dst[3], src_alpha));
    }
  }

  SDL_FreeSurface(rgba_text);
  cache.frame.pixels = cache.pixels.data();
  cache.frame.width = width;
  cache.frame.height = height;
  cache.frame.alpha = 1.0f;
  cache.frame.visible = true;
  ++cache.frame.generation;
  subtitle_log("rendered %dx%d generation %u: \"%s\"",
               width,
               height,
               static_cast<unsigned>(cache.frame.generation),
               text);
  return true;
}

}  // namespace

bool subtitles_init() {
  if (g_subtitles.initialized) {
    return true;
  }

  subtitle_log("init using font %s", kFontPath);
  if (TTF_WasInit() == 0) {
    if (TTF_Init() != 0) {
      std::printf("TTF_Init failed: %s\n", TTF_GetError());
      return false;
    }
    g_subtitles.ttf_owned = true;
    subtitle_log("TTF_Init ok");
  }

  g_subtitles.font = TTF_OpenFont(kFontPath, kFontSize);
  if (g_subtitles.font == nullptr) {
    std::printf("Subtitle font load failed at %s: %s\n", kFontPath, TTF_GetError());
    if (g_subtitles.ttf_owned) {
      TTF_Quit();
    }
    g_subtitles = {};
    return false;
  }
  subtitle_log("opened subtitle font size %d", kFontSize);
  g_subtitles.hud_font = TTF_OpenFont(kFontPath, kHudFontSize);
  if (g_subtitles.hud_font == nullptr) {
    std::printf("HUD font load failed at %s: %s\n", kFontPath, TTF_GetError());
    TTF_CloseFont(g_subtitles.font);
    if (g_subtitles.ttf_owned) {
      TTF_Quit();
    }
    g_subtitles = {};
    return false;
  }
  subtitle_log("opened HUD font size %d", kHudFontSize);

  g_subtitles.initialized = true;
  subtitle_log("init complete");
  return true;
}

void subtitles_shutdown() {
  if (g_subtitles.font != nullptr) {
    TTF_CloseFont(g_subtitles.font);
  }
  if (g_subtitles.hud_font != nullptr) {
    TTF_CloseFont(g_subtitles.hud_font);
  }
  if (g_subtitles.ttf_owned) {
    TTF_Quit();
  }
  g_subtitles = {};
}

void subtitles_show(const char* text, float seconds) {
  if (!g_subtitles.initialized) {
    return;
  }

  if (!render_text(g_subtitles.subtitle,
                   g_subtitles.font,
                   text,
                   kMaxTextWidth,
                   kPaddingX,
                   kPaddingY,
                   false)) {
    return;
  }

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
  std::snprintf(text, sizeof(text), "FPS %.1f", fps);
  render_text(g_subtitles.hud,
              g_subtitles.hud_font,
              text,
              kHudMaxTextWidth,
              kHudPaddingX,
              kHudPaddingY,
              true);
}

const SubtitleFrame& subtitles_frame() {
  return g_subtitles.subtitle.frame;
}

const SubtitleFrame& subtitles_hud_frame() {
  return g_subtitles.hud.frame;
}

}  // namespace voxel
