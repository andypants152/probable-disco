export const sceneBackgroundColor = 0x1d2b27;
export const fogDensity = 0.014;

export const cameraConfig = {
  fov: 70,
  near: 0.1,
  far: 600,
  position: [0, 2.2, 0],
};

export const rendererConfig = {
  antialias: false,
  maxPixelRatio: 2,
};

export const lightConfig = {
  moonSky: 0xe4fff7,
  moonGround: 0x506244,
  moonIntensity: 2.25,
  glowColor: 0xf2fff8,
  glowIntensity: 1.55,
  glowPosition: [-40, 80, -30],
};

export const groundColor = 0x385338;
export const trunkColor = 0x8a5d3c;
export const leafColor = 0x347a49;

export const chunkSize = 48;
export const chunkRadius = 2;
export const chunkSpan = chunkRadius * 2 + 1;
export const treeStep = 8;
export const maxTreesPerChunk = (chunkSize / treeStep) ** 2;
export const maxTrunkBlocks = maxTreesPerChunk * 11;
export const maxLeafBlocks = maxTreesPerChunk * 96;

export const groundStep = 4;
export const maxGroundBlocks = (chunkSize / groundStep) ** 2;
export const maxRockBlocks = 28;
export const maxStumpBlocks = 12;
export const maxLogBlocks = 12;
export const maxMushroomBlocks = 24;

export const walkSpeed = 10;
export const stickDeadzone = 0.16;
export const stickLookSpeed = 2.8;

export const foxConfig = {
  moveSpeed: 7,
  turnSpeed: 12,
  collisionRadius: 0.42,
  headCollisionRadius: 0.32,
  tailCollisionRadius: 0.34,
};

export const thirdPersonCameraConfig = {
  distance: 5.5,
  height: 2.25,
  targetHeight: 0.75,
  mouseLookSpeed: 0.002,
  smoothness: 10,
  minPitch: -0.35,
  maxPitch: 0.75,
};

export const owlConfig = {
  position: [0, 2.25, -7],
  rotationY: Math.PI,
  scale: 1,
  triggerRadius: 4,
  vanishDelay: 5,
  eyeGlowColor: 0x8ffff2,
};

export const charmConfig = {
  pickupRadius: 1.1,
  orbitRadius: 0.62,
  glowColor: 0x7fffee,
  dimIntensity: 0.35,
  brightIntensity: 1.45,
};

export const moonClearingConfig = {
  position: [42, 0, -104],
  radius: 16,
  edgeNoise: 3.5,
  owlOffset: [-7, 3.2, 5],
  triggerRadius: 7,
  glowColor: 0x9fffe6,
};

export const audioConfig = {
  masterVolume: 0.18,
  humBaseFrequency: 210,
  humMaxGain: 0.1,
  humPitchRange: 0.28,
  humTextThreshold: 0.68,
  warmHumTextThreshold: 0.9,
};
