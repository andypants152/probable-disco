#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include "core/app.h"
#include "core/audio.h"
#include "web_renderer.h"

namespace {

constexpr float kGamepadMoveDeadZone = 0.25f;
constexpr float kGamepadLookDeadZone = 0.12f;

struct Host {
  voxel::App app;
  voxel::WebRenderer renderer;
  voxel::CameraInput input;
  double last_time_ms = 0.0;
  float touch_move_x = 0.0f;
  float touch_move_y = 0.0f;
  float touch_look_x = 0.0f;
  float touch_look_y = 0.0f;
  bool touch_action_pressed = false;
  bool touch_action_held = false;
  bool touch_pause_pressed = false;
  bool touch_pause_held = false;
  bool gamepad_action_held = false;
  bool gamepad_pause_held = false;
  bool mouse_down = false;
};

Host g_host;

float clamp_axis(float value) {
  return std::max(-1.0f, std::min(1.0f, value));
}

float dominant_axis(float current, float candidate) {
  return std::abs(candidate) > std::abs(current) ? candidate : current;
}

bool gamepad_button_down(const EmscriptenGamepadEvent& pad, int index) {
  return index >= 0 &&
      index < pad.numButtons &&
      (pad.digitalButton[index] || pad.analogButton[index] > 0.5);
}

float gamepad_axis(const EmscriptenGamepadEvent& pad, int index) {
  if (index < 0 || index >= pad.numAxes) {
    return 0.0f;
  }
  const double value = pad.axis[index];
  return std::isfinite(value) ? static_cast<float>(value) : 0.0f;
}

void merge_gamepad_axis(float* axes, int index, float value) {
  axes[index] = dominant_axis(axes[index], value);
}

void merge_gamepad_button(int* buttons, int index, bool down) {
  if (down) {
    buttons[index] = 1;
  }
}

bool poll_web_gamepad(float* axes, int* buttons) {
  static bool sample_error_logged = false;
  static double no_gamepads_log_time_ms = -3000.0;
  static bool seen_gamepad[16] = {};

  const EMSCRIPTEN_RESULT sample_result = emscripten_sample_gamepad_data();
  if (sample_result != EMSCRIPTEN_RESULT_SUCCESS &&
      sample_result != EMSCRIPTEN_RESULT_DEFERRED &&
      !sample_error_logged) {
    std::printf("[probable-disco] emscripten_sample_gamepad_data failed: %d\n", sample_result);
    sample_error_logged = true;
  }

  const int gamepad_count = emscripten_get_num_gamepads();
  bool found = false;
  for (int i = 0; i < gamepad_count; ++i) {
    EmscriptenGamepadEvent pad = {};
    const EMSCRIPTEN_RESULT status_result = emscripten_get_gamepad_status(i, &pad);
    if (status_result != EMSCRIPTEN_RESULT_SUCCESS || !pad.connected) {
      continue;
    }

    found = true;
    if (i >= 0 && i < static_cast<int>(sizeof(seen_gamepad) / sizeof(seen_gamepad[0])) && !seen_gamepad[i]) {
      std::printf("[probable-disco] Emscripten gamepad detected index %d id \"%s\" mapping \"%s\" axes %d buttons %d\n",
                  pad.index,
                  pad.id,
                  pad.mapping,
                  pad.numAxes,
                  pad.numButtons);
      seen_gamepad[i] = true;
    }

    merge_gamepad_axis(axes, 0, gamepad_axis(pad, 0));
    merge_gamepad_axis(axes, 1, gamepad_axis(pad, 1));
    merge_gamepad_axis(axes, 2, gamepad_axis(pad, 2));
    merge_gamepad_axis(axes, 3, gamepad_axis(pad, 3));

    const float dpad_x = gamepad_axis(pad, 6);
    const float dpad_y = gamepad_axis(pad, 7);
    merge_gamepad_button(buttons, 0, gamepad_button_down(pad, 12) || dpad_y < -0.5f);
    merge_gamepad_button(buttons, 1, gamepad_button_down(pad, 13) || dpad_y > 0.5f);
    merge_gamepad_button(buttons, 2, gamepad_button_down(pad, 14) || dpad_x < -0.5f);
    merge_gamepad_button(buttons, 3, gamepad_button_down(pad, 15) || dpad_x > 0.5f);
    merge_gamepad_button(buttons, 4, gamepad_button_down(pad, 0) || gamepad_button_down(pad, 1));
    merge_gamepad_button(buttons, 5, gamepad_button_down(pad, 8) || gamepad_button_down(pad, 9));
  }

  if (!found) {
    const double now_ms = emscripten_get_now();
    if (now_ms - no_gamepads_log_time_ms >= 3000.0) {
      std::printf("[probable-disco] No usable Emscripten gamepads; reported count %d. "
                  "Focus the page and press a controller button to unlock browser gamepad input.\n",
                  gamepad_count);
      no_gamepads_log_time_ms = now_ms;
    }
  }
  return found;
}

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

