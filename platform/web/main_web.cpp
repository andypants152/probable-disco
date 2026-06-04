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
  bool gamepad_l_held = false;
  bool gamepad_r_held = false;
  bool gamepad_zl_held = false;
  bool gamepad_zr_held = false;
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
    HEAP32[(buttons >> 2) + 4] = (pressed(0) || pressed(1)) ? 1 : 0;
    HEAP32[(buttons >> 2) + 5] = (pressed(8) || pressed(9)) ? 1 : 0;
    HEAP32[(buttons >> 2) + 6] = pressed(4) ? 1 : 0;
    HEAP32[(buttons >> 2) + 7] = pressed(5) ? 1 : 0;
    HEAP32[(buttons >> 2) + 8] = pressed(6) ? 1 : 0;
    HEAP32[(buttons >> 2) + 9] = pressed(7) ? 1 : 0;
    return 1;
  }

  return 0;
});

float clamp_axis(float value) {
  return std::max(-1.0f, std::min(1.0f, value));
}

float dominant_axis(float current, float candidate) {
  return std::abs(candidate) > std::abs(current) ? candidate : current;
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

void apply_gamepad_input(Host& host, voxel::CameraInput& input) {
  float axes[4] = {};
  int buttons[10] = {};
  if (poll_web_gamepad(axes, buttons) == 0) {
    host.gamepad_action_held = false;
    host.gamepad_pause_held = false;
    host.gamepad_l_held = false;
    host.gamepad_r_held = false;
    host.gamepad_zl_held = false;
    host.gamepad_zr_held = false;
    return;
  }

  const float left_x = std::abs(axes[0]) >= kGamepadMoveDeadZone ? axes[0] : 0.0f;
  const float left_y = std::abs(axes[1]) >= kGamepadMoveDeadZone ? axes[1] : 0.0f;
  const float right_x = std::abs(axes[2]) >= kGamepadLookDeadZone ? axes[2] : 0.0f;
  const float right_y = std::abs(axes[3]) >= kGamepadLookDeadZone ? axes[3] : 0.0f;

  const bool gamepad_action = buttons[4] != 0;
  const bool gamepad_pause = buttons[5] != 0;
  const bool gamepad_l = buttons[6] != 0;
  const bool gamepad_r = buttons[7] != 0;
  const bool gamepad_zl = buttons[8] != 0;
  const bool gamepad_zr = buttons[9] != 0;

  if (gamepad_l && !host.gamepad_l_held) {
    host.app.set_gameplay_light_limit(host.app.gameplay_light_limit() - 1);
    std::printf("dev light limit %d\n", host.app.gameplay_light_limit());
  }
  if (gamepad_r && !host.gamepad_r_held) {
    host.app.set_gameplay_light_limit(host.app.gameplay_light_limit() + 1);
    std::printf("dev light limit %d\n", host.app.gameplay_light_limit());
  }
  if (gamepad_zl && !host.gamepad_zl_held) {
    host.app.dev_collect_active_fireflies();
    std::printf("dev collected active fireflies\n");
  }
  if (gamepad_zr && !host.gamepad_zr_held) {
    host.app.dev_deposit_carried_fireflies();
    std::printf("dev deposited carried fireflies\n");
  }

  input.forward = input.forward || buttons[0] != 0;
  input.back = input.back || buttons[1] != 0;
  input.left = input.left || buttons[2] != 0;
  input.right = input.right || buttons[3] != 0;
  input.action_held = input.action_held || gamepad_action;
  input.action_pressed = input.action_pressed || (gamepad_action && !host.gamepad_action_held);
  input.interact = input.interact || gamepad_action;
  input.pause_held = input.pause_held || gamepad_pause;
  input.pause_pressed = input.pause_pressed || (gamepad_pause && !host.gamepad_pause_held);
  host.gamepad_action_held = gamepad_action;
  host.gamepad_pause_held = gamepad_pause;
  host.gamepad_l_held = gamepad_l;
  host.gamepad_r_held = gamepad_r;
  host.gamepad_zl_held = gamepad_zl;
  host.gamepad_zr_held = gamepad_zr;

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

  g_host.last_time_ms = emscripten_get_now();
  emscripten_set_main_loop_arg(frame, &g_host, 0, EM_TRUE);
  return 0;
}
