#include "web_renderer.h"

#include <cstdio>
#include <vector>

#include "math/mat4.h"
#include "subtitles.h"

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
uniform vec4 u_light_position_radius[8];
uniform vec4 u_light_color_intensity[8];

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
  for (int i = 0; i < 4; ++i) {
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
  float fog = clamp(1.0 - exp(-0.020 * 0.020 * v_depth * v_depth), 0.0, 0.96);
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

uniform vec4 u_rect;

out vec2 v_tex_coord;

void main() {
  vec2 positions[6] = vec2[6](
    vec2(u_rect.x, u_rect.y),
    vec2(u_rect.z, u_rect.y),
    vec2(u_rect.z, u_rect.w),
    vec2(u_rect.x, u_rect.y),
    vec2(u_rect.z, u_rect.w),
    vec2(u_rect.x, u_rect.w)
  );
  vec2 tex_coords[6] = vec2[6](
    vec2(0.0, 1.0),
    vec2(1.0, 1.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(0.0, 0.0)
  );
  gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
  v_tex_coord = tex_coords[gl_VertexID];
}
)glsl";

constexpr const char* kOverlayFragmentShader = R"glsl(#version 300 es
precision highp float;

uniform sampler2D u_texture;
uniform float u_alpha;

in vec2 v_tex_coord;
out vec4 frag_color;

void main() {
  vec4 color = texture(u_texture, v_tex_coord);
  frag_color = vec4(color.rgb, color.a * u_alpha);
}
)glsl";

void decode_color(PackedColor packed, unsigned char* out) {
  out[0] = static_cast<unsigned char>((packed >> 24) & 0xffu);
  out[1] = static_cast<unsigned char>((packed >> 16) & 0xffu);
  out[2] = static_cast<unsigned char>((packed >> 8) & 0xffu);
  out[3] = static_cast<unsigned char>(packed & 0xffu);
}

}  // namespace

bool WebRenderer::init() {
  EmscriptenWebGLContextAttributes attributes;
  emscripten_webgl_init_context_attributes(&attributes);
  attributes.alpha = EM_FALSE;
  attributes.depth = EM_TRUE;
  attributes.stencil = EM_FALSE;
  attributes.antialias = EM_TRUE;
  attributes.majorVersion = 2;
  attributes.minorVersion = 0;

  context_ = emscripten_webgl_create_context("#canvas", &attributes);
  if (context_ <= 0) {
    std::printf("Failed to create WebGL2 context.\n");
    return false;
  }

  emscripten_webgl_make_context_current(context_);

  if (!create_program() || !create_overlay_program()) {
    return false;
  }

  glGenVertexArrays(1, &overlay_vao_);
  glGenTextures(1, &subtitle_texture_);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glClearColor(0.114f, 0.169f, 0.153f, 1.0f);
  return true;
}

void WebRenderer::upload_mesh(const Mesh& mesh) {
  emscripten_webgl_make_context_current(context_);
  destroy_buffers();
  upload_buffer(static_mesh_, mesh, GL_STATIC_DRAW);
}

void WebRenderer::upload_static_mesh(const Mesh& mesh) {
  emscripten_webgl_make_context_current(context_);
  destroy_buffer(static_mesh_);
  upload_buffer(static_mesh_, mesh, GL_STATIC_DRAW);
}

void WebRenderer::upload_dynamic_mesh(const Mesh& mesh) {
  emscripten_webgl_make_context_current(context_);
  destroy_buffer(dynamic_mesh_);
  upload_buffer(dynamic_mesh_, mesh, GL_DYNAMIC_DRAW);
}

