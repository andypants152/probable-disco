#pragma once

#include <cstdint>

namespace voxel {

using Index = std::uint32_t;
using PackedColor = std::uint32_t;

constexpr PackedColor pack_rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
  return (static_cast<PackedColor>(r) << 24) |
         (static_cast<PackedColor>(g) << 16) |
         (static_cast<PackedColor>(b) << 8) |
         static_cast<PackedColor>(a);
}

}  // namespace voxel
