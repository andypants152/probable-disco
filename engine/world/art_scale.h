#pragma once

namespace voxel {

// One logical terrain cell remains one gameplay/world unit. Visual detail can subdivide that unit.
constexpr float WORLD_LOGICAL_CELL_SIZE = 1.0f;
constexpr float TERRAIN_VISUAL_VOXEL_SCALE = 0.5f;
constexpr float TREE_VISUAL_VOXEL_SCALE = 0.5f;

constexpr float TREE_TRUNK_DETAIL_DENSITY = 0.28f;
constexpr float TREE_LEAF_DETAIL_DENSITY = 0.88f;
constexpr float TERRAIN_SURFACE_DETAIL_DENSITY = 0.24f;

constexpr bool ROOT_DETAIL_ENABLED = true;
constexpr bool LEDGE_BREAKUP_ENABLED = true;

constexpr int MAX_TREE_DETAIL_VOXELS_PER_CHUNK = 4200;
constexpr int MAX_TERRAIN_DETAIL_VOXELS_PER_CHUNK = 420;

constexpr int TREE_LAYOUT_CELL_SIZE = 8;
constexpr int TREE_SHORT_TRUNK_MIN_CELLS = 5;
constexpr int TREE_MEDIUM_TRUNK_MIN_CELLS = 8;
constexpr int TREE_TALL_TRUNK_MIN_CELLS = 12;
constexpr int TREE_SHORT_LEAF_CLUMP_RADIUS_CELLS = 3;
constexpr int TREE_MEDIUM_LEAF_CLUMP_RADIUS_CELLS = 3;
constexpr int TREE_TALL_LEAF_CLUMP_RADIUS_CELLS = 4;

}  // namespace voxel
