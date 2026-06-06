#include "terrain_streamer.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <utility>

#include "world/chunk.h"
#include "world/generator.h"
#include "world/mesher.h"

namespace voxel {

namespace {

constexpr int kWorldRenderRadiusChunks = 2;
constexpr int kWorldVisualDetailLevel = 2;

int floor_to_int(float value) {
  return static_cast<int>(std::floor(value));
}

int floor_div(int value, int divisor) {
  int quotient = value / divisor;
  const int remainder = value % divisor;
  if (remainder < 0) {
    --quotient;
  }
  return quotient;
}

int chunk_center_step(int current_chunk, int center_chunk) {
  const int delta = current_chunk - center_chunk;
  if (delta > 1) {
    return center_chunk + 1;
  }
  if (delta < -1) {
    return center_chunk - 1;
  }
  return center_chunk;
}

void append_mesh(Mesh& destination, const Mesh& source) {
  const Index index_offset = static_cast<Index>(destination.vertices.size());
  destination.vertices.insert(destination.vertices.end(), source.vertices.begin(), source.vertices.end());
  destination.normals.insert(destination.normals.end(), source.normals.begin(), source.normals.end());
  destination.colors.insert(destination.colors.end(), source.colors.begin(), source.colors.end());
  destination.micro_positions.insert(destination.micro_positions.end(),
                                     source.micro_positions.begin(),
                                     source.micro_positions.end());
  for (Index index : source.indices) {
    destination.indices.push_back(index + index_offset);
  }
}

}  // namespace

void TerrainStreamer::init(const TerrainGenerator& generator, Vec3 focus_position) {
  center_chunk_x_ = floor_div(floor_to_int(focus_position.x), kChunkSize);
  center_chunk_z_ = floor_div(floor_to_int(focus_position.z), kChunkSize);
  chunk_changed_ = true;
  chunk_cache_.clear();
  rebuild(generator);
}

bool TerrainStreamer::update(const TerrainGenerator& generator, Vec3 focus_position) {
  const int current_chunk_x = floor_div(floor_to_int(focus_position.x), kChunkSize);
  const int current_chunk_z = floor_div(floor_to_int(focus_position.z), kChunkSize);
  const int next_center_chunk_x = chunk_center_step(current_chunk_x, center_chunk_x_);
  const int next_center_chunk_z = chunk_center_step(current_chunk_z, center_chunk_z_);
  chunk_changed_ = next_center_chunk_x != center_chunk_x_ || next_center_chunk_z != center_chunk_z_;
  if (!chunk_changed_) {
    stats_.rebuilt_chunks = 0;
    return false;
  }

  center_chunk_x_ = next_center_chunk_x;
  center_chunk_z_ = next_center_chunk_z;
  rebuild(generator);
  return true;
}

void TerrainStreamer::rebuild(const TerrainGenerator& generator) {
  stats_ = {};
  const int span_chunks = kWorldRenderRadiusChunks * 2 + 1;
  const int min_chunk_x = center_chunk_x_ - kWorldRenderRadiusChunks;
  const int min_chunk_z = center_chunk_z_ - kWorldRenderRadiusChunks;
  const int max_chunk_x = center_chunk_x_ + kWorldRenderRadiusChunks;
  const int max_chunk_z = center_chunk_z_ + kWorldRenderRadiusChunks;

  for (int chunk_z = min_chunk_z; chunk_z <= max_chunk_z; ++chunk_z) {
    for (int chunk_x = min_chunk_x; chunk_x <= max_chunk_x; ++chunk_x) {
      auto cached = std::find_if(chunk_cache_.begin(),
                                 chunk_cache_.end(),
                                 [chunk_x, chunk_z](const CachedTerrainChunk& chunk) {
                                   return chunk.chunk_x == chunk_x && chunk.chunk_z == chunk_z;
                                 });
      const int visual_detail_level = kWorldVisualDetailLevel;
      if (cached != chunk_cache_.end() && cached->visual_detail_level == visual_detail_level) {
        continue;
      }

      CachedTerrainChunk chunk;
      chunk.chunk_x = chunk_x;
      chunk.chunk_z = chunk_z;
      chunk.visual_detail_level = visual_detail_level;
      chunk.mesh = build_world_mesh(generator,
                                    chunk_x * kChunkSize,
                                    chunk_z * kChunkSize,
                                    kChunkSize,
                                    kChunkSize,
                                    visual_detail_level);
      ++stats_.rebuilt_chunks;
      if (cached != chunk_cache_.end()) {
        *cached = std::move(chunk);
      } else {
        chunk_cache_.push_back(std::move(chunk));
      }
    }
  }

  std::size_t vertex_count = 0;
  std::size_t index_count = 0;
  for (const CachedTerrainChunk& chunk : chunk_cache_) {
    if (chunk.chunk_x < min_chunk_x || chunk.chunk_x > max_chunk_x ||
        chunk.chunk_z < min_chunk_z || chunk.chunk_z > max_chunk_z) {
      continue;
    }
    ++stats_.visible_chunks;
    vertex_count += chunk.mesh.vertices.size();
    index_count += chunk.mesh.indices.size();
    stats_.largest_chunk_vertices = std::max(stats_.largest_chunk_vertices, chunk.mesh.vertices.size());
    stats_.largest_chunk_triangles = std::max(stats_.largest_chunk_triangles, chunk.mesh.indices.size() / 3u);
  }
  stats_.visible_vertices = vertex_count;
  stats_.visible_triangles = index_count / 3u;

  terrain_mesh_.clear();
  terrain_mesh_.vertices.reserve(vertex_count);
  terrain_mesh_.normals.reserve(vertex_count);
  terrain_mesh_.colors.reserve(vertex_count);
  terrain_mesh_.micro_positions.reserve(vertex_count);
  terrain_mesh_.indices.reserve(index_count);

  for (int chunk_z = min_chunk_z; chunk_z <= max_chunk_z; ++chunk_z) {
    for (int chunk_x = min_chunk_x; chunk_x <= max_chunk_x; ++chunk_x) {
      const auto cached = std::find_if(chunk_cache_.begin(),
                                       chunk_cache_.end(),
                                       [chunk_x, chunk_z](const CachedTerrainChunk& chunk) {
                                         return chunk.chunk_x == chunk_x && chunk.chunk_z == chunk_z;
                                       });
      if (cached != chunk_cache_.end()) {
        append_mesh(terrain_mesh_, cached->mesh);
      }
    }
  }

  std::vector<CachedTerrainChunk> visible_cache;
  visible_cache.reserve(static_cast<std::size_t>(span_chunks * span_chunks));
  for (CachedTerrainChunk& chunk : chunk_cache_) {
    if (chunk.chunk_x < min_chunk_x || chunk.chunk_x > max_chunk_x ||
        chunk.chunk_z < min_chunk_z || chunk.chunk_z > max_chunk_z) {
      continue;
    }
    visible_cache.push_back(std::move(chunk));
  }
  chunk_cache_ = std::move(visible_cache);
}

}  // namespace voxel
