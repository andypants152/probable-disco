import { audioConfig } from "../config.js";

export function createOrbHum(audioContext, destination) {
  const filter = audioContext.createBiquadFilter();
  const humGain = audioContext.createGain();
  const primary = audioContext.createOscillator();
  const overtone = audioContext.createOscillator();
  const overtoneGain = audioContext.createGain();

  filter.type = "lowpass";
  filter.frequency.value = 950;
  humGain.gain.value = 0;
  overtoneGain.gain.value = 0.26;
  primary.type = "sine";
  overtone.type = "triangle";
  primary.frequency.value = audioConfig.humBaseFrequency;
  overtone.frequency.value = audioConfig.humBaseFrequency * 1.5;

  primary.connect(filter);
  overtone.connect(overtoneGain);
  overtoneGain.connect(filter);
  filter.connect(humGain);
  humGain.connect(destination);
  primary.start();
  overtone.start();

  let smoothedAlignment = 0;
  let elapsed = 0;

  function update(delta, { orbCollected = false, guidanceAlignment = 0 } = {}) {
    elapsed += delta;
    const targetAlignment = orbCollected ? guidanceAlignment : 0;
    smoothedAlignment += (targetAlignment - smoothedAlignment) * (1 - Math.exp(-delta * 4));

    const pulseRate = 0.8 + smoothedAlignment * 1.8;
    const pulse = 0.72 + Math.sin(elapsed * pulseRate * Math.PI * 2) * 0.28;
    const gain = audioConfig.humMaxGain * smoothedAlignment * pulse;
    const frequency = audioConfig.humBaseFrequency * (1 + audioConfig.humPitchRange * smoothedAlignment);

    humGain.gain.setTargetAtTime(gain, audioContext.currentTime, 0.08);
    primary.frequency.setTargetAtTime(frequency, audioContext.currentTime, 0.08);
    overtone.frequency.setTargetAtTime(frequency * 1.5, audioContext.currentTime, 0.08);
    filter.frequency.setTargetAtTime(850 + smoothedAlignment * 420, audioContext.currentTime, 0.12);
  }

  function stop() {
    humGain.gain.setTargetAtTime(0, audioContext.currentTime, 0.04);
  }

  return {
    update,
    stop,
  };
}
