#include "render/bitmap_font.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace voxel {

namespace {

constexpr float kGlyphAdvance = 6.0f;
constexpr float kLineAdvance = 9.0f;

const char* glyph_rows(char c) {
  switch (c) {
    case 'A': return "01110""10001""10001""11111""10001""10001""10001";
    case 'B': return "11110""10001""10001""11110""10001""10001""11110";
    case 'C': return "01111""10000""10000""10000""10000""10000""01111";
    case 'D': return "11110""10001""10001""10001""10001""10001""11110";
    case 'E': return "11111""10000""10000""11110""10000""10000""11111";
    case 'F': return "11111""10000""10000""11110""10000""10000""10000";
    case 'G': return "01111""10000""10000""10011""10001""10001""01111";
    case 'H': return "10001""10001""10001""11111""10001""10001""10001";
    case 'I': return "11111""00100""00100""00100""00100""00100""11111";
    case 'J': return "00111""00010""00010""00010""10010""10010""01100";
    case 'K': return "10001""10010""10100""11000""10100""10010""10001";
    case 'L': return "10000""10000""10000""10000""10000""10000""11111";
    case 'M': return "10001""11011""10101""10101""10001""10001""10001";
    case 'N': return "10001""11001""10101""10011""10001""10001""10001";
    case 'O': return "01110""10001""10001""10001""10001""10001""01110";
    case 'P': return "11110""10001""10001""11110""10000""10000""10000";
    case 'Q': return "01110""10001""10001""10001""10101""10010""01101";
    case 'R': return "11110""10001""10001""11110""10100""10010""10001";
    case 'S': return "01111""10000""10000""01110""00001""00001""11110";
    case 'T': return "11111""00100""00100""00100""00100""00100""00100";
    case 'U': return "10001""10001""10001""10001""10001""10001""01110";
    case 'V': return "10001""10001""10001""10001""10001""01010""00100";
    case 'W': return "10001""10001""10001""10101""10101""10101""01010";
    case 'X': return "10001""10001""01010""00100""01010""10001""10001";
    case 'Y': return "10001""10001""01010""00100""00100""00100""00100";
    case 'Z': return "11111""00001""00010""00100""01000""10000""11111";
    case '0': return "01110""10001""10011""10101""11001""10001""01110";
    case '1': return "00100""01100""00100""00100""00100""00100""01110";
    case '2': return "01110""10001""00001""00010""00100""01000""11111";
    case '3': return "11110""00001""00001""01110""00001""00001""11110";
    case '4': return "00010""00110""01010""10010""11111""00010""00010";
    case '5': return "11111""10000""10000""11110""00001""00001""11110";
    case '6': return "01111""10000""10000""11110""10001""10001""01110";
    case '7': return "11111""00001""00010""00100""01000""01000""01000";
    case '8': return "01110""10001""10001""01110""10001""10001""01110";
    case '9': return "01110""10001""10001""01111""00001""00001""11110";
    case '.': return "00000""00000""00000""00000""00000""01100""01100";
    case ',': return "00000""00000""00000""00000""01100""01100""01000";
    case '\'': return "01100""01100""01000""00000""00000""00000""00000";
    case '"': return "01010""01010""01010""00000""00000""00000""00000";
    case ':': return "00000""01100""01100""00000""01100""01100""00000";
    case '!': return "00100""00100""00100""00100""00100""00000""00100";
    case '?': return "01110""10001""00001""00010""00100""00000""00100";
    case '-': return "00000""00000""00000""11110""00000""00000""00000";
    case '/': return "00001""00010""00010""00100""01000""01000""10000";
    default: return "00000""00000""00000""00000""00000""00000""00000";
  }
}

char atlas_char(char c) {
  const unsigned char uc = static_cast<unsigned char>(c);
  if (uc >= 'a' && uc <= 'z') {
    return static_cast<char>(std::toupper(uc));
  }
  return c;
}

float word_width(const char* text, float glyph_scale) {
  float width = 0.0f;
  while (*text != '\0' && *text != ' ' && *text != '\n') {
    width += kGlyphAdvance * glyph_scale;
    ++text;
  }
  return width;
}

}  // namespace

void BitmapFontAtlas::build() {
  pixels_.assign(static_cast<std::size_t>(kWidth * kHeight * 4), 0);

  auto set_pixel = [this](int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) {
      return;
    }
    unsigned char* pixel = &pixels_[static_cast<std::size_t>((y * kWidth + x) * 4)];
    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
    pixel[3] = a;
  };

  set_pixel(0, 0, 255, 255, 255, 255);

  for (int code = 32; code < 128; ++code) {
    const char c = atlas_char(static_cast<char>(code));
    const char* rows = glyph_rows(c);
    const int column = code % kColumns;
    const int row = code / kColumns;
    const int origin_x = column * kCellSize + 1;
    const int origin_y = row * kCellSize;
    for (int y = 0; y < 7; ++y) {
      for (int x = 0; x < 5; ++x) {
        if (rows[y * 5 + x] == '1') {
          set_pixel(origin_x + x, origin_y + y, 255, 255, 255, 255);
        }
      }
    }
  }
}

void BitmapFontAtlas::glyph_uv(char c, float& u0, float& v0, float& u1, float& v1) const {
  unsigned char code = static_cast<unsigned char>(c);
  if (code < 32 || code >= 128) {
    code = static_cast<unsigned char>('?');
  }
  const int column = code % kColumns;
  const int row = code / kColumns;
  u0 = static_cast<float>(column * kCellSize) / static_cast<float>(kWidth);
  v0 = static_cast<float>(row * kCellSize) / static_cast<float>(kHeight);
  u1 = static_cast<float>((column + 1) * kCellSize) / static_cast<float>(kWidth);
  v1 = static_cast<float>((row + 1) * kCellSize) / static_cast<float>(kHeight);
}

