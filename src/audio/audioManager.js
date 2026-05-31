import { audioConfig } from "../config.js";
import { createOrbHum } from "./orbHum.js";

export function createAudioManager() {
  const AudioContextClass = window.AudioContext || window.webkitAudioContext;
  let audioContext = null;
  let masterGain = null;
  let orbHum = null;
  let available = Boolean(AudioContextClass);
  let unlocked = false;
  let muted = false;
  let gamepadUnlockAttempted = false;

  function initAudio() {
    if (!available || audioContext) return Boolean(audioContext);

    try {
      audioContext = new AudioContextClass();
      masterGain = audioContext.createGain();
      masterGain.gain.value = audioConfig.masterVolume;
      masterGain.connect(audioContext.destination);
      orbHum = createOrbHum(audioContext, masterGain);
      return true;
    } catch {
      available = false;
      return false;
    }
  }

  async function unlockAudio() {
    if (!available) return false;
    if (!initAudio()) return false;

    try {
      if (audioContext.state !== "running") {
        await audioContext.resume();
      }
      unlocked = audioContext.state === "running";
      return unlocked;
    } catch {
      return false;
    }
  }

  function setMasterVolume(value) {
    if (!masterGain || !audioContext) return;
    masterGain.gain.setTargetAtTime(muted ? 0 : value, audioContext.currentTime, 0.03);
  }

  function setMuted(value) {
    muted = value;
    setMasterVolume(audioConfig.masterVolume);
  }

  function toggleMute() {
    setMuted(!muted);
    return muted;
  }

  function playTone({ frequency, duration, gain, type = "sine", endFrequency = frequency, filter = 1200 }) {
    if (!unlocked || muted || !audioContext || !masterGain) return;

    const now = audioContext.currentTime;
    const oscillator = audioContext.createOscillator();
    const envelope = audioContext.createGain();
    const toneFilter = audioContext.createBiquadFilter();

    oscillator.type = type;
    oscillator.frequency.setValueAtTime(frequency, now);
    oscillator.frequency.exponentialRampToValueAtTime(endFrequency, now + duration);
    toneFilter.type = "lowpass";
    toneFilter.frequency.value = filter;
    envelope.gain.setValueAtTime(0, now);
    envelope.gain.linearRampToValueAtTime(gain, now + 0.035);
    envelope.gain.exponentialRampToValueAtTime(0.0001, now + duration);

    oscillator.connect(toneFilter);
    toneFilter.connect(envelope);
    envelope.connect(masterGain);
    oscillator.start(now);
    oscillator.stop(now + duration + 0.04);
  }

  function playCharmAppear() {
    playTone({ frequency: 520, endFrequency: 760, duration: 0.65, gain: 0.035, type: "triangle", filter: 1800 });
  }

  function playCharmPickup() {
    playTone({ frequency: 420, endFrequency: 840, duration: 0.8, gain: 0.05, type: "sine", filter: 1600 });
  }

  function playOwlAppear() {
    playTone({ frequency: 170, endFrequency: 120, duration: 0.75, gain: 0.04, type: "triangle", filter: 700 });
  }

  function playClearingReached() {
    playTone({ frequency: 260, endFrequency: 390, duration: 1.15, gain: 0.045, type: "sine", filter: 1300 });
  }

  function pollGamepadUnlock() {
    if (unlocked || gamepadUnlockAttempted) return;

    const gamepad = Array.from(navigator.getGamepads?.() ?? []).find(
      (pad) => pad?.connected && pad.buttons.some((button) => button.pressed),
    );
    if (!gamepad) return;
    gamepadUnlockAttempted = true;
    unlockAudio();
  }

  function update(delta, state = {}) {
    pollGamepadUnlock();
    if (!unlocked || muted) {
      orbHum?.update(delta, { orbCollected: false, guidanceAlignment: 0 });
      return;
    }

    orbHum?.update(delta, state);
  }

  for (const eventName of ["pointerdown", "click", "keydown"]) {
    window.addEventListener(eventName, unlockAudio, { once: true });
  }

  window.addEventListener("keydown", (event) => {
    if (event.code === "KeyM") toggleMute();
  });

  return {
    initAudio,
    unlockAudio,
    setMasterVolume,
    update,
    playCharmAppear,
    playCharmPickup,
    playOwlAppear,
    playClearingReached,
    get muted() {
      return muted;
    },
    get unlocked() {
      return unlocked;
    },
  };
}