void WebRenderer::upload_subtitle(const SubtitleFrame& subtitle) {
  if (context_ <= 0 || subtitle_texture_ == 0) {
    return;
  }

  subtitle_visible_ = subtitle.visible && subtitle.pixels != nullptr && subtitle.width > 0 && subtitle.height > 0;
  subtitle_alpha_ = subtitle.alpha;
  if (!subtitle_visible_) {
    subtitle_width_ = 0;
    subtitle_height_ = 0;
    subtitle_generation_ = subtitle.generation;
    return;
  }

  if (subtitle_generation_ == subtitle.generation) {
    return;
  }

  emscripten_webgl_make_context_current(context_);
  subtitle_generation_ = subtitle.generation;
  subtitle_width_ = subtitle.width;
  subtitle_height_ = subtitle.height;
  glBindTexture(GL_TEXTURE_2D, subtitle_texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D,
               0,
               GL_RGBA,
               subtitle_width_,
               subtitle_height_,
               0,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               subtitle.pixels);
}

void WebRenderer::upload_gameplay_lights(const GameplayLight* lights, int count) {
  light_count_ = 0;
  for (int i = 0; lights != nullptr && i < count && light_count_ < kMaxRendererGameplayLights; ++i) {
    if (!lights[i].active || lights[i].radius <= 0.0f || lights[i].intensity <= 0.0f) {
      continue;
    }
    const int base = light_count_ * 4;
    light_position_radius_[base + 0] = lights[i].position.x;
    light_position_radius_[base + 1] = lights[i].position.y;
    light_position_radius_[base + 2] = lights[i].position.z;
    light_position_radius_[base + 3] = lights[i].radius;
    light_color_intensity_[base + 0] = lights[i].color.x;
    light_color_intensity_[base + 1] = lights[i].color.y;
    light_color_intensity_[base + 2] = lights[i].color.z;
    light_color_intensity_[base + 3] = lights[i].intensity;
    ++light_count_;
  }
}

void WebRenderer::upload_buffer(MeshBuffer& buffer, const Mesh& mesh, GLenum usage) {
  if (mesh.indices.empty() || mesh.vertices.empty()) {
    buffer.index_count = 0;
    return;
  }

  std::vector<GpuVertex> gpu_vertices;
  gpu_vertices.reserve(mesh.vertices.size());

  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    const Vec3 position = mesh.vertices[i];
    const Vec3 normal = i < mesh.normals.size() ? mesh.normals[i] : Vec3{0.0f, 1.0f, 0.0f};
    const PackedColor color = i < mesh.colors.size() ? mesh.colors[i] : pack_rgba(255, 0, 255);

    GpuVertex vertex = {};
    vertex.position[0] = position.x;
    vertex.position[1] = position.y;
    vertex.position[2] = position.z;
    vertex.normal[0] = normal.x;
    vertex.normal[1] = normal.y;
    vertex.normal[2] = normal.z;
    decode_color(color, vertex.color);
    const Vec3 micro_position = i < mesh.micro_positions.size() ? mesh.micro_positions[i] : Vec3{0.0f, 0.0f, 0.0f};
    vertex.micro_position[0] = micro_position.x;
    vertex.micro_position[1] = micro_position.y;
    vertex.micro_position[2] = micro_position.z;
    gpu_vertices.push_back(vertex);
  }

  glGenVertexArrays(1, &buffer.vao);
  glBindVertexArray(buffer.vao);

  glGenBuffers(1, &buffer.vertex_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, buffer.vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(gpu_vertices.size() * sizeof(GpuVertex)),
               gpu_vertices.data(),
               usage);

  glGenBuffers(1, &buffer.index_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.index_buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(Index)),
               mesh.indices.data(),
               usage);

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

  buffer.index_count = static_cast<GLsizei>(mesh.indices.size());
  glBindVertexArray(0);
}

