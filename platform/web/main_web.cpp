#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include "app.h"
#include "audio.h"
#include "forest_audio.h"
#include "web_renderer.h"

namespace {

constexpr float kGamepadMoveDeadZone = 0.25f;
constexpr float kGamepadLookDeadZone = 0.12f;
constexpr float kGamepadLookPixelsPerSecond = 720.0f;

struct Host {
  voxel::App app;
  voxel::WebRenderer renderer;
  voxel::CameraInput input;
  double last_time_ms = 0.0;
  float debug_hum_volume = 0.0f;
  float debug_hum_pitch = 1.0f;
  bool mouse_down = false;
};

Host g_host;

EM_JS(int, poll_web_gamepad, (float* axes, int* buttons), {
  if (!navigator.getGamepads) {
    return 0;
  }

  var pads = navigator.getGamepads();
  for (var i = 0; i < pads.length; ++i) {
    var pad = pads[i];
    if (!pad || !pad.connected) {
      continue;
    }

    function axis(index) {
      if (!pad.axes || pad.axes.length <= index) {
        return 0;
      }
      var value = pad.axes[index] || 0;
      return Number.isFinite(value) ? value : 0;
    }

    function pressed(index) {
      if (!pad.buttons || pad.buttons.length <= index) {
        return false;
      }
      var button = pad.buttons[index];
      if (typeof button === "object") {
        return button.pressed || button.value > 0.5;
      }
      return button > 0.5;
    }

    HEAPF32[(axes >> 2) + 0] = axis(0);
    HEAPF32[(axes >> 2) + 1] = axis(1);
    HEAPF32[(axes >> 2) + 2] = axis(2);
    HEAPF32[(axes >> 2) + 3] = axis(3);

    HEAP32[(buttons >> 2) + 0] = pressed(12) ? 1 : 0;
    HEAP32[(buttons >> 2) + 1] = pressed(13) ? 1 : 0;
    HEAP32[(buttons >> 2) + 2] = pressed(14) ? 1 : 0;
    HEAP32[(buttons >> 2) + 3] = pressed(15) ? 1 : 0;
    return 1;
  }

  return 0;
});

void resize_canvas(Host& host) {
  double css_width = 0.0;
  double css_height = 0.0;
  emscripten_get_element_css_size("#canvas", &css_width, &css_height);

  const double pixel_ratio = emscripten_get_device_pixel_ratio();
  const int width = std::max(1, static_cast<int>(css_width * pixel_ratio));
  const int height = std::max(1, static_cast<int>(css_height * pixel_ratio));

  int current_width = 0;
  int current_height = 0;
  emscripten_get_canvas_element_size("#canvas", &current_width, &current_height);
  if (width != current_width || height != current_height) {
    emscripten_set_canvas_element_size("#canvas", width, height);
  }

  host.app.camera().aspect = static_cast<float>(width) / static_cast<float>(height);
}

EM_BOOL key_callback(int event_type, const EmscriptenKeyboardEvent* event, void* user_data) {
  auto* host = static_cast<Host*>(user_data);
  const bool down = event_type == EMSCRIPTEN_EVENT_KEYDOWN;
  if (down) {
    voxel::audio_resume();
  }

  if (std::strcmp(event->code, "KeyW") == 0) {
    host->input.forward = down;
    return EM_TRUE;
  }
  if (std::strcmp(event->code, "KeyS") == 0) {
    host->input.back = down;
    return EM_TRUE;
  }
  if (std::strcmp(event->code, "KeyA") == 0) {
    host->input.left = down;
    return EM_TRUE;
  }
  if (std::strcmp(event->code, "KeyD") == 0) {
    host->input.right = down;
    return EM_TRUE;
  }
  if (std::strcmp(event->code, "Space") == 0) {
    host->input.up = down;
    return EM_TRUE;
  }
  if (std::strcmp(event->code, "ShiftLeft") == 0 || std::strcmp(event->code, "ShiftRight") == 0) {
    host->input.down = down;
    return EM_TRUE;
  }
  if (down && !event->repeat && std::strcmp(event->code, "KeyM") == 0) {
    voxel::audio_play_mote_chime(0.85f);
    return EM_TRUE;
  }
  if (down && !event->repeat && std::strcmp(event->code, "KeyO") == 0) {
    voxel::audio_play_owl_appear();
    return EM_TRUE;
  }
  if (down && !event->repeat &&
      (std::strcmp(event->code, "Minus") == 0 || std::strcmp(event->code, "Digit9") == 0)) {
    host->debug_hum_volume = std::max(0.0f, host->debug_hum_volume - 0.015f);
    voxel::forest_audio_debug_override_hum(host->debug_hum_volume, host->debug_hum_pitch);
    return EM_TRUE;
  }
  if (down && !event->repeat &&
      (std::strcmp(event->code, "Equal") == 0 || std::strcmp(event->code, "Digit0") == 0)) {
    host->debug_hum_volume = std::min(0.18f, host->debug_hum_volume + 0.015f);
    voxel::forest_audio_debug_override_hum(host->debug_hum_volume, host->debug_hum_pitch);
    return EM_TRUE;
  }
  if (down && !event->repeat && std::strcmp(event->code, "BracketLeft") == 0) {
    host->debug_hum_pitch = std::max(0.65f, host->debug_hum_pitch - 0.08f);
    voxel::forest_audio_debug_override_hum(host->debug_hum_volume, host->debug_hum_pitch);
    return EM_TRUE;
  }
  if (down && !event->repeat && std::strcmp(event->code, "BracketRight") == 0) {
    host->debug_hum_pitch = std::min(1.60f, host->debug_hum_pitch + 0.08f);
    voxel::forest_audio_debug_override_hum(host->debug_hum_volume, host->debug_hum_pitch);
    return EM_TRUE;
  }

  return EM_FALSE;
}

EM_BOOL mouse_button_callback(int event_type, const EmscriptenMouseEvent*, void* user_data) {
  auto* host = static_cast<Host*>(user_data);
  host->mouse_down = event_type == EMSCRIPTEN_EVENT_MOUSEDOWN;
  if (host->mouse_down) {
    voxel::audio_resume();
    emscripten_request_pointerlock("#canvas", EM_TRUE);
  }
  return EM_TRUE;
}

EM_BOOL mouse_move_callback(int, const EmscriptenMouseEvent* event, void* user_data) {
  auto* host = static_cast<Host*>(user_data);
  if (host->mouse_down) {
    host->input.look_delta_x += static_cast<float>(event->movementX);
    host->input.look_delta_y += static_cast<float>(event->movementY);
  }
  return EM_TRUE;
}

void apply_gamepad_input(voxel::CameraInput& input) {
  float axes[4] = {};
  int buttons[4] = {};
  if (poll_web_gamepad(axes, buttons) == 0) {
    return;
  }

  const float left_x = std::abs(axes[0]) >= kGamepadMoveDeadZone ? axes[0] : 0.0f;
  const float left_y = std::abs(axes[1]) >= kGamepadMoveDeadZone ? axes[1] : 0.0f;
  const float right_x = std::abs(axes[2]) >= kGamepadLookDeadZone ? axes[2] : 0.0f;
  const float right_y = std::abs(axes[3]) >= kGamepadLookDeadZone ? axes[3] : 0.0f;

  input.forward = input.forward || left_y < 0.0f || buttons[0] != 0;
  input.back = input.back || left_y > 0.0f || buttons[1] != 0;
  input.left = input.left || left_x < 0.0f || buttons[2] != 0;
  input.right = input.right || left_x > 0.0f || buttons[3] != 0;

  input.look_delta_x += right_x * kGamepadLookPixelsPerSecond * input.delta_time;
  input.look_delta_y += right_y * kGamepadLookPixelsPerSecond * input.delta_time;
}

void frame(void* user_data) {
  auto* host = static_cast<Host*>(user_data);
  const double now = emscripten_get_now();
  const float dt = static_cast<float>(std::min(0.05, (now - host->last_time_ms) / 1000.0));
  host->last_time_ms = now;
  host->input.delta_time = dt;

  voxel::CameraInput frame_input = host->input;
  apply_gamepad_input(frame_input);

  resize_canvas(*host);
  host->app.frame(host->renderer, frame_input);

  host->input.look_delta_x = 0.0f;
  host->input.look_delta_y = 0.0f;
}

}  // namespace

int main() {
  resize_canvas(g_host);

  if (!g_host.app.init(g_host.renderer)) {
    std::printf("Failed to initialize voxel forest.\n");
    return 1;
  }

  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &g_host, EM_TRUE, key_callback);
  emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &g_host, EM_TRUE, key_callback);
  emscripten_set_mousedown_callback("#canvas", &g_host, EM_TRUE, mouse_button_callback);
  emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &g_host, EM_TRUE, mouse_button_callback);
  emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &g_host, EM_TRUE, mouse_move_callback);

  g_host.last_time_ms = emscripten_get_now();
  emscripten_set_main_loop_arg(frame, &g_host, 0, EM_TRUE);
  return 0;
}
