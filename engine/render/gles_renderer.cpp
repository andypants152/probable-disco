#include "gles_renderer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "math/mat4.h"
#include "render/render_types.h"
#include "core/subtitles.h"

namespace voxel {

namespace {

constexpr const char* kVertexShader = R"glsl(#version 300 es
precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec3 a_micro_position;

uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 v_normal;
out vec4 v_color;
out vec3 v_world_position;
out vec3 v_micro_position;
out float v_depth;

void main() {
  vec4 view_position = u_view * vec4(a_position, 1.0);
  gl_Position = u_projection * view_position;
  v_normal = a_normal;
  v_color = a_color;
  v_world_position = a_position;
  v_micro_position = a_micro_position;
  v_depth = -view_position.z;
}
)glsl";

constexpr const char* kFragmentShader = R"glsl(#version 300 es
precision highp float;

in vec3 v_normal;
in vec4 v_color;
in vec3 v_world_position;
in vec3 v_micro_position;
in float v_depth;

uniform int u_light_count;
uniform vec4 u_light_position_radius[6];
uniform vec4 u_light_color_intensity[6];

out vec4 frag_color;

float cubeEdge(float a, float b) {
  float width = 0.13;
  return max(step(0.5 - width, abs(a)), step(0.5 - width, abs(b)));
}

void main() {
  vec3 normal = normalize(v_normal);
  vec3 normal_axis = abs(normal);
  vec2 micro_uv = normal_axis.x > normal_axis.y && normal_axis.x > normal_axis.z
    ? v_micro_position.yz
    : normal_axis.y > normal_axis.z
      ? v_micro_position.xz
      : v_micro_position.xy;
  float edge = cubeEdge(micro_uv.x, micro_uv.y);

  vec3 moon_sky = vec3(0.894, 1.0, 0.969);
  vec3 moon_ground = vec3(0.314, 0.384, 0.267);
  vec3 glow_color = vec3(0.949, 1.0, 0.973);
  vec3 glow_dir = normalize(vec3(-0.40, 0.80, -0.30));
  float hemi = normal.y * 0.5 + 0.5;
  float glow = max(dot(normal, glow_dir), 0.0);
  vec3 light = mix(moon_ground, moon_sky, hemi) * 0.38 + glow_color * glow * 0.24 + vec3(0.035, 0.045, 0.040);
  vec3 local_light = vec3(0.0);
  for (int i = 0; i < 6; ++i) {
    if (i >= u_light_count) {
      break;
    }
    vec4 light_pos_radius = u_light_position_radius[i];
    vec4 light_color_intensity = u_light_color_intensity[i];
    vec3 light_delta = v_world_position - light_pos_radius.xyz;
    float radius = max(light_pos_radius.w, 0.001);
    float falloff = clamp(1.0 - dot(light_delta, light_delta) / (radius * radius), 0.0, 1.0);
    falloff *= falloff;
    local_light += light_color_intensity.rgb * (falloff * light_color_intensity.w * 1.65);
  }
  vec3 outline = vec3(0.015, 0.020, 0.018);
  vec3 fog_color = vec3(0.114, 0.169, 0.153);
  float fog_depth = v_depth * 0.042;
  float fog = clamp((fog_depth * fog_depth) / (1.0 + fog_depth * fog_depth), 0.0, 0.985);
  float emissive = clamp(1.0 - v_color.a, 0.0, 1.0);
  vec3 local_visible = local_light * (1.0 - emissive * 0.70);
  vec3 fill = v_color.rgb * light + v_color.rgb * local_visible * 1.15 + local_visible * 0.16;
  vec3 emissive_color = mix(v_color.rgb, min(v_color.rgb * 1.25, vec3(1.0)), emissive);
  vec3 outlined = mix(fill, outline, edge * (1.0 - emissive * 0.78));
  vec3 lit_color = mix(outlined, emissive_color, emissive);
  float local_strength = clamp(max(max(local_light.r, local_light.g), local_light.b), 0.0, 1.0);
  frag_color = vec4(mix(lit_color, fog_color, fog * (1.0 - emissive * 0.72) * (1.0 - local_strength * 0.35)), 1.0);
}
)glsl";

constexpr const char* kOverlayVertexShader = R"glsl(#version 300 es
precision highp float;

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_tex_coord;
layout(location = 2) in vec4 a_color;

uniform vec2 u_viewport_size;

out vec2 v_tex_coord;
out vec4 v_color;

