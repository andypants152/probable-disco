#include "audio.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace voxel {

namespace {

constexpr int kPreferredSampleRate = 48000;
constexpr int kFallbackSampleRate = 44100;
constexpr int kChannels = 2;
constexpr int kVoiceCount = 24;
#if defined(__SWITCH__)
constexpr int kBufferSamples = 1024;
#else
constexpr int kBufferSamples = 512;
#endif
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

enum class VoiceKind {
  Chime,
  OwlTone,
  OwlNoise,
  FootstepNoise,
};

struct Voice {
  bool active = false;
  VoiceKind kind = VoiceKind::Chime;
  float frequency = 440.0f;
  float phase = 0.0f;
  float amplitude = 0.0f;
  float pan = 0.0f;
  float age = 0.0f;
  float duration = 1.0f;
  float decay = 3.0f;
  float descend = 0.0f;
  float filter = 0.0f;
  std::uint32_t noise = 0x12345678u;
};

struct AudioState {
  SDL_AudioDeviceID device = 0;
  SDL_AudioSpec spec = {};
  bool initialized = false;
  bool sdl_audio_started = false;
  bool sdl_audio_owned = false;
  bool gameplay_audio_ready = false;
  float hum_target_volume = 0.0f;
  float hum_volume = 0.0f;
  float hum_target_pitch = 1.0f;
  float hum_pitch = 1.0f;
  float hum_phase_a = 0.0f;
  float hum_phase_b = 0.0f;
  float hum_lfo_phase = 0.0f;
  Voice voices[kVoiceCount] = {};
  std::uint32_t rng = 0xa53c4d1fu;
  float debug_timer = 0.0f;
};

AudioState g_audio;

float clamp_float(float value, float minimum, float maximum) {
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return value;
}

float wrap_phase(float phase) {
  while (phase >= kTwoPi) {
    phase -= kTwoPi;
  }
  return phase;
}

float next_random_unit() {
  std::uint32_t x = g_audio.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_audio.rng = x;
  return static_cast<float>(x & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

std::uint32_t next_noise(std::uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return state;
}

float equal_power_left(float pan) {
  return std::cos((pan + 1.0f) * 0.25f * kPi);
}

float equal_power_right(float pan) {
  return std::sin((pan + 1.0f) * 0.25f * kPi);
}

Voice* find_free_voice() {
  Voice* oldest = &g_audio.voices[0];
  for (Voice& voice : g_audio.voices) {
    if (!voice.active) {
      return &voice;
    }
    if (voice.age > oldest->age) {
      oldest = &voice;
    }
  }
  return oldest;
}

void start_voice(VoiceKind kind, float frequency, float amplitude, float duration, float decay, float pan, float descend) {
  Voice* voice = find_free_voice();
  *voice = {};
  voice->active = true;
  voice->kind = kind;
  voice->frequency = frequency;
  voice->amplitude = amplitude;
  voice->duration = duration;
  voice->decay = decay;
  voice->pan = clamp_float(pan, -1.0f, 1.0f);
  voice->descend = descend;
  voice->noise = g_audio.rng ^ 0x6d2b79f5u;
}

float render_hum(AudioState& state, float sample_rate) {
  state.hum_volume += (state.hum_target_volume - state.hum_volume) * 0.0012f;
  state.hum_pitch += (state.hum_target_pitch - state.hum_pitch) * 0.0008f;

  const float pitch = clamp_float(state.hum_pitch, 0.55f, 1.85f);
  const float base_frequency = 132.0f * pitch;
  state.hum_phase_a = wrap_phase(state.hum_phase_a + kTwoPi * base_frequency / sample_rate);
  state.hum_phase_b = wrap_phase(state.hum_phase_b + kTwoPi * (base_frequency * 1.333f) / sample_rate);
  state.hum_lfo_phase = wrap_phase(state.hum_lfo_phase + kTwoPi * 0.065f / sample_rate);

  const float lfo = 0.72f + 0.06f * std::sin(state.hum_lfo_phase) +
                    0.025f * std::sin(state.hum_lfo_phase * 2.37f + 1.2f);
  const float tone = 0.42f * std::sin(state.hum_phase_a) +
                     0.14f * std::sin(state.hum_phase_b + 0.4f);
  return tone * state.hum_volume * lfo;
}

float render_voice(Voice& voice, float sample_rate) {
  voice.age += 1.0f / sample_rate;
  if (voice.age >= voice.duration) {
    voice.active = false;
    return 0.0f;
  }

  if (voice.kind == VoiceKind::Chime) {
    const float attack = clamp_float(voice.age / 0.018f, 0.0f, 1.0f);
    const float envelope = attack * std::exp(-voice.age * voice.decay);
    voice.phase = wrap_phase(voice.phase + kTwoPi * voice.frequency / sample_rate);
    const float harmonic = std::sin(voice.phase * 2.006f + 0.35f);
    return (std::sin(voice.phase) + 0.32f * harmonic) * voice.amplitude * envelope;
  }

  if (voice.kind == VoiceKind::OwlTone) {
    const float t = voice.age / voice.duration;
    const float release = clamp_float((voice.duration - voice.age) / 0.28f, 0.0f, 1.0f);
    const float attack = clamp_float(voice.age / 0.075f, 0.0f, 1.0f);
    const float frequency = voice.frequency - voice.descend * t + 14.0f * std::sin(t * kTwoPi * 2.0f);
    voice.phase = wrap_phase(voice.phase + kTwoPi * frequency / sample_rate);
    return std::sin(voice.phase) * voice.amplitude * attack * release;
  }

  if (voice.kind == VoiceKind::FootstepNoise) {
    const float attack = clamp_float(voice.age / 0.018f, 0.0f, 1.0f);
    const float release = clamp_float((voice.duration - voice.age) / voice.duration, 0.0f, 1.0f);
    const std::uint32_t noise = next_noise(voice.noise);
    const float raw = (static_cast<float>((noise >> 10) & 0x3ffu) / 512.0f) - 1.0f;
    voice.filter += (raw - voice.filter) * 0.11f;
    return voice.filter * voice.amplitude * attack * release * release;
  }

  const float attack = clamp_float(voice.age / 0.055f, 0.0f, 1.0f);
  const float release = clamp_float((voice.duration - voice.age) / 0.42f, 0.0f, 1.0f);
  const std::uint32_t noise = next_noise(voice.noise);
  const float raw = (static_cast<float>((noise >> 9) & 0x7ffu) / 1024.0f) - 1.0f;
  voice.filter += (raw - voice.filter) * 0.035f;
  voice.phase = wrap_phase(voice.phase + kTwoPi * 3.2f / sample_rate);
  const float wobble = 0.65f + 0.35f * std::sin(voice.phase);
  return voice.filter * voice.amplitude * attack * release * wobble;
}

void mix_sample(float& left, float& right) {
  AudioState& state = g_audio;
  const float sample_rate = static_cast<float>(state.spec.freq > 0 ? state.spec.freq : kPreferredSampleRate);

  const float mono = render_hum(state, sample_rate);
  left = mono;
  right = mono;

  for (Voice& voice : state.voices) {
    if (!voice.active) {
      continue;
    }
    const float sample = render_voice(voice, sample_rate);
    left += sample * equal_power_left(voice.pan);
    right += sample * equal_power_right(voice.pan);
  }

  left = clamp_float(left, -1.0f, 1.0f);
  right = clamp_float(right, -1.0f, 1.0f);
}

void audio_callback(void*, Uint8* stream, int length) {
  AudioState& state = g_audio;
  const int frames = length / (SDL_AUDIO_BITSIZE(state.spec.format) / 8) / kChannels;

  if (state.spec.format == AUDIO_F32SYS) {
    float* out = reinterpret_cast<float*>(stream);
    for (int i = 0; i < frames; ++i) {
      float left = 0.0f;
      float right = 0.0f;
      mix_sample(left, right);
      out[i * 2 + 0] = left;
      out[i * 2 + 1] = right;
    }
    return;
  }

  Sint16* out = reinterpret_cast<Sint16*>(stream);
  for (int i = 0; i < frames; ++i) {
    float left = 0.0f;
    float right = 0.0f;
    mix_sample(left, right);
    out[i * 2 + 0] = static_cast<Sint16>(left * 32767.0f);
    out[i * 2 + 1] = static_cast<Sint16>(right * 32767.0f);
  }
}

bool open_device(SDL_AudioFormat format, int frequency) {
  SDL_AudioSpec desired = {};
  desired.freq = frequency;
  desired.format = format;
  desired.channels = kChannels;
  desired.samples = kBufferSamples;
  desired.callback = audio_callback;

  g_audio.device = SDL_OpenAudioDevice(nullptr, 0, &desired, &g_audio.spec,
                                       SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
                                           SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
  return g_audio.device != 0;
}

int active_voice_count_locked() {
  int count = 0;
  for (const Voice& voice : g_audio.voices) {
    if (voice.active) {
      ++count;
    }
  }
  return count;
}

}  // namespace

bool audio_init() {
  if (g_audio.initialized) {
    return true;
  }

  if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
      std::printf("Audio init failed: %s\n", SDL_GetError());
      return false;
    }
    g_audio.sdl_audio_owned = true;
  }
  g_audio.sdl_audio_started = true;

#if defined(__SWITCH__)
  const bool opened = open_device(AUDIO_S16SYS, kPreferredSampleRate) ||
                      open_device(AUDIO_S16SYS, kFallbackSampleRate) ||
                      open_device(AUDIO_F32SYS, kPreferredSampleRate) ||
                      open_device(AUDIO_F32SYS, kFallbackSampleRate);
#else
  const bool opened = open_device(AUDIO_F32SYS, kPreferredSampleRate) ||
                      open_device(AUDIO_F32SYS, kFallbackSampleRate) ||
                      open_device(AUDIO_S16SYS, kPreferredSampleRate) ||
                      open_device(AUDIO_S16SYS, kFallbackSampleRate);
#endif
  if (!opened) {
    std::printf("Audio device open failed: %s\n", SDL_GetError());
    if (g_audio.sdl_audio_owned) {
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      g_audio.sdl_audio_owned = false;
    }
    g_audio.sdl_audio_started = false;
    return false;
  }

  g_audio.hum_target_volume = 0.0f;
  g_audio.hum_target_pitch = 1.0f;
  g_audio.initialized = true;
#if defined(__EMSCRIPTEN__)
  g_audio.gameplay_audio_ready = false;
#else
  g_audio.gameplay_audio_ready = true;
#endif
  SDL_PauseAudioDevice(g_audio.device, 0);
  std::printf("Audio initialized: yes rate=%d channels=%d format=%s\n",
              g_audio.spec.freq,
              g_audio.spec.channels,
              g_audio.spec.format == AUDIO_F32SYS ? "float" : "s16");
  return true;
}

void audio_shutdown() {
  if (g_audio.device != 0) {
    SDL_CloseAudioDevice(g_audio.device);
  }
  if (g_audio.sdl_audio_owned) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
  g_audio = AudioState{};
}

void audio_update(float dt) {
  if (!g_audio.initialized) {
    return;
  }

  g_audio.debug_timer += dt;
  if (g_audio.debug_timer >= 5.0f) {
    g_audio.debug_timer = 0.0f;
    const AudioDebugStatus status = audio_debug_status();
    std::printf("audio yes rate %d voices %d hum %.2f pitch %.2f\n",
                status.sample_rate,
                status.active_voices,
                status.hum_volume,
                status.hum_pitch);
  }
}

void audio_resume() {
  if (g_audio.initialized && g_audio.device != 0) {
    SDL_PauseAudioDevice(g_audio.device, 0);
    g_audio.gameplay_audio_ready = true;
  }
}

bool audio_ready_for_gameplay_sound() {
  return g_audio.initialized && g_audio.device != 0 && g_audio.gameplay_audio_ready;
}

void audio_set_forest_hum(float volume, float pitch) {
  if (!g_audio.initialized || g_audio.device == 0) {
    return;
  }

  SDL_LockAudioDevice(g_audio.device);
  g_audio.hum_target_volume = clamp_float(volume, 0.0f, 0.18f);
  g_audio.hum_target_pitch = clamp_float(pitch, 0.55f, 1.85f);
  SDL_UnlockAudioDevice(g_audio.device);
}

void audio_play_mote_chime(float intensity) {
  if (!g_audio.initialized || g_audio.device == 0) {
    return;
  }

  intensity = clamp_float(intensity, 0.0f, 1.0f);
  SDL_LockAudioDevice(g_audio.device);
  const float random_a = next_random_unit();
  const float random_b = next_random_unit();
  const float frequency = 920.0f + random_a * 760.0f + intensity * 180.0f;
  const float pan = random_b * 1.4f - 0.7f;
  start_voice(VoiceKind::Chime, frequency, 0.09f + 0.08f * intensity, 1.2f, 4.8f, pan, 0.0f);
  SDL_UnlockAudioDevice(g_audio.device);
}

void audio_play_owl_appear() {
  if (!g_audio.initialized || g_audio.device == 0) {
    return;
  }

  SDL_LockAudioDevice(g_audio.device);
  start_voice(VoiceKind::OwlTone, 360.0f, 0.24f, 1.05f, 2.0f, -0.14f, 150.0f);
  start_voice(VoiceKind::OwlTone, 265.0f, 0.15f, 1.25f, 2.0f, 0.18f, 70.0f);
  start_voice(VoiceKind::OwlNoise, 0.0f, 0.075f, 0.92f, 2.0f, 0.05f, 0.0f);
  SDL_UnlockAudioDevice(g_audio.device);
}

void audio_play_footstep_rustle(float intensity) {
  if (!g_audio.initialized || g_audio.device == 0) {
    return;
  }

  intensity = clamp_float(intensity, 0.0f, 1.0f);
  SDL_LockAudioDevice(g_audio.device);
  const float pan = next_random_unit() * 0.5f - 0.25f;
  start_voice(VoiceKind::FootstepNoise, 0.0f, 0.014f + 0.018f * intensity, 0.16f, 5.0f, pan, 0.0f);
  SDL_UnlockAudioDevice(g_audio.device);
}

AudioDebugStatus audio_debug_status() {
  AudioDebugStatus status;
  status.initialized = g_audio.initialized;
  if (!g_audio.initialized || g_audio.device == 0) {
    return status;
  }

  SDL_LockAudioDevice(g_audio.device);
  status.sample_rate = g_audio.spec.freq;
  status.active_voices = active_voice_count_locked();
  status.hum_volume = g_audio.hum_target_volume;
  status.hum_pitch = g_audio.hum_target_pitch;
  SDL_UnlockAudioDevice(g_audio.device);
  return status;
}

}  // namespace voxel
