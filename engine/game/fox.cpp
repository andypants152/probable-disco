#include "fox.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace voxel {

namespace {

struct Box {
  Vec3 min;
  Vec3 max;
  PackedColor color;
};

struct Face {
  Vec3 normal;
  std::array<Vec3, 4> corners;
};

struct PartTransform {
  Vec3 offset;
  Vec3 pivot;
  float pitch = 0.0f;
  float yaw = 0.0f;
  float roll = 0.0f;
};

constexpr std::array<Face, 6> kBoxFaces = {{
  {{1.0f, 0.0f, 0.0f}, {{{1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 1.0f}}}},
  {{-1.0f, 0.0f, 0.0f}, {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}}}},
  {{0.0f, 1.0f, 0.0f}, {{{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}}}},
  {{0.0f, -1.0f, 0.0f}, {{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}}},
  {{0.0f, 0.0f, 1.0f}, {{{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f}}}},
  {{0.0f, 0.0f, -1.0f}, {{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}}},
}};

float clamp_unit(float value) {
  return std::max(-1.0f, std::min(1.0f, value));
}

Vec3 rotate_y(Vec3 v, float heading) {
  const float s = std::sin(heading);
  const float c = std::cos(heading);
  return {
    v.x * c + v.z * s,
    v.y,
    -v.x * s + v.z * c,
  };
}

Vec3 rotate_x(Vec3 v, float angle) {
  const float s = std::sin(angle);
  const float c = std::cos(angle);
  return {
    v.x,
    v.y * c - v.z * s,
    v.y * s + v.z * c,
  };
}

Vec3 rotate_z(Vec3 v, float angle) {
  const float s = std::sin(angle);
  const float c = std::cos(angle);
  return {
    v.x * c - v.y * s,
    v.x * s + v.y * c,
    v.z,
  };
}

Vec3 apply_part_rotation(Vec3 v, const PartTransform& transform) {
  v = rotate_x(v, transform.pitch);
  v = rotate_y(v, transform.yaw);
  v = rotate_z(v, transform.roll);
  return v;
}

Vec3 transform_local_position(Vec3 local, const PartTransform& transform) {
  return transform.pivot + apply_part_rotation(local - transform.pivot, transform) + transform.offset;
}

void append_box(Mesh& mesh, const Box& box, Vec3 origin, float heading, const PartTransform& transform = {}) {
  for (const Face& face : kBoxFaces) {
    const Index start = static_cast<Index>(mesh.vertices.size());
    const Vec3 normal = rotate_y(apply_part_rotation(face.normal, transform), heading);
    for (const Vec3& corner : face.corners) {
      const Vec3 local = {
        box.min.x + (box.max.x - box.min.x) * corner.x,
        box.min.y + (box.max.y - box.min.y) * corner.y,
        box.min.z + (box.max.z - box.min.z) * corner.z,
      };
      const Vec3 rotated = rotate_y(transform_local_position(local, transform), heading);
      mesh.vertices.push_back(origin + rotated);
      mesh.normals.push_back(normal);
      mesh.colors.push_back(box.color);
      mesh.micro_positions.push_back({corner.x - 0.5f, corner.y - 0.5f, corner.z - 0.5f});
    }

    mesh.indices.push_back(start + 0);
    mesh.indices.push_back(start + 1);
    mesh.indices.push_back(start + 2);
    mesh.indices.push_back(start + 0);
    mesh.indices.push_back(start + 2);
    mesh.indices.push_back(start + 3);
  }
}

}  // namespace