void main() {
  vec2 clip = vec2(a_position.x / u_viewport_size.x * 2.0 - 1.0,
                   1.0 - a_position.y / u_viewport_size.y * 2.0);
  gl_Position = vec4(clip, 0.0, 1.0);
  v_tex_coord = a_tex_coord;
  v_color = a_color;
}
)glsl";

constexpr const char* kOverlayFragmentShader = R"glsl(#version 300 es
precision highp float;

uniform sampler2D u_texture;

in vec2 v_tex_coord;
in vec4 v_color;
out vec4 frag_color;

void main() {
  vec4 sample_color = texture(u_texture, v_tex_coord);
  frag_color = vec4(v_color.rgb, v_color.a * sample_color.a);
}
)glsl";

void decode_color(PackedColor packed, unsigned char* out) {
  out[0] = static_cast<unsigned char>((packed >> 24) & 0xffu);
  out[1] = static_cast<unsigned char>((packed >> 16) & 0xffu);
  out[2] = static_cast<unsigned char>((packed >> 8) & 0xffu);
  out[3] = static_cast<unsigned char>(packed & 0xffu);
}

using Clock = std::chrono::steady_clock;

std::uint64_t elapsed_ns(Clock::time_point start, Clock::time_point end) {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

}  // namespace

bool GlesRenderer::init() {
  if (!create_context()) {
    return false;
  }
  make_context_current();

  if (!create_program() || !create_overlay_program()) {
    return false;
  }

  create_font_texture();
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glClearColor(0.114f, 0.169f, 0.153f, 1.0f);
  return true;
}

void GlesRenderer::begin_frame_stats() {
  frame_stats_ = {};
}

void GlesRenderer::upload_mesh(const Mesh& mesh) {
  make_context_current();
  const auto upload_start = Clock::now();
  upload_buffer(static_mesh_, mesh, GL_STATIC_DRAW);
  dynamic_mesh_.index_count = 0;
  dynamic_mesh_.vertex_count = 0;
  frame_stats_.static_upload_ns = elapsed_ns(upload_start, Clock::now());
}

void GlesRenderer::upload_static_mesh(const Mesh& mesh) {
  make_context_current();
  const auto upload_start = Clock::now();
  upload_buffer(static_mesh_, mesh, GL_STATIC_DRAW);
  frame_stats_.static_upload_ns = elapsed_ns(upload_start, Clock::now());
}

void GlesRenderer::upload_dynamic_mesh(const Mesh& mesh) {
  make_context_current();
  const auto upload_start = Clock::now();
  upload_buffer(dynamic_mesh_, mesh, GL_DYNAMIC_DRAW);
  frame_stats_.dynamic_upload_ns = elapsed_ns(upload_start, Clock::now());
}

void GlesRenderer::upload_buffer(MeshBuffer& buffer, const Mesh& mesh, GLenum usage) {
  if (mesh.indices.empty() || mesh.vertices.empty()) {
    buffer.index_count = 0;
    buffer.vertex_count = 0;
    return;
  }

  mesh_vertices_.clear();
  mesh_vertices_.reserve(mesh.vertices.size());

  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    const Vec3 position = mesh.vertices[i];
    const Vec3 normal = i < mesh.normals.size() ? mesh.normals[i] : Vec3{0.0f, 1.0f, 0.0f};
    const PackedColor color = i < mesh.colors.size() ? mesh.colors[i] : pack_rgba(255, 0, 255);
    const Vec3 micro_position = i < mesh.micro_positions.size() ? mesh.micro_positions[i] : Vec3{0.0f, 0.0f, 0.0f};

    GpuVertex vertex = {};
    vertex.position[0] = position.x;
    vertex.position[1] = position.y;
    vertex.position[2] = position.z;
    vertex.normal[0] = normal.x;
    vertex.normal[1] = normal.y;
    vertex.normal[2] = normal.z;
    decode_color(color, vertex.color);
    vertex.micro_position[0] = micro_position.x;
    vertex.micro_position[1] = micro_position.y;
    vertex.micro_position[2] = micro_position.z;
    mesh_vertices_.push_back(vertex);
  }

  ensure_mesh_buffer(buffer);

  const GLsizeiptr vertex_bytes = static_cast<GLsizeiptr>(mesh_vertices_.size() * sizeof(GpuVertex));
  const GLsizeiptr index_bytes = static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(Index));
  glBindVertexArray(buffer.vao);
  glBindBuffer(GL_ARRAY_BUFFER, buffer.vertex_buffer);
  if (vertex_bytes > buffer.vertex_capacity_bytes) {
    glBufferData(GL_ARRAY_BUFFER, vertex_bytes, mesh_vertices_.data(), usage);
    buffer.vertex_capacity_bytes = vertex_bytes;
  } else {
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, mesh_vertices_.data());
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.index_buffer);
  if (index_bytes > buffer.index_capacity_bytes) {
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_bytes, mesh.indices.data(), usage);
    buffer.index_capacity_bytes = index_bytes;
  } else {
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes, mesh.indices.data());
  }

  buffer.index_count = static_cast<GLsizei>(mesh.indices.size());
  buffer.vertex_count = mesh.vertices.size();
  glBindVertexArray(0);
}