void BitmapFontAtlas::solid_uv(float& u0, float& v0, float& u1, float& v1) const {
  u0 = 0.0f;
  v0 = 0.0f;
  u1 = 1.0f / static_cast<float>(kWidth);
  v1 = 1.0f / static_cast<float>(kHeight);
}

TextLayoutMetrics measure_bitmap_text(const char* text, float glyph_scale, float max_width) {
  TextLayoutMetrics metrics = {};
  if (text == nullptr || text[0] == '\0') {
    return metrics;
  }

  float x = 0.0f;
  float y = 0.0f;
  for (const char* at = text; *at != '\0'; ++at) {
    if (*at == '\n') {
      metrics.width = std::max(metrics.width, x);
      x = 0.0f;
      y += kLineAdvance * glyph_scale;
      continue;
    }
    if (*at == ' ') {
      const float next_word_width = word_width(at + 1, glyph_scale);
      if (x > 0.0f && max_width > 0.0f && x + kGlyphAdvance * glyph_scale + next_word_width > max_width) {
        metrics.width = std::max(metrics.width, x);
        x = 0.0f;
        y += kLineAdvance * glyph_scale;
        continue;
      }
    }
    x += kGlyphAdvance * glyph_scale;
  }

  metrics.width = std::max(metrics.width, x);
  metrics.height = y + static_cast<float>(BitmapFontAtlas::kCellSize) * glyph_scale;
  return metrics;
}

void append_bitmap_text(QuadBatch& batch,
                        const BitmapFontAtlas& atlas,
                        const char* text,
                        float x,
                        float y,
                        float glyph_scale,
                        float max_width,
                        PackedColor color) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }

  float pen_x = x;
  float pen_y = y;
  for (const char* at = text; *at != '\0'; ++at) {
    if (*at == '\n') {
      pen_x = x;
      pen_y += kLineAdvance * glyph_scale;
      continue;
    }
    if (*at == ' ') {
      const float next_word_width = word_width(at + 1, glyph_scale);
      if (pen_x > x && max_width > 0.0f &&
          pen_x - x + kGlyphAdvance * glyph_scale + next_word_width > max_width) {
        pen_x = x;
        pen_y += kLineAdvance * glyph_scale;
        continue;
      }
      pen_x += kGlyphAdvance * glyph_scale;
      continue;
    }

    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    atlas.glyph_uv(*at, u0, v0, u1, v1);
    batch.add(pen_x,
              pen_y,
              static_cast<float>(BitmapFontAtlas::kCellSize) * glyph_scale,
              static_cast<float>(BitmapFontAtlas::kCellSize) * glyph_scale,
              u0,
              v0,
              u1,
              v1,
              color);
    pen_x += kGlyphAdvance * glyph_scale;
  }
}

void build_subtitle_batch(QuadBatch& batch,
                          const BitmapFontAtlas& atlas,
                          const SubtitleFrame& subtitle,
                          int framebuffer_width,
                          int framebuffer_height) {
  batch.clear();
  if (!subtitle.visible || subtitle.text == nullptr || subtitle.text[0] == '\0' || subtitle.alpha <= 0.0f) {
    return;
  }

  const float framebuffer_min = static_cast<float>(std::min(framebuffer_width, framebuffer_height));
  const float glyph_scale = subtitle.compact ? std::max(2.0f, framebuffer_min / 390.0f)
                                             : std::max(3.0f, framebuffer_min / 230.0f);
  const float max_text_width = static_cast<float>(framebuffer_width) * (subtitle.compact ? 0.32f : 0.78f);
  const float pad_x = subtitle.compact ? 10.0f : 22.0f;
  const float pad_y = subtitle.compact ? 7.0f : 15.0f;
  const TextLayoutMetrics metrics = measure_bitmap_text(subtitle.text, glyph_scale, max_text_width);
  if (metrics.width <= 0.0f || metrics.height <= 0.0f) {
    return;
  }

  const float panel_width = metrics.width + pad_x * 2.0f;
  const float panel_height = metrics.height + pad_y * 2.0f;
  const float x = (static_cast<float>(framebuffer_width) - panel_width) * 0.5f;
  const float y = subtitle.compact
      ? 20.0f
      : static_cast<float>(framebuffer_height) - panel_height - std::max(34.0f, framebuffer_min * 0.055f);

  const std::uint8_t alpha = static_cast<std::uint8_t>(std::clamp(subtitle.alpha, 0.0f, 1.0f) * 255.0f);
  float solid_u0 = 0.0f;
  float solid_v0 = 0.0f;
  float solid_u1 = 0.0f;
  float solid_v1 = 0.0f;
  atlas.solid_uv(solid_u0, solid_v0, solid_u1, solid_v1);
  batch.add(x,
            y,
            panel_width,
            panel_height,
            solid_u0,
            solid_v0,
            solid_u1,
            solid_v1,
            pack_rgba(8, 13, 12, static_cast<std::uint8_t>(subtitle.compact ? alpha * 150 / 255 : alpha * 185 / 255)));

  append_bitmap_text(batch,
                     atlas,
                     subtitle.text,
                     x + pad_x,
                     y + pad_y,
                     glyph_scale,
                     max_text_width,
                     pack_rgba(242, 238, 220, alpha));
}

}  // namespace voxel
