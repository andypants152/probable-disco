#pragma once

namespace voxel {

struct AudioDebugStatus {
  bool initialized = false;
  int sample_rate = 0;
  int active_voices = 0;
  float hum_volume = 0.0f;
  float hum_pitch = 1.0f;
};

bool audio_init();
void audio_shutdown();
void audio_update(float dt);
void audio_resume();
bool audio_ready_for_gameplay_sound();

void audio_set_forest_hum(float volume, float pitch);
void audio_play_mote_chime(float intensity);
void audio_play_owl_appear();
void audio_play_footstep_rustle(float intensity);

AudioDebugStatus audio_debug_status();

}  // namespace voxel