void GlesRenderer::ensure_mesh_buffer(MeshBuffer& buffer) {
  if (buffer.vao != 0) {
    return;
  }

  glGenVertexArrays(1, &buffer.vao);
  glGenBuffers(1, &buffer.vertex_buffer);
  glGenBuffers(1, &buffer.index_buffer);

  glBindVertexArray(buffer.vao);
  glBindBuffer(GL_ARRAY_BUFFER, buffer.vertex_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.index_buffer);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                        reinterpret_cast<void*>(offsetof(GpuVertex, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                        reinterpret_cast<void*>(offsetof(GpuVertex, normal)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GpuVertex),
                        reinterpret_cast<void*>(offsetof(GpuVertex, color)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                        reinterpret_cast<void*>(offsetof(GpuVertex, micro_position)));
  glBindVertexArray(0);
}

void GlesRenderer::render_frame(const RenderFrame& frame) {
  if (program_ == 0) {
    return;
  }

  const auto frame_start = Clock::now();
  make_context_current();

  const auto acquire_start = Clock::now();
  int width = 1;
  int height = 1;
  framebuffer_size(width, height);
  width = std::max(1, width);
  height = std::max(1, height);
  glViewport(0, 0, width, height);
  frame_stats_.acquire_ns = elapsed_ns(acquire_start, Clock::now());

  light_count_ = 0;
  for (int i = 0; i < frame.light_count && light_count_ < kMaxRendererGameplayLights; ++i) {
    const GameplayLight& light = frame.lights[static_cast<std::size_t>(i)];
    if (!light.active || light.radius <= 0.0f || light.intensity <= 0.0f) {
      continue;
    }
    const int base = light_count_ * 4;
    light_position_radius_[base + 0] = light.position.x;
    light_position_radius_[base + 1] = light.position.y;
    light_position_radius_[base + 2] = light.position.z;
    light_position_radius_[base + 3] = light.radius;
    light_color_intensity_[base + 0] = light.color.x;
    light_color_intensity_[base + 1] = light.color.y;
    light_color_intensity_[base + 2] = light.color.z;
    light_color_intensity_[base + 3] = light.intensity;
    ++light_count_;
  }

  const Mat4 view = frame.camera.view_matrix();
  const Mat4 projection = frame.camera.projection_matrix();

  const auto clear_start = Clock::now();
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  frame_stats_.clear_ns = elapsed_ns(clear_start, Clock::now());

  const auto command_start = Clock::now();
  glUseProgram(program_);
  glUniformMatrix4fv(view_uniform_, 1, GL_FALSE, view.m);
  glUniformMatrix4fv(projection_uniform_, 1, GL_FALSE, projection.m);
  glUniform1i(light_count_uniform_, light_count_);
  glUniform4fv(light_position_radius_uniform_, kMaxRendererGameplayLights, light_position_radius_);
  glUniform4fv(light_color_intensity_uniform_, kMaxRendererGameplayLights, light_color_intensity_);
  frame_stats_.command_record_ns = elapsed_ns(command_start, Clock::now());

  const auto draw_start = Clock::now();
  frame_stats_.vertices = static_mesh_.vertex_count + dynamic_mesh_.vertex_count;
  frame_stats_.triangles =
      static_cast<std::size_t>(static_mesh_.index_count + dynamic_mesh_.index_count) / 3u;
  for (const RenderCommand& command : frame.commands) {
    switch (command.type) {
      case RenderCommandType::DrawStaticMesh:
        draw_buffer(static_mesh_);
        break;
      case RenderCommandType::DrawDynamicMesh:
        draw_buffer(dynamic_mesh_);
        break;
      case RenderCommandType::DrawSubtitle:
        overlay_batch_.clear();
        if (frame.subtitle != nullptr) {
          append_subtitle_batch(overlay_batch_, font_atlas_, *frame.subtitle, width, height);
        }
        if (frame.hud != nullptr) {
          append_subtitle_batch(overlay_batch_, font_atlas_, *frame.hud, width, height);
        }
        if (frame.fps != nullptr) {
          append_subtitle_batch(overlay_batch_, font_atlas_, *frame.fps, width, height);
        }
        if (!overlay_batch_.empty()) {
          upload_overlay_batch(overlay_batch_);
          draw_overlay_batch(width, height);
        }
        break;
    }
  }

  glBindVertexArray(0);
  frame_stats_.draw_ns = elapsed_ns(draw_start, Clock::now());

  const auto present_start = Clock::now();
  present();
  frame_stats_.present_ns = elapsed_ns(present_start, Clock::now());
  frame_stats_.total_ns = elapsed_ns(frame_start, Clock::now());
}

void GlesRenderer::shutdown() {
  make_context_current();
  destroy_gl_resources();
  destroy_context();
}

GLuint GlesRenderer::compile_shader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok != GL_TRUE) {
    char log[1024] = {};
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    std::printf("Shader compile failed: %s\n", log);
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

bool GlesRenderer::create_program() {
  const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, kVertexShader);
  const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, kFragmentShader);
  if (vertex_shader == 0 || fragment_shader == 0) {
    return false;
  }

  program_ = glCreateProgram();
  glAttachShader(program_, vertex_shader);
  glAttachShader(program_, fragment_shader);
  glLinkProgram(program_);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  GLint ok = GL_FALSE;
  glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (ok != GL_TRUE) {
    char log[1024] = {};
    glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
    std::printf("Program link failed: %s\n", log);
    glDeleteProgram(program_);
    program_ = 0;
    return false;
  }

  view_uniform_ = glGetUniformLocation(program_, "u_view");
  projection_uniform_ = glGetUniformLocation(program_, "u_projection");
  light_count_uniform_ = glGetUniformLocation(program_, "u_light_count");
  light_position_radius_uniform_ = glGetUniformLocation(program_, "u_light_position_radius");
  light_color_intensity_uniform_ = glGetUniformLocation(program_, "u_light_color_intensity");
  return true;
}