void append_fox_mesh(Mesh& mesh, Vec3 ground_center, float heading_radians, const FoxAnimationPose& pose) {
  constexpr PackedColor orange = pack_rgba(210, 92, 34);
  constexpr PackedColor dark_orange = pack_rgba(166, 66, 28);
  constexpr PackedColor warm_orange = pack_rgba(226, 112, 43);
  constexpr PackedColor white = pack_rgba(236, 231, 211);
  constexpr PackedColor black = pack_rgba(22, 19, 16);

  const float walk = std::max(0.0f, std::min(1.0f, pose.walk_blend));
  const float gait = std::sin(pose.walk_cycle);
  const float pair_a_lift = std::max(0.0f, std::cos(pose.walk_cycle)) * walk * 0.075f;
  const float pair_b_lift = std::max(0.0f, -std::cos(pose.walk_cycle)) * walk * 0.075f;
  const float pair_a_swing = gait * walk * 0.34f;
  const float pair_b_swing = -gait * walk * 0.34f;
  const float tail_sway = clamp_unit(pose.tail_sway);
  const float ear_twitch = std::max(0.0f, std::min(1.0f, pose.ear_twitch));

  const PartTransform body = {
    {0.0f, pose.body_bob, 0.0f},
    {0.0f, 0.72f, 0.14f},
    pose.body_pitch,
    0.0f,
    0.0f,
  };
  const PartTransform chest = body;
  const PartTransform head = {
    {0.0f, pose.body_bob + pose.head_bob, 0.0f},
    {0.0f, 1.28f, 0.88f},
    pose.body_pitch * 0.45f,
    walk * std::sin(pose.walk_cycle + 0.60f) * 0.025f,
    0.0f,
  };
  const PartTransform left_ear = {
    {0.0f, pose.body_bob + pose.head_bob, 0.0f},
    {-0.48f, 1.88f, 1.16f},
    pose.body_pitch * 0.35f + ear_twitch * 0.16f,
    0.0f,
    -ear_twitch * 0.08f,
  };
  const PartTransform right_ear = {
    {0.0f, pose.body_bob + pose.head_bob, 0.0f},
    {0.48f, 1.88f, 1.16f},
    pose.body_pitch * 0.35f + ear_twitch * 0.12f,
    0.0f,
    ear_twitch * 0.06f,
  };
  const PartTransform back_left_leg = {
    {0.0f, pair_b_lift, 0.0f},
    {-0.78f, 0.62f, -0.58f},
    pair_b_swing,
    0.0f,
    0.0f,
  };
  const PartTransform back_right_leg = {
    {0.0f, pair_a_lift, 0.0f},
    {0.78f, 0.62f, -0.58f},
    pair_a_swing,
    0.0f,
    0.0f,
  };
  const PartTransform front_left_leg = {
    {0.0f, pair_a_lift, 0.0f},
    {-0.78f, 0.62f, 0.65f},
    pair_a_swing,
    0.0f,
    0.0f,
  };
  const PartTransform front_right_leg = {
    {0.0f, pair_b_lift, 0.0f},
    {0.78f, 0.62f, 0.65f},
    pair_b_swing,
    0.0f,
    0.0f,
  };
  const PartTransform tail_base = {
    {0.0f, pose.body_bob * 0.75f, 0.0f},
    {0.0f, 1.20f, -0.98f},
    pose.body_pitch * 0.25f,
    tail_sway * 0.13f,
    0.0f,
  };
  const PartTransform tail_mid = {
    {0.0f, pose.body_bob * 0.75f, 0.0f},
    {0.0f, 1.30f, -1.85f},
    pose.body_pitch * 0.20f,
    tail_sway * 0.19f,
    0.0f,
  };
  const PartTransform tail_tip = {
    {0.0f, pose.body_bob * 0.75f, 0.0f},
    {0.0f, 1.52f, -2.62f},
    pose.body_pitch * 0.15f,
    tail_sway * 0.24f,
    0.0f,
  };

  // Body and chest.
  const std::array<Box, 9> body_boxes = {{
    {{-1.02f, 0.52f, -0.90f}, {1.02f, 1.30f, 0.92f}, orange},
    {{-0.78f, 1.18f, -0.78f}, {0.78f, 1.50f, 0.76f}, warm_orange},
    {{-0.54f, 1.36f, -0.54f}, {0.54f, 1.58f, 0.44f}, warm_orange},
    {{-0.86f, 0.40f, -0.66f}, {0.86f, 0.76f, 0.70f}, dark_orange},
    {{-1.22f, 0.68f, -0.70f}, {-0.78f, 1.16f, 0.62f}, orange},
    {{0.78f, 0.68f, -0.70f}, {1.22f, 1.16f, 0.62f}, orange},
    {{-0.80f, 0.62f, -1.15f}, {0.80f, 1.22f, -0.80f}, dark_orange},
    {{-0.86f, 0.64f, 0.78f}, {0.86f, 1.26f, 1.16f}, warm_orange},
    {{-0.46f, 0.48f, -1.00f}, {0.46f, 0.74f, -0.56f}, dark_orange},
  }};
  for (const Box& box : body_boxes) {
    append_box(mesh, box, ground_center, heading_radians, body);
  }

  const std::array<Box, 3> chest_boxes = {{
    {{-0.62f, 0.64f, 0.94f}, {0.62f, 1.26f, 1.28f}, white},
    {{-0.42f, 0.50f, 0.82f}, {0.42f, 0.88f, 1.14f}, white},
    {{-0.28f, 1.18f, 1.02f}, {0.28f, 1.40f, 1.22f}, white},
  }};
  for (const Box& box : chest_boxes) {
    append_box(mesh, box, ground_center, heading_radians, chest);
  }

  // Head, snout, ears, and eyes. The fox faces +Z toward the starting camera.
  const std::array<Box, 5> head_boxes = {{
    {{-0.62f, 1.12f, 0.88f}, {0.62f, 1.86f, 1.66f}, orange},
    {{-0.46f, 1.78f, 0.98f}, {0.46f, 2.04f, 1.50f}, warm_orange},
    {{-0.80f, 1.26f, 1.04f}, {-0.46f, 1.70f, 1.52f}, orange},
    {{0.46f, 1.26f, 1.04f}, {0.80f, 1.70f, 1.52f}, orange},
    {{-0.42f, 1.02f, 1.08f}, {0.42f, 1.30f, 1.56f}, dark_orange},
  }};
  for (const Box& box : head_boxes) {
    append_box(mesh, box, ground_center, heading_radians, head);
  }
  append_box(mesh, {{-0.46f, 1.22f, 1.62f}, {0.46f, 1.56f, 2.20f}, white},
             ground_center, heading_radians, head);
  append_box(mesh, {{-0.34f, 1.10f, 1.72f}, {0.34f, 1.34f, 2.08f}, white},
             ground_center, heading_radians, head);
  append_box(mesh, {{-0.14f, 1.36f, 2.21f}, {0.14f, 1.54f, 2.38f}, black},
             ground_center, heading_radians, head);
  append_box(mesh, {{-0.48f, 1.64f, 1.70f}, {-0.25f, 1.82f, 1.88f}, black},
             ground_center, heading_radians, head);
  append_box(mesh, {{0.25f, 1.64f, 1.70f}, {0.48f, 1.82f, 1.88f}, black},
             ground_center, heading_radians, head);
  append_box(mesh, {{-0.68f, 1.88f, 1.02f}, {-0.28f, 2.45f, 1.42f}, dark_orange},
             ground_center, heading_radians, left_ear);
  append_box(mesh, {{0.28f, 1.88f, 1.02f}, {0.68f, 2.45f, 1.42f}, dark_orange},
             ground_center, heading_radians, right_ear);

  // Legs and paws. Diagonal pairs alternate: front-left/back-right, front-right/back-left.
  append_box(mesh, {{-0.94f, 0.16f, -0.76f}, {-0.62f, 0.62f, -0.42f}, dark_orange},
             ground_center, heading_radians, back_left_leg);
  append_box(mesh, {{-1.00f, 0.00f, -0.84f}, {-0.56f, 0.22f, -0.34f}, dark_orange},
             ground_center, heading_radians, back_left_leg);
  append_box(mesh, {{0.62f, 0.16f, -0.76f}, {0.94f, 0.62f, -0.42f}, dark_orange},
             ground_center, heading_radians, back_right_leg);
  append_box(mesh, {{0.56f, 0.00f, -0.84f}, {1.00f, 0.22f, -0.34f}, dark_orange},
             ground_center, heading_radians, back_right_leg);
  append_box(mesh, {{-0.94f, 0.16f, 0.48f}, {-0.62f, 0.62f, 0.82f}, dark_orange},
             ground_center, heading_radians, front_left_leg);
  append_box(mesh, {{-1.00f, 0.00f, 0.40f}, {-0.56f, 0.22f, 0.92f}, dark_orange},
             ground_center, heading_radians, front_left_leg);
  append_box(mesh, {{0.62f, 0.16f, 0.48f}, {0.94f, 0.62f, 0.82f}, dark_orange},
             ground_center, heading_radians, front_right_leg);
  append_box(mesh, {{0.56f, 0.00f, 0.40f}, {1.00f, 0.22f, 0.92f}, dark_orange},
             ground_center, heading_radians, front_right_leg);

  // Raised tail with white tip.
  append_box(mesh, {{-0.42f, 0.98f, -2.00f}, {0.42f, 1.50f, -0.96f}, dark_orange},
             ground_center, heading_radians, tail_base);
  append_box(mesh, {{-0.30f, 1.22f, -2.18f}, {0.30f, 1.70f, -1.36f}, orange},
             ground_center, heading_radians, tail_base);
  append_box(mesh, {{-0.36f, 1.18f, -2.82f}, {0.36f, 1.78f, -2.05f}, orange},
             ground_center, heading_radians, tail_mid);
  append_box(mesh, {{-0.25f, 1.42f, -3.02f}, {0.25f, 1.92f, -2.46f}, warm_orange},
             ground_center, heading_radians, tail_mid);
  append_box(mesh, {{-0.30f, 1.34f, -3.34f}, {0.30f, 1.76f, -2.86f}, white},
             ground_center, heading_radians, tail_tip);
  append_box(mesh, {{-0.20f, 1.44f, -3.54f}, {0.20f, 1.68f, -3.20f}, white},
             ground_center, heading_radians, tail_tip);
}

}  // namespace voxel