  if (std::strcmp(event->code, "KeyW") == 0 || std::strcmp(event->code, "ArrowUp") == 0) {
    host->input.forward = down;
    return EM_TRUE;
  }
  if (std::strcmp(event->code, "KeyS") == 0 || std::strcmp(event->code, "ArrowDown") == 0) {
    host->input.back = down;
    return EM_TRUE;
  }
  if (std::strcmp(event->code, "KeyA") == 0 || std::strcmp(event->code, "ArrowLeft") == 0) {
    host->input.left = down;
    return EM_TRUE;
  }
  if (std::strcmp(event->code, "KeyD") == 0 || std::strcmp(event->code, "ArrowRight") == 0) {
    host->input.right = down;
    return EM_TRUE;
  }
  if (std::strcmp(event->code, "Space") == 0) {
    host->input.up = down;
    host->input.interact = down;
    host->input.action_held = down;
    host->input.action_pressed = down && event->repeat == 0;
    return EM_TRUE;
  }
  if (std::strcmp(event->code, "Enter") == 0 || std::strcmp(event->code, "KeyE") == 0) {
    host->input.interact = down;
    host->input.action_held = down;
    host->input.action_pressed = down && event->repeat == 0;
    return EM_TRUE;
  }
  if (std::strcmp(event->code, "ShiftLeft") == 0 || std::strcmp(event->code, "ShiftRight") == 0) {
    host->input.down = down;
    return EM_TRUE;
  }
  if (std::strcmp(event->code, "Escape") == 0) {
    host->input.pause_held = down;
    host->input.pause_pressed = down && event->repeat == 0;
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

EM_BOOL gamepad_event_callback(int event_type, const EmscriptenGamepadEvent* event, void*) {
  const char* name = event_type == EMSCRIPTEN_EVENT_GAMEPADCONNECTED
      ? "connected"
      : "disconnected";
  std::printf("[probable-disco] Emscripten gamepad %s index %d id \"%s\" mapping \"%s\" axes %d buttons %d\n",
              name,
              event->index,
              event->id,
              event->mapping,
              event->numAxes,
              event->numButtons);
  return EM_FALSE;
}

void apply_gamepad_input(Host& host, voxel::CameraInput& input) {
  float axes[4] = {};
  int buttons[6] = {};
  if (poll_web_gamepad(axes, buttons) == 0) {
    host.gamepad_action_held = false;
    host.gamepad_pause_held = false;
    return;
  }

  const float left_x = std::abs(axes[0]) >= kGamepadMoveDeadZone ? axes[0] : 0.0f;
  const float left_y = std::abs(axes[1]) >= kGamepadMoveDeadZone ? axes[1] : 0.0f;
  const float right_x = std::abs(axes[2]) >= kGamepadLookDeadZone ? axes[2] : 0.0f;
  const float right_y = std::abs(axes[3]) >= kGamepadLookDeadZone ? axes[3] : 0.0f;

  const bool gamepad_action = buttons[4] != 0;
  const bool gamepad_pause = buttons[5] != 0;

  input.forward = input.forward || buttons[0] != 0;
  input.back = input.back || buttons[1] != 0;
  input.left = input.left || buttons[2] != 0;
  input.right = input.right || buttons[3] != 0;
  input.forward = input.forward || left_y < 0.0f;
  input.back = input.back || left_y > 0.0f;
  input.left = input.left || left_x < 0.0f;
  input.right = input.right || left_x > 0.0f;
  input.action_held = input.action_held || gamepad_action;
  input.action_pressed = input.action_pressed || (gamepad_action && !host.gamepad_action_held);
  input.interact = input.interact || gamepad_action;
  input.pause_held = input.pause_held || gamepad_pause;
  input.pause_pressed = input.pause_pressed || (gamepad_pause && !host.gamepad_pause_held);
  host.gamepad_action_held = gamepad_action;
  host.gamepad_pause_held = gamepad_pause;

  input.move_x = dominant_axis(input.move_x, left_x);
  input.move_y = dominant_axis(input.move_y, left_y);
  input.look_x = dominant_axis(input.look_x, right_x);
  input.look_y = dominant_axis(input.look_y, right_y);
}

void apply_touch_input(const Host& host, voxel::CameraInput& input) {
  input.move_x = dominant_axis(input.move_x, host.touch_move_x);
  input.move_y = dominant_axis(input.move_y, host.touch_move_y);
  input.look_x = dominant_axis(input.look_x, host.touch_look_x);
  input.look_y = dominant_axis(input.look_y, host.touch_look_y);
  input.action_held = input.action_held || host.touch_action_held;
  input.action_pressed = input.action_pressed || host.touch_action_pressed;
  input.interact = input.interact || host.touch_action_pressed;
  input.pause_held = input.pause_held || host.touch_pause_held;
  input.pause_pressed = input.pause_pressed || host.touch_pause_pressed;
}

void frame(void* user_data) {
  auto* host = static_cast<Host*>(user_data);
  const double now = emscripten_get_now();
  const float dt = static_cast<float>(std::min(0.05, (now - host->last_time_ms) / 1000.0));
  host->last_time_ms = now;
  host->input.delta_time = dt;

  voxel::CameraInput frame_input = host->input;
  apply_touch_input(*host, frame_input);
  apply_gamepad_input(*host, frame_input);

  resize_canvas(*host);
  host->app.frame(host->renderer, frame_input);

  host->input.look_delta_x = 0.0f;
  host->input.look_delta_y = 0.0f;
  host->input.action_pressed = false;
  host->input.pause_pressed = false;
  host->touch_action_pressed = false;
  host->touch_pause_pressed = false;
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void game_set_touch_move(float x, float y) {
  g_host.touch_move_x = clamp_axis(x);
  g_host.touch_move_y = clamp_axis(y);
  if (std::abs(g_host.touch_move_x) > 0.001f || std::abs(g_host.touch_move_y) > 0.001f) {
    voxel::audio_resume();
  }
}

EMSCRIPTEN_KEEPALIVE void game_set_touch_look(float x, float y) {
  g_host.touch_look_x = clamp_axis(x);
  g_host.touch_look_y = clamp_axis(y);
  if (std::abs(g_host.touch_look_x) > 0.001f || std::abs(g_host.touch_look_y) > 0.001f) {
    voxel::audio_resume();
  }
}

EMSCRIPTEN_KEEPALIVE void game_set_touch_button(int button, int pressed) {
  const bool down = pressed != 0;
  if (down) {
    voxel::audio_resume();
  }

  if (button == 0) {
    g_host.touch_action_pressed = down && !g_host.touch_action_held;
    g_host.touch_action_held = down;
  } else if (button == 1) {
    g_host.touch_pause_pressed = down && !g_host.touch_pause_held;
    g_host.touch_pause_held = down;
  }
}

EMSCRIPTEN_KEEPALIVE void game_clear_touch_input() {
  g_host.touch_move_x = 0.0f;
  g_host.touch_move_y = 0.0f;
  g_host.touch_look_x = 0.0f;
  g_host.touch_look_y = 0.0f;
  g_host.touch_action_pressed = false;
  g_host.touch_action_held = false;
  g_host.touch_pause_pressed = false;
  g_host.touch_pause_held = false;
}

}

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
  emscripten_set_gamepadconnected_callback(nullptr, EM_FALSE, gamepad_event_callback);
  emscripten_set_gamepaddisconnected_callback(nullptr, EM_FALSE, gamepad_event_callback);

  g_host.last_time_ms = emscripten_get_now();
  emscripten_set_main_loop_arg(frame, &g_host, 0, EM_TRUE);
  return 0;
}