bool GlesRenderer::create_overlay_program() {
  const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, kOverlayVertexShader);
  const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, kOverlayFragmentShader);
  if (vertex_shader == 0 || fragment_shader == 0) {
    return false;
  }

  overlay_program_ = glCreateProgram();
  glAttachShader(overlay_program_, vertex_shader);
  glAttachShader(overlay_program_, fragment_shader);
  glLinkProgram(overlay_program_);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  GLint ok = GL_FALSE;
  glGetProgramiv(overlay_program_, GL_LINK_STATUS, &ok);
  if (ok != GL_TRUE) {
    char log[1024] = {};
    glGetProgramInfoLog(overlay_program_, sizeof(log), nullptr, log);
    std::printf("Overlay program link failed: %s\n", log);
    glDeleteProgram(overlay_program_);
    overlay_program_ = 0;
    return false;
  }

  overlay_viewport_uniform_ = glGetUniformLocation(overlay_program_, "u_viewport_size");
  overlay_texture_uniform_ = glGetUniformLocation(overlay_program_, "u_texture");
  return true;
}

void GlesRenderer::create_font_texture() {
  font_atlas_.build();
  glGenTextures(1, &font_texture_);
  glBindTexture(GL_TEXTURE_2D, font_texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D,
               0,
               GL_RGBA,
               font_atlas_.width(),
               font_atlas_.height(),
               0,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               font_atlas_.pixels().data());
}

void GlesRenderer::upload_overlay_batch(const QuadBatch& batch) {
  if (batch.empty()) {
    overlay_index_count_ = 0;
    return;
  }

  overlay_vertices_.clear();
  overlay_vertices_.reserve(batch.vertices().size());
  for (const QuadVertex& source : batch.vertices()) {
    OverlayVertex vertex = {};
    vertex.position[0] = source.position[0];
    vertex.position[1] = source.position[1];
    vertex.tex_coord[0] = source.tex_coord[0];
    vertex.tex_coord[1] = source.tex_coord[1];
    decode_color(source.color, vertex.color);
    overlay_vertices_.push_back(vertex);
  }

  ensure_overlay_buffer();

  const GLsizeiptr vertex_bytes = static_cast<GLsizeiptr>(overlay_vertices_.size() * sizeof(OverlayVertex));
  const GLsizeiptr index_bytes = static_cast<GLsizeiptr>(batch.indices().size() * sizeof(Index));
  glBindVertexArray(overlay_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, overlay_vertex_buffer_);
  if (vertex_bytes > overlay_vertex_capacity_bytes_) {
    glBufferData(GL_ARRAY_BUFFER, vertex_bytes, overlay_vertices_.data(), GL_DYNAMIC_DRAW);
    overlay_vertex_capacity_bytes_ = vertex_bytes;
  } else {
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, overlay_vertices_.data());
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, overlay_index_buffer_);
  if (index_bytes > overlay_index_capacity_bytes_) {
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_bytes, batch.indices().data(), GL_DYNAMIC_DRAW);
    overlay_index_capacity_bytes_ = index_bytes;
  } else {
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes, batch.indices().data());
  }

  overlay_index_count_ = static_cast<GLsizei>(batch.indices().size());
  glBindVertexArray(0);
}

