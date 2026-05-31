import * as THREE from "three";

import { moonClearingConfig } from "../config.js";
import { noise2D } from "./noise.js";

export function getMoonClearingInfluence(worldX, worldZ) {
  const [centerX, , centerZ] = moonClearingConfig.position;
  const edgeNoise =
    noise2D(worldX * 0.035 + 19, worldZ * 0.035 - 23) * moonClearingConfig.edgeNoise;
  const radius = moonClearingConfig.radius + edgeNoise;
  const distance = Math.hypot(worldX - centerX, worldZ - centerZ);

  return THREE.MathUtils.clamp((radius - distance) / 6, 0, 1);
}

export function getMoonClearingRingInfluence(worldX, worldZ) {
  const [centerX, , centerZ] = moonClearingConfig.position;
  const distance = Math.hypot(worldX - centerX, worldZ - centerZ);
  const ringDistance = Math.abs(distance - moonClearingConfig.radius);

  return THREE.MathUtils.clamp(1 - ringDistance / 7, 0, 1);
}

export function createMoonClearingLandmark() {
  const group = new THREE.Group();
  group.position.set(...moonClearingConfig.position);

  function update() {}

  return {
    group,
    update,
    position: group.position,
  };
}
