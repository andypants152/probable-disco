#pragma once

namespace voxel {

bool audio_init();
void audio_shutdown();
void audio_update(float dt);
void audio_resume();
bool audio_ready_for_gameplay_sound();

void audio_play_mote_chime(float intensity);
void audio_play_mote_note(float frequency, float intensity, float pan);
void audio_play_owl_appear();
void audio_play_footstep_rustle(float intensity);

}  // namespace voxel
