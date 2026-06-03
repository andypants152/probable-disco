#include <switch.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.h"
#include "audio.h"
#include "switch_renderer.h"

namespace {

constexpr float kStickMax = 32767.0f;
constexpr float kMoveStickDeadZone = 0.24f;
constexpr float kLookStickDeadZone = 0.14f;
constexpr float kLookPixelsPerSecond = 720.0f;

float normalized_axis(s32 value) {
  float normalized = static_cast<float>(value) / kStickMax;
  if (normalized < -1.0f) {
    normalized = -1.0f;
  }
  if (normalized > 1.0f) {
    normalized = 1.0f;
  }
  return normalized;
}

float apply_dead_zone(float value, float dead_zone) {
  return std::fabs(value) >= dead_zone ? value : 0.0f;
}

void update_input(voxel::CameraInput& input, const PadState& pad) {
  input = {};
  input.delta_time = 1.0f / 60.0f;

  const u64 held = padGetButtons(&pad);
  const HidAnalogStickState left_stick = padGetStickPos(&pad, 0);
  const HidAnalogStickState right_stick = padGetStickPos(&pad, 1);
  const float left_x = apply_dead_zone(normalized_axis(left_stick.x), kMoveStickDeadZone);
  const float left_y = apply_dead_zone(normalized_axis(left_stick.y), kMoveStickDeadZone);
  const float right_x = apply_dead_zone(normalized_axis(right_stick.x), kLookStickDeadZone);
  const float right_y = apply_dead_zone(normalized_axis(right_stick.y), kLookStickDeadZone);

  input.forward = (held & HidNpadButton_Up) != 0;
  input.back = (held & HidNpadButton_Down) != 0;
  input.left = (held & HidNpadButton_Left) != 0;
  input.right = (held & HidNpadButton_Right) != 0;
  input.up = (held & HidNpadButton_A) != 0;
  input.down = (held & HidNpadButton_B) != 0;

  input.forward = input.forward || left_y > 0.0f;
  input.back = input.back || left_y < 0.0f;
  input.left = input.left || left_x < 0.0f;
  input.right = input.right || left_x > 0.0f;

  input.look_delta_x += right_x * kLookPixelsPerSecond * input.delta_time;
  input.look_delta_y -= right_y * kLookPixelsPerSecond * input.delta_time;

  if ((held & HidNpadButton_L) != 0) {
    input.look_delta_x -= 4.0f;
  }
  if ((held & HidNpadButton_R) != 0) {
    input.look_delta_x += 4.0f;
  }
  if ((held & HidNpadButton_ZL) != 0) {
    input.look_delta_y -= 4.0f;
  }
  if ((held & HidNpadButton_ZR) != 0) {
    input.look_delta_y += 4.0f;
  }
}

#if defined(VOXEL_SWITCH_TIMING)
bool timing_output_initialized = false;

void init_timing_output() {
  if (R_SUCCEEDED(socketInitializeDefault())) {
    timing_output_initialized = true;
    nxlinkStdio();
  }
}

void shutdown_timing_output() {
  if (timing_output_initialized) {
    socketExit();
    timing_output_initialized = false;
  }
}

double ns_to_ms(u64 ns) {
  return static_cast<double>(ns) / 1000000.0;
}
#endif

}  // namespace

int main(int, char**) {
#if defined(VOXEL_SWITCH_TIMING)
  init_timing_output();
#endif

  padConfigureInput(1, HidNpadStyleSet_NpadStandard);

  PadState pad;
  padInitializeDefault(&pad);

  voxel::SwitchRenderer renderer;
  voxel::App app;
  if (!app.init(renderer)) {
    svcSleepThread(2'000'000'000);
#if defined(VOXEL_SWITCH_TIMING)
    shutdown_timing_output();
#endif
    return 1;
  }

  while (appletMainLoop()) {
    padUpdate(&pad);
    const u64 down = padGetButtonsDown(&pad);
    if ((down & HidNpadButton_Plus) != 0) {
      break;
    }
    voxel::CameraInput input;
    update_input(input, pad);
    input.interact = (down & HidNpadButton_A) != 0;
    app.frame(renderer, input);
#if defined(VOXEL_SWITCH_TIMING)
    static int profile_frame = 0;
    if ((++profile_frame % 60) == 0) {
      const auto& app_stats = app.frame_stats();
      const auto& stats = renderer.frame_stats();
      std::printf("frame %.2fms update %.2fms rebuild world %.2fms fox %.2fms scene %.2fms upload %.2fms render %.2fms\n",
                  ns_to_ms(app_stats.total_ns),
                  ns_to_ms(app_stats.update_ns),
                  ns_to_ms(app_stats.world_rebuild_ns),
                  ns_to_ms(app_stats.fox_rebuild_ns),
                  ns_to_ms(app_stats.scene_rebuild_ns),
                  ns_to_ms(app_stats.upload_ns),
                  ns_to_ms(app_stats.render_ns));
      std::printf("render %.2fms wait %.2fms acquire %.2fms record %.2fms clear %.2fms draw %.2fms present %.2fms upload static %.2fms dyn %.2fms\n",
                  ns_to_ms(stats.total_ns),
                  ns_to_ms(stats.wait_ns),
                  ns_to_ms(stats.acquire_ns),
                  ns_to_ms(stats.command_record_ns),
                  ns_to_ms(stats.clear_ns),
                  ns_to_ms(stats.draw_ns),
                  ns_to_ms(stats.present_ns),
                  ns_to_ms(stats.static_upload_ns),
                  ns_to_ms(stats.dynamic_upload_ns));
      std::printf("verts %zu tris %zu\n", stats.vertices, stats.triangles);
      std::printf("loop carried %d lantern %d deposit %d/%d fireflies %d lights %d objective %.2f glow %.2f lantern %.2f radius %.2f\n",
                  app_stats.carried_fireflies,
                  app_stats.active_lantern_index,
                  app_stats.deposited_fireflies,
                  app_stats.required_fireflies,
                  app_stats.active_fireflies,
                  app_stats.active_gameplay_lights,
                  app_stats.distance_to_objective,
                  app_stats.firefly_glow_intensity,
                  app_stats.lantern_light_intensity,
                  app_stats.lantern_light_radius);
    }
#endif
  }

  app.shutdown(renderer);
#if defined(VOXEL_SWITCH_TIMING)
  shutdown_timing_output();
#endif
  return 0;
}