void WebRenderer::render_frame(const Camera& camera) {
  if (context_ <= 0 || program_ == 0) {
    return;
  }

  emscripten_webgl_make_context_current(context_);

  int width = 1;
  int height = 1;
  emscripten_get_canvas_element_size("#canvas", &width, &height);
  glViewport(0, 0, width, height);

  const Mat4 view = camera.view_matrix();
  const Mat4 projection = camera.projection_matrix();

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glUseProgram(program_);
  glUniformMatrix4fv(view_uniform_, 1, GL_FALSE, view.m);
  glUniformMatrix4fv(projection_uniform_, 1, GL_FALSE, projection.m);
  glUniform1i(light_count_uniform_, light_count_);
  glUniform4fv(light_position_radius_uniform_, kMaxRendererGameplayLights, light_position_radius_);
  glUniform4fv(light_color_intensity_uniform_, kMaxRendererGameplayLights, light_color_intensity_);

  draw_buffer(static_mesh_);
  draw_buffer(dynamic_mesh_);
  draw_subtitle(width, height);
  glBindVertexArray(0);
}

void WebRenderer::shutdown() {
  if (context_ > 0) {
    emscripten_webgl_make_context_current(context_);
  }

  destroy_buffers();

  if (program_ != 0) {
    glDeleteProgram(program_);
    program_ = 0;
  }
  if (overlay_program_ != 0) {
    glDeleteProgram(overlay_program_);
    overlay_program_ = 0;
  }
  if (subtitle_texture_ != 0) {
    glDeleteTextures(1, &subtitle_texture_);
    subtitle_texture_ = 0;
  }
  if (overlay_vao_ != 0) {
    glDeleteVertexArrays(1, &overlay_vao_);
    overlay_vao_ = 0;
  }

  if (context_ > 0) {
    emscripten_webgl_destroy_context(context_);
    context_ = 0;
  }
}

GLuint WebRenderer::compile_shader(GLenum type, const char* source) {
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

bool WebRenderer::create_program() {
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

bool WebRenderer::create_overlay_program() {
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
  return true;
}

void WebRenderer::draw_buffer(const MeshBuffer& buffer) {
  if (buffer.vao == 0 || buffer.index_count == 0) {
    return;
  }

  glBindVertexArray(buffer.vao);
  glDrawElements(GL_TRIANGLES, buffer.index_count, GL_UNSIGNED_INT, nullptr);
}

void WebRenderer::draw_subtitle(int framebuffer_width, int framebuffer_height) {
  if (!subtitle_visible_ || subtitle_texture_ == 0 || overlay_program_ == 0 || overlay_vao_ == 0 ||
      subtitle_width_ <= 0 || subtitle_height_ <= 0) {
    return;
  }

  const float max_width = static_cast<float>(framebuffer_width) * 0.84f;
  const float scale = subtitle_width_ > max_width ? max_width / static_cast<float>(subtitle_width_) : 1.0f;
  const float draw_width = static_cast<float>(subtitle_width_) * scale;
  const float draw_height = static_cast<float>(subtitle_height_) * scale;
  const float x = (static_cast<float>(framebuffer_width) - draw_width) * 0.5f;
  const float y = static_cast<float>(framebuffer_height) - draw_height - 42.0f;

  const float left = x / static_cast<float>(framebuffer_width) * 2.0f - 1.0f;
  const float right = (x + draw_width) / static_cast<float>(framebuffer_width) * 2.0f - 1.0f;
  const float top = 1.0f - y / static_cast<float>(framebuffer_height) * 2.0f;
  const float bottom = 1.0f - (y + draw_height) / static_cast<float>(framebuffer_height) * 2.0f;

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(overlay_program_);
  glBindVertexArray(overlay_vao_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, subtitle_texture_);
  glUniform1i(glGetUniformLocation(overlay_program_, "u_texture"), 0);
  glUniform1f(glGetUniformLocation(overlay_program_, "u_alpha"), subtitle_alpha_);
  glUniform4f(glGetUniformLocation(overlay_program_, "u_rect"), left, bottom, right, top);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

void WebRenderer::destroy_buffer(MeshBuffer& buffer) {
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
  buffer.index_count = 0;
}

void WebRenderer::destroy_buffers() {
  destroy_buffer(static_mesh_);
  destroy_buffer(dynamic_mesh_);
}

}  // namespace voxel
