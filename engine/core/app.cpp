#include "app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "audio.h"
#include "forest_audio.h"
#include "game/fox.h"
#include "subtitles.h"
#include "world/mesher.h"

namespace voxel {

namespace {

constexpr float kNormalizedLookPixelsPerSecond = 1800.0f;
constexpr float kCameraDistance = 13.0f;
constexpr float kCameraTargetHeight = 1.35f;
constexpr float kSquirrelAnimationUploadInterval = 0.10f;
constexpr float kGameplayAnimationUploadInterval = 0.05f;
constexpr int kSquirrelsPerOwlReturn = 5;
constexpr float kOwlReturnClearRadius = 18.0f;
constexpr float kOwlReturnClearBuffer = 4.0f;
constexpr std::uint32_t kOwlSpeakerId = 0x0f11u;

using Clock = std::chrono::steady_clock;

std::uint64_t elapsed_ns(Clock::time_point start, Clock::time_point end) {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

float clamped_axis(float value) {
  return std::max(-1.0f, std::min(1.0f, value));
}

float horizontal_distance(Vec3 a, Vec3 b) {
  const float dx = b.x - a.x;
  const float dz = b.z - a.z;
  return std::sqrt(dx * dx + dz * dz);
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

std::uint32_t hash_u32(std::uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return value;
}

const char* owl_return_line_for_squirrel_count(int squirrel_count) {
  if (squirrel_count == 5) {
    return "You are farther than you think.";
  }
  if (squirrel_count == 10) {
    return "Endless things teach slowly.";
  }
  if (squirrel_count == 15) {
    return "A path is not always underfoot.";
  }
  if (squirrel_count == 20) {
    return "The dark remembers your light.";
  }

  constexpr std::array<const char*, 6> kLines = {{
      "Do not hurry the moon.",
      "The woods are listening.",
      "Again, little fox.",
      "You are not lost the same way.",
      "Some lights are for being found.",
      "The forest has begun to recognize you.",
  }};
  const std::uint32_t bucket = static_cast<std::uint32_t>(std::max(0, squirrel_count / kSquirrelsPerOwlReturn));
  return kLines[static_cast<std::size_t>(hash_u32(bucket ^ 0x0f11f1b5u) % kLines.size())];
}

}  // namespace

bool App::init(Renderer& renderer) {
  fox_controller_.init(generator_);
  camera_.yaw = -1.57079632679f;
  camera_.pitch = -0.38f;
  update_camera({});

  terrain_streamer_.init(generator_, fox_controller_.position());
  rebuild_fox_mesh();
  owl_encounter_.init(generator_);
  firefly_loop_.init(generator_);
  squirrel_quest_.init(generator_, firefly_loop_);
  rabbit_burrows_.init(generator_, firefly_loop_, squirrel_quest_);
  lantern_hud_revealed_ = false;
  squirrel_hud_revealed_ = false;
  squirrel_help_count_ = 0;
  next_owl_return_squirrel_count_ = kSquirrelsPerOwlReturn;
  owl_return_pending_ = false;
  owl_return_activity_center_ = {};
  rebuild_dynamic_mesh();
  render_frame_commands_.commands.reserve(3);

  if (!renderer.init()) {
    return false;
  }

  if (renderer.supports_separate_meshes()) {
    renderer.upload_static_mesh(terrain_streamer_.mesh());
    renderer.upload_dynamic_mesh(dynamic_mesh_);
  } else {
    rebuild_scene_mesh();
    renderer.upload_mesh(mesh_);
  }

  if (!audio_init()) {
    std::printf("Audio unavailable; continuing without sound.\n");
  }
  forest_audio_init();
  if (!subtitles_init()) {
    std::printf("Subtitles unavailable; continuing without subtitle overlay.\n");
  }

  initialized_ = true;
  return true;
}

void App::frame(Renderer& renderer, const CameraInput& input) {
  if (!initialized_) {
    return;
  }

  frame_stats_ = {};
  renderer.begin_frame_stats();
  const auto frame_start = Clock::now();
  const auto update_start = Clock::now();
  const bool confirm_pressed = input.action_pressed || (input.interact && !previous_interact_down_);
  bool conversation_started = false;
  squirrel_animation_upload_timer_ =
      std::max(0.0f, squirrel_animation_upload_timer_ - std::max(0.0f, input.delta_time));
  gameplay_animation_upload_timer_ =
      std::max(0.0f, gameplay_animation_upload_timer_ - std::max(0.0f, input.delta_time));

  CameraInput gameplay_input = input;
  if (conversation_controller_.locks_input()) {
    gameplay_input.forward = false;
    gameplay_input.back = false;
    gameplay_input.left = false;
    gameplay_input.right = false;
    gameplay_input.up = false;
    gameplay_input.down = false;
    gameplay_input.interact = false;
    gameplay_input.action_pressed = false;
    gameplay_input.action_held = false;
    gameplay_input.move_x = 0.0f;
    gameplay_input.move_y = 0.0f;
    gameplay_input.look_x = 0.0f;
    gameplay_input.look_y = 0.0f;
    gameplay_input.look_delta_x = 0.0f;
    gameplay_input.look_delta_y = 0.0f;
  }
  const bool gameplay_confirm_pressed =
      gameplay_input.action_pressed || (gameplay_input.interact && !previous_interact_down_);

  const bool fox_moved = fox_controller_.update(gameplay_input, generator_, camera_);
  const bool fox_animation_changed = fox_controller_.animation_changed();
  const Vec3 fox_position = fox_controller_.position();
  const float fox_heading = fox_controller_.heading();
  const bool owl_changed = owl_encounter_.update(gameplay_input.delta_time,
                                                 generator_,
                                                 fox_position,
                                                 gameplay_input.interact || gameplay_input.action_pressed);
  const int previous_carried_fireflies = firefly_loop_.carried_fireflies();
  const int previous_active_lantern_index = firefly_loop_.active_lantern_index();
  const int previous_deposited_fireflies = firefly_loop_.deposited_fireflies();
  const int previous_active_fireflies = firefly_loop_.active_firefly_count();
  bool gameplay_changed = firefly_loop_.update(gameplay_input.delta_time, generator_, fox_position, fox_heading);
  bool gameplay_structural_changed =
      previous_carried_fireflies != firefly_loop_.carried_fireflies() ||
      previous_active_lantern_index != firefly_loop_.active_lantern_index() ||
      previous_deposited_fireflies != firefly_loop_.deposited_fireflies() ||
      previous_active_fireflies != firefly_loop_.active_firefly_count();
  const SquirrelQuest::UpdateResult squirrel_update = squirrel_quest_.update(
      gameplay_input.delta_time,
      generator_,
      firefly_loop_,
      fox_position,
      !conversation_controller_.active() && !conversation_started);
  bool squirrel_animation_changed = squirrel_update.animation_changed;
  const RabbitBurrows::UpdateResult rabbit_update = rabbit_burrows_.update(
      gameplay_input.delta_time,
      generator_,
      firefly_loop_,
      squirrel_quest_,
      fox_position,
      gameplay_confirm_pressed,
      !conversation_controller_.active() && !conversation_started);
  OwlEncounter::DialogueEvent owl_dialogue = {};
  if (owl_encounter_.consume_dialogue_event(owl_dialogue)) {
    conversation_started = begin_owl_dialogue(owl_dialogue,
                                             fox_position,
                                             gameplay_changed,
                                             gameplay_structural_changed);
  }
  if (!conversation_controller_.active() && !conversation_started) {
    squirrel_approach_events_.clear();
    squirrel_quest_.drain_approach_events(squirrel_approach_events_);
    if (!squirrel_approach_events_.empty()) {
      const SquirrelQuest::ApproachEvent& event = squirrel_approach_events_.front();
      ConversationController::Request request = {};
      request.speaker_position = event.squirrel_position;
      request.listener_position = fox_position;
      request.speaker_id = event.squirrel_id;
      request.text = "";
      request.seconds = event.seconds;
      request.shot = ConversationController::Shot::FollowSpeaker;
      request.show_subtitle = false;
      conversation_controller_.begin(camera_, request);
      conversation_started = true;
    }
  }
  Vec3 active_squirrel_position = {};
  const bool active_squirrel_conversation =
      conversation_controller_.active() &&
      squirrel_quest_.squirrel_position(conversation_controller_.speaker_id(), active_squirrel_position);
  if (active_squirrel_conversation) {
    conversation_controller_.set_speaker_position(active_squirrel_position);
  }
  Vec3 active_rabbit_position = {};
  const bool active_rabbit_conversation =
      conversation_controller_.active() &&
      rabbit_burrows_.rabbit_position(conversation_controller_.speaker_id(), active_rabbit_position);
  if (active_rabbit_conversation) {
    conversation_controller_.set_speaker_position(active_rabbit_position);
  }
  if (conversation_controller_.active() && conversation_controller_.speaker_id() == kOwlSpeakerId) {
    conversation_controller_.set_speaker_position(owl_encounter_.position());
  }
  if (!conversation_started && (!conversation_controller_.active() || active_squirrel_conversation)) {
    squirrel_dialogue_events_.clear();
    squirrel_quest_.drain_dialogue_events(squirrel_dialogue_events_);
    for (const SquirrelQuest::DialogueEvent& event : squirrel_dialogue_events_) {
      if (conversation_controller_.active() && conversation_controller_.speaker_id() != event.squirrel_id) {
        continue;
      }
      ConversationController::Request request = {};
      request.speaker_position = event.squirrel_position;
      request.listener_position = fox_position;
      request.speaker_id = event.squirrel_id;
      request.text = event.text;
      request.seconds = event.seconds;
      request.shot = ConversationController::Shot::SpeakerCloseUp;
      squirrel_hud_revealed_ = true;
      if (conversation_controller_.active()) {
        conversation_controller_.replace_line(camera_, request);
      } else {
        conversation_controller_.begin(camera_, request);
      }
      conversation_started = true;
      break;
    }
  }
  if (!conversation_started && !conversation_controller_.active()) {
    rabbit_dialogue_events_.clear();
    rabbit_burrows_.drain_dialogue_events(rabbit_dialogue_events_);
    if (!rabbit_dialogue_events_.empty()) {
      const RabbitBurrows::DialogueEvent& event = rabbit_dialogue_events_.front();
      ConversationController::Request request = {};
      request.speaker_position = event.rabbit_position;
      request.listener_position = fox_position;
      request.speaker_id = event.rabbit_id;
      request.text = event.text;
      request.seconds = event.seconds;
      request.shot = ConversationController::Shot::SpeakerCloseUp;
      conversation_controller_.begin(camera_, request);
      conversation_started = true;
    }
  }
  squirrel_completion_events_.clear();
  squirrel_quest_.drain_completion_events(squirrel_completion_events_);
  update_owl_return_progress(squirrel_completion_events_);
  for (const SquirrelQuest::CompletionEvent& event : squirrel_completion_events_) {
    firefly_loop_.add_squirrel_completion_bonus(event.position);
  }
  if (!conversation_controller_.active() && !squirrel_completion_events_.empty()) {
    const SquirrelQuest::CompletionEvent& event = squirrel_completion_events_.front();
    ConversationController::Request request = {};
    request.speaker_position = event.squirrel_position;
    request.listener_position = fox_position;
    request.speaker_id = event.squirrel_id;
    request.text = event.text;
    request.seconds = event.seconds;
    request.shot = ConversationController::Shot::SpeakerMediumCloseUp;
    conversation_controller_.begin(camera_, request);
    conversation_started = true;
  }
  const bool squirrel_events_waiting =
      !squirrel_approach_events_.empty() ||
      !squirrel_dialogue_events_.empty() ||
      !squirrel_completion_events_.empty();
  if (!conversation_controller_.active() && !conversation_started) {
    const bool owl_return_started = maybe_schedule_owl_return(fox_position, squirrel_events_waiting);
    if (owl_return_started) {
      gameplay_changed = true;
      gameplay_structural_changed = true;
      OwlEncounter::DialogueEvent return_dialogue = {};
      if (owl_encounter_.consume_dialogue_event(return_dialogue)) {
        conversation_started = begin_owl_dialogue(return_dialogue,
                                                 fox_position,
                                                 gameplay_changed,
                                                 gameplay_structural_changed);
      }
    }
  }
  rebuild_gameplay_lights();
  const bool conversation_camera_changed = conversation_controller_.active()
      ? conversation_controller_.update(input.delta_time, confirm_pressed && !conversation_started, camera_)
      : false;
  const bool squirrel_talking_changed =
      squirrel_quest_.set_talking_squirrel(conversation_controller_.speaker_id(), conversation_controller_.talking());
  squirrel_animation_changed = squirrel_animation_changed || squirrel_talking_changed;
  if (!conversation_controller_.active()) {
    update_camera(gameplay_input);
  }
  const bool owl_encounter_active = owl_encounter_.talking() || owl_encounter_.flying();
  const ForestAudioPlayerState player_audio = {
    fox_position,
    fox_controller_.forward(),
    fox_controller_.movement_speed(),
    true,
  };
  const ForestAudioWorldState world_audio = {
    firefly_loop_.objective_position(fox_position),
    owl_encounter_.position(),
    owl_encounter_active,
  };
  forest_audio_update(input.delta_time, &player_audio, &world_audio);
  audio_update(input.delta_time);
  subtitles_update(input.delta_time);
  update_lantern_hud();
  subtitles_set_fps(hud_fps_);
  frame_stats_.carried_fireflies = firefly_loop_.carried_fireflies();
  frame_stats_.active_lantern_index = firefly_loop_.active_lantern_index();
  frame_stats_.deposited_fireflies = firefly_loop_.deposited_fireflies();
  frame_stats_.required_fireflies = firefly_loop_.required_fireflies();
  frame_stats_.active_fireflies = firefly_loop_.active_firefly_count();
  frame_stats_.active_gameplay_lights = gameplay_light_count_;
  frame_stats_.gameplay_light_limit = gameplay_light_limit_;
  frame_stats_.fps = hud_fps_;
  frame_stats_.firefly_glow_intensity = firefly_loop_.firefly_glow_intensity();
  frame_stats_.lantern_light_intensity = firefly_loop_.lantern_light_intensity();
  frame_stats_.lantern_light_radius = firefly_loop_.lantern_light_radius();
  frame_stats_.distance_to_objective = horizontal_distance(fox_position, firefly_loop_.objective_position(fox_position));
  frame_stats_.update_ns = elapsed_ns(update_start, Clock::now());
  frame_stats_.fox_moved = fox_moved;
  frame_stats_.squirrel_structural_changed = squirrel_update.structural_changed;
  frame_stats_.squirrel_animation_changed = squirrel_animation_changed;
  frame_stats_.squirrel_lights_changed = squirrel_update.lights_changed;
  frame_stats_.gameplay_structural_changed = gameplay_structural_changed;

  const auto world_rebuild_start = Clock::now();
  const bool chunk_changed = terrain_streamer_.update(generator_, fox_position);
  frame_stats_.chunk_changed = chunk_changed;
  if (chunk_changed) {
    frame_stats_.world_rebuild_ns = elapsed_ns(world_rebuild_start, Clock::now());
  }
  const TerrainStreamer::Stats& terrain_stats = terrain_streamer_.stats();
  frame_stats_.terrain_visible_chunks = terrain_stats.visible_chunks;
  frame_stats_.terrain_rebuilt_chunks = terrain_stats.rebuilt_chunks;
  frame_stats_.terrain_vertices = terrain_stats.visible_vertices;
  frame_stats_.terrain_triangles = terrain_stats.visible_triangles;
  frame_stats_.terrain_largest_chunk_vertices = terrain_stats.largest_chunk_vertices;
  frame_stats_.terrain_largest_chunk_triangles = terrain_stats.largest_chunk_triangles;
  frame_stats_.terrain_rebuilt_surface_columns = terrain_stats.rebuilt_surface_columns;
  frame_stats_.terrain_skipped_voxel_samples = terrain_stats.skipped_terrain_voxel_samples;

  const bool squirrel_animation_upload_due =
      squirrel_animation_changed && squirrel_animation_upload_timer_ <= 0.0f;
  const bool squirrel_mesh_changed =
      squirrel_update.structural_changed || squirrel_animation_upload_due;
  const bool gameplay_animation_upload_due =
      gameplay_changed && gameplay_animation_upload_timer_ <= 0.0f;
  const bool gameplay_mesh_changed =
      gameplay_structural_changed || gameplay_animation_upload_due;
  const bool rabbit_mesh_changed =
      rabbit_update.structural_changed || rabbit_update.animation_changed;
  const bool dynamic_mesh_update_needed =
      fox_moved ||
      fox_animation_changed ||
      owl_changed ||
      gameplay_mesh_changed ||
      squirrel_mesh_changed ||
      rabbit_mesh_changed ||
      conversation_started ||
      conversation_camera_changed ||
      !squirrel_completion_events_.empty() ||
      chunk_changed;
  frame_stats_.squirrel_animation_upload_due = squirrel_animation_upload_due;
  frame_stats_.gameplay_animation_upload_due = gameplay_animation_upload_due;

  if (renderer.supports_separate_meshes()) {
    if (chunk_changed) {
      const auto upload_start = Clock::now();
      renderer.upload_static_mesh(terrain_streamer_.mesh());
      frame_stats_.upload_ns += elapsed_ns(upload_start, Clock::now());
    }
    if (dynamic_mesh_update_needed) {
      const auto fox_rebuild_start = Clock::now();
      if (fox_moved || fox_animation_changed || chunk_changed) {
        rebuild_fox_mesh();
      }
      rebuild_dynamic_mesh();
      frame_stats_.dynamic_mesh_rebuilt = true;
      frame_stats_.fox_rebuild_ns = elapsed_ns(fox_rebuild_start, Clock::now());
      const auto upload_start = Clock::now();
      renderer.upload_dynamic_mesh(dynamic_mesh_);
      frame_stats_.dynamic_mesh_uploaded = true;
      frame_stats_.upload_ns += elapsed_ns(upload_start, Clock::now());
      if (gameplay_changed) {
        gameplay_animation_upload_timer_ = kGameplayAnimationUploadInterval;
      }
      if (squirrel_update.structural_changed || squirrel_animation_changed) {
        squirrel_animation_upload_timer_ = kSquirrelAnimationUploadInterval;
      }
    }
  } else if (dynamic_mesh_update_needed) {
    const auto fox_rebuild_start = Clock::now();
    if (fox_moved || fox_animation_changed || chunk_changed) {
      rebuild_fox_mesh();
    }
    rebuild_dynamic_mesh();
    frame_stats_.dynamic_mesh_rebuilt = true;
    frame_stats_.fox_rebuild_ns = elapsed_ns(fox_rebuild_start, Clock::now());
    const auto scene_rebuild_start = Clock::now();
    rebuild_scene_mesh();
    frame_stats_.scene_rebuild_ns = elapsed_ns(scene_rebuild_start, Clock::now());
    const auto upload_start = Clock::now();
    renderer.upload_mesh(mesh_);
    frame_stats_.dynamic_mesh_uploaded = true;
    frame_stats_.upload_ns = elapsed_ns(upload_start, Clock::now());
    if (gameplay_changed) {
      gameplay_animation_upload_timer_ = kGameplayAnimationUploadInterval;
    }
    if (squirrel_update.structural_changed || squirrel_animation_changed) {
      squirrel_animation_upload_timer_ = kSquirrelAnimationUploadInterval;
    }
  }

  const auto render_start = Clock::now();
  render_frame_commands_.clear();
  render_frame_commands_.camera = camera_;
  render_frame_commands_.light_count = std::min(gameplay_light_count_, kMaxRendererGameplayLights);
  for (int i = 0; i < render_frame_commands_.light_count; ++i) {
    render_frame_commands_.lights[static_cast<std::size_t>(i)] = gameplay_lights_[static_cast<std::size_t>(i)];
  }
  render_frame_commands_.subtitle = &subtitles_frame();
  render_frame_commands_.hud = &subtitles_hud_frame();
  render_frame_commands_.fps = &subtitles_fps_frame();
  render_frame_commands_.commands.push_back({RenderCommandType::DrawStaticMesh});
  if (renderer.supports_separate_meshes()) {
    render_frame_commands_.commands.push_back({RenderCommandType::DrawDynamicMesh});
  }
  render_frame_commands_.commands.push_back({RenderCommandType::DrawSubtitle});
  renderer.render_frame(render_frame_commands_);
  frame_stats_.render_ns = elapsed_ns(render_start, Clock::now());
  frame_stats_.total_ns = elapsed_ns(frame_start, Clock::now());
  frame_stats_.fps = frame_stats_.total_ns > 0
      ? 1000000000.0f / static_cast<float>(frame_stats_.total_ns)
      : 0.0f;
  hud_fps_ = frame_stats_.fps;
  previous_interact_down_ = input.interact;
}

void App::shutdown(Renderer& renderer) {
  subtitles_shutdown();
  forest_audio_shutdown();
  audio_shutdown();
  renderer.shutdown();
  initialized_ = false;
}

void App::update_owl_return_progress(const std::vector<SquirrelQuest::CompletionEvent>& completion_events) {
  for (const SquirrelQuest::CompletionEvent& event : completion_events) {
    ++squirrel_help_count_;
    owl_return_activity_center_ = event.position;
    if (squirrel_help_count_ >= next_owl_return_squirrel_count_ && !owl_return_pending_) {
      owl_return_pending_ = true;
#if defined(VOXEL_DEBUG_OWL_RETURN)
      std::printf("owl return queued squirrels %d activity %.2f %.2f %.2f\n",
                  squirrel_help_count_,
                  owl_return_activity_center_.x,
                  owl_return_activity_center_.y,
                  owl_return_activity_center_.z);
#endif
    }
  }
}

bool App::maybe_schedule_owl_return(Vec3 fox_position, bool squirrel_events_waiting) {
  if (!owl_return_pending_ || !owl_encounter_.gone()) {
    return false;
  }
  if (conversation_controller_.active() ||
      squirrel_events_waiting ||
      squirrel_quest_.active_quest_needs_acorns() ||
      squirrel_quest_.fox_near_auto_talk_squirrel(fox_position) ||
      subtitles_visible()) {
    return false;
  }

  if (horizontal_distance(fox_position, owl_return_activity_center_) <=
      kOwlReturnClearRadius + kOwlReturnClearBuffer) {
    return false;
  }

  const bool scheduled = owl_encounter_.schedule_return(
      generator_,
      firefly_loop_,
      squirrel_quest_,
      rabbit_burrows_,
      owl_return_activity_center_,
      fox_position,
      fox_controller_.forward(),
      kOwlReturnClearRadius,
      owl_return_line_for_squirrel_count(next_owl_return_squirrel_count_));
  if (scheduled) {
    owl_return_pending_ = false;
    next_owl_return_squirrel_count_ += kSquirrelsPerOwlReturn;
  }
  return scheduled;
}

bool App::begin_owl_dialogue(const OwlEncounter::DialogueEvent& owl_dialogue,
                             Vec3 fox_position,
                             bool& gameplay_changed,
                             bool& gameplay_structural_changed) {
  if (owl_dialogue.line == 1 && !firefly_loop_.fireflies_unlocked()) {
    firefly_loop_.unlock_fireflies(generator_);
    gameplay_changed = true;
    gameplay_structural_changed = true;
  }
  if (owl_dialogue.line == 2) {
    lantern_hud_revealed_ = true;
  }

  ConversationController::Request request = {};
  request.speaker_position = owl_encounter_.position();
  request.listener_position = fox_position;
  request.speaker_id = kOwlSpeakerId;
  request.text = owl_dialogue.text;
  request.seconds = owl_dialogue.seconds;
  request.show_subtitle = owl_dialogue.show_subtitle;
  request.allow_confirm_skip = false;
  if (!owl_dialogue.show_subtitle) {
    request.shot = ConversationController::Shot::FollowSpeaker;
  } else if (owl_dialogue.line == 1) {
    request.shot = ConversationController::Shot::SpeakerCloseUp;
  } else if (owl_dialogue.line == 2) {
    request.shot = ConversationController::Shot::LookAtFocus;
    request.focus_position = firefly_loop_.farthest_firefly_position(fox_position);
  } else if (owl_dialogue.focus_target) {
    request.shot = ConversationController::Shot::LookAtFocus;
    request.focus_position = owl_dialogue.target_position;
  } else {
    request.shot = ConversationController::Shot::SpeakerMediumCloseUp;
  }
  if (conversation_controller_.active()) {
    conversation_controller_.replace_line(camera_, request);
  } else {
    conversation_controller_.begin(camera_, request);
  }
  return true;
}

void App::rebuild_fox_mesh() {
  fox_mesh_.clear();
  append_fox_mesh(fox_mesh_,
                  fox_controller_.position(),
                  fox_controller_.heading(),
                  fox_controller_.animation_pose());
}

void App::rebuild_dynamic_mesh() {
  dynamic_mesh_.clear();
  append_mesh(dynamic_mesh_, fox_mesh_);
  firefly_loop_.append_dynamic_mesh(dynamic_mesh_, fox_controller_.position(), fox_controller_.heading());
  squirrel_quest_.append_dynamic_mesh(dynamic_mesh_, fox_controller_.position());
  rabbit_burrows_.append_dynamic_mesh(dynamic_mesh_, fox_controller_.position(), fox_controller_.heading());
  append_owl_perch_mesh(dynamic_mesh_, owl_encounter_.perch_position(), owl_encounter_.perch_heading());
  if (owl_encounter_.return_perch_visible()) {
    append_owl_perch_mesh(dynamic_mesh_,
                          owl_encounter_.return_perch_position(),
                          owl_encounter_.return_perch_heading());
  }
  if (owl_encounter_.gone()) {
    return;
  }

  append_owl_mesh(dynamic_mesh_,
                  owl_encounter_.position(),
                  owl_encounter_.heading(),
                  owl_encounter_.wing_pose(),
                  owl_encounter_.head_yaw(),
                  owl_encounter_.head_pitch(),
                  owl_encounter_.head_roll(),
                  owl_encounter_.body_bob(),
                  owl_encounter_.blink());
}

void App::rebuild_scene_mesh() {
  mesh_ = terrain_streamer_.mesh();
  append_mesh(mesh_, dynamic_mesh_);
}

void App::set_gameplay_light_limit(int limit) {
  gameplay_light_limit_ = std::max(0, std::min(kMaxRendererGameplayLights, limit));
}

void App::dev_collect_active_fireflies() {
  firefly_loop_.dev_collect_active_fireflies();
}

void App::dev_deposit_carried_fireflies() {
  firefly_loop_.dev_deposit_carried_fireflies(generator_);
}

void App::update_lantern_hud() {
  char text[160] = {};
  int written = 0;
  if (lantern_hud_revealed_) {
    written = std::snprintf(text,
                            sizeof(text),
                            "Lanterns lit %d",
                            firefly_loop_.active_lantern_index());
  }
  if (squirrel_hud_revealed_ && written >= 0 && written < static_cast<int>(sizeof(text))) {
    written += std::snprintf(text + written,
                             sizeof(text) - static_cast<std::size_t>(written),
                             "%sSquirrels helped %d",
                             written > 0 ? "   " : "",
                             squirrel_quest_.completed_squirrels());
  }
  if (squirrel_hud_revealed_ &&
      squirrel_quest_.active_quest_needs_acorns() &&
      written >= 0 &&
      written < static_cast<int>(sizeof(text))) {
    std::snprintf(text + written,
                  sizeof(text) - static_cast<std::size_t>(written),
                  "%sAcorns %d/%d",
                  written > 0 ? "\n" : "",
                  squirrel_quest_.active_collected_acorns(),
                  squirrel_quest_.active_required_acorns());
  }
  const char* rabbit_prompt = rabbit_burrows_.interaction_prompt(fox_controller_.position());
  if (rabbit_prompt != nullptr && !subtitles_visible()) {
    subtitles_show(rabbit_prompt, 0.45f);
  }
  subtitles_set_hud_text(text);
}

void App::rebuild_gameplay_lights() {
  for (GameplayLight& light : gameplay_lights_) {
    light = {};
  }
  gameplay_light_count_ = 0;
  firefly_loop_.append_gameplay_lights(gameplay_lights_,
                                       gameplay_light_count_,
                                       gameplay_light_limit_,
                                       fox_controller_.position(),
                                       fox_controller_.heading());
  squirrel_quest_.append_gameplay_lights(gameplay_lights_,
                                         gameplay_light_count_,
                                         gameplay_light_limit_,
                                         fox_controller_.position());
}

void App::update_camera(const CameraInput& input) {
  const float look_delta_x = input.look_delta_x +
      clamped_axis(input.look_x) * kNormalizedLookPixelsPerSecond * input.delta_time;
  const float look_delta_y = input.look_delta_y +
      clamped_axis(input.look_y) * kNormalizedLookPixelsPerSecond * input.delta_time;
  camera_.yaw += look_delta_x * camera_.look_sensitivity;
  camera_.pitch -= look_delta_y * camera_.look_sensitivity;
  camera_.pitch = std::max(-1.20f, std::min(-0.12f, camera_.pitch));

  const Vec3 target = fox_controller_.position() + Vec3{0.0f, kCameraTargetHeight, 0.0f};
  camera_.position = target - camera_.forward() * kCameraDistance;
}

}  // namespace voxel