void GlesRenderer::ensure_overlay_buffer() {
  if (overlay_vao_ != 0) {
    return;
  }

  glGenVertexArrays(1, &overlay_vao_);
  glGenBuffers(1, &overlay_vertex_buffer_);
  glGenBuffers(1, &overlay_index_buffer_);

  glBindVertexArray(overlay_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, overlay_vertex_buffer_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, overlay_index_buffer_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex),
                        reinterpret_cast<void*>(offsetof(OverlayVertex, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex),
                        reinterpret_cast<void*>(offsetof(OverlayVertex, tex_coord)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(OverlayVertex),
                        reinterpret_cast<void*>(offsetof(OverlayVertex, color)));
  glBindVertexArray(0);
}

void GlesRenderer::draw_buffer(const MeshBuffer& buffer) {
  if (buffer.vao == 0 || buffer.index_count == 0) {
    return;
  }

  glBindVertexArray(buffer.vao);
  glDrawElements(GL_TRIANGLES, buffer.index_count, GL_UNSIGNED_INT, nullptr);
}

void GlesRenderer::draw_overlay_batch(int framebuffer_width, int framebuffer_height) {
  if (overlay_vao_ == 0 || overlay_index_count_ == 0 || overlay_program_ == 0 || font_texture_ == 0) {
    return;
  }

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(overlay_program_);
  glUniform2f(overlay_viewport_uniform_, static_cast<float>(framebuffer_width), static_cast<float>(framebuffer_height));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, font_texture_);
  glUniform1i(overlay_texture_uniform_, 0);
  glBindVertexArray(overlay_vao_);
  glDrawElements(GL_TRIANGLES, overlay_index_count_, GL_UNSIGNED_INT, nullptr);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

void GlesRenderer::destroy_buffer(MeshBuffer& buffer) {
  if (buffer.index_buffer != 0) {
    glDeleteBuffers(1, &buffer.index_buffer);
    buffer.index_buffer = 0;
  }
  if (buffer.vertex_buffer != 0) {
    glDeleteBuffers(1, &buffer.vertex_buffer);
    buffer.vertex_buffer = 0;
  }
  if (buffer.vao != 0) {
    glDeleteVertexArrays(1, &buffer.vao);
    buffer.vao = 0;
  }
  buffer.vertex_capacity_bytes = 0;
  buffer.index_capacity_bytes = 0;
  buffer.index_count = 0;
  buffer.vertex_count = 0;
}

void GlesRenderer::destroy_buffers() {
  destroy_buffer(static_mesh_);
  destroy_buffer(dynamic_mesh_);
}

void GlesRenderer::destroy_gl_resources() {
  destroy_buffers();
  if (program_ != 0) {
    glDeleteProgram(program_);
    program_ = 0;
  }
  if (overlay_program_ != 0) {
    glDeleteProgram(overlay_program_);
    overlay_program_ = 0;
  }
  if (font_texture_ != 0) {
    glDeleteTextures(1, &font_texture_);
    font_texture_ = 0;
  }
  if (overlay_index_buffer_ != 0) {
    glDeleteBuffers(1, &overlay_index_buffer_);
    overlay_index_buffer_ = 0;
  }
  if (overlay_vertex_buffer_ != 0) {
    glDeleteBuffers(1, &overlay_vertex_buffer_);
    overlay_vertex_buffer_ = 0;
  }
  if (overlay_vao_ != 0) {
    glDeleteVertexArrays(1, &overlay_vao_);
    overlay_vao_ = 0;
  }
  overlay_vertex_capacity_bytes_ = 0;
  overlay_index_capacity_bytes_ = 0;
  overlay_index_count_ = 0;
}

}  // namespace voxel
