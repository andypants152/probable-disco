#pragma once

#include <cstdint>
#include <vector>

#include "core/subtitles.h"
#include "render/quad_batch.h"

namespace voxel {

class BitmapFontAtlas {
 public:
  static constexpr int kCellSize = 8;
  static constexpr int kColumns = 16;
  static constexpr int kRows = 8;
  static constexpr int kWidth = kColumns * kCellSize;
  static constexpr int kHeight = kRows * kCellSize;

  const std::vector<unsigned char>& pixels() const { return pixels_; }
  int width() const { return kWidth; }
  int height() const { return kHeight; }

  void build();
  void glyph_uv(char c, float& u0, float& v0, float& u1, float& v1) const;
  void solid_uv(float& u0, float& v0, float& u1, float& v1) const;

 private:
  std::vector<unsigned char> pixels_;
};

struct TextLayoutMetrics {
  float width = 0.0f;
  float height = 0.0f;
};

TextLayoutMetrics measure_bitmap_text(const char* text, float glyph_scale, float max_width);
void append_bitmap_text(QuadBatch& batch,
                        const BitmapFontAtlas& atlas,
                        const char* text,
                        float x,
                        float y,
                        float glyph_scale,
                        float max_width,
                        PackedColor color);
void build_subtitle_batch(QuadBatch& batch,
                          const BitmapFontAtlas& atlas,
                          const SubtitleFrame& subtitle,
                          int framebuffer_width,
                          int framebuffer_height);
void append_subtitle_batch(QuadBatch& batch,
                           const BitmapFontAtlas& atlas,
                           const SubtitleFrame& subtitle,
                           int framebuffer_width,
                           int framebuffer_height);

}  // namespace voxel
