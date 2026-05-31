import * as THREE from "three";

import { charmConfig, moonClearingConfig } from "../config.js";

const targetDirection = new THREE.Vector3();
const facingDirection = new THREE.Vector3();
const orbitOffset = new THREE.Vector3();

function makeWispTexture() {
  const canvas = document.createElement("canvas");
  canvas.width = 64;
  canvas.height = 64;
  const context = canvas.getContext("2d");
  const gradient = context.createRadialGradient(32, 32, 2, 32, 32, 30);
  gradient.addColorStop(0, "rgba(230, 255, 248, 0.95)");
  gradient.addColorStop(0.35, "rgba(127, 255, 238, 0.55)");
  gradient.addColorStop(1, "rgba(127, 255, 238, 0)");
  context.fillStyle = gradient;
  context.fillRect(0, 0, 64, 64);
  return new THREE.CanvasTexture(canvas);
}

export function createCharm() {
  const group = new THREE.Group();
  group.visible = false;

  const wispMaterial = new THREE.SpriteMaterial({
    map: makeWispTexture(),
    color: charmConfig.glowColor,
    transparent: true,
    opacity: 0.5,
    depthWrite: false,
  });
  const wisp = new THREE.Sprite(wispMaterial);
  wisp.scale.set(0.62, 0.62, 0.62);
  group.add(wisp);

  let elapsed = 0;
  let state = "hidden";
  let glowAmount = charmConfig.dimIntensity;
  let guidanceAlignment = 0;
  let baseY = 0;

  function appear(position) {
    group.position.copy(position);
    baseY = position.y;
    group.visible = true;
    state = "available";
  }

  function collect() {
    state = "collected";
  }

  function update(delta, fox, landmarkPosition, cameraYaw) {
    if (!group.visible) return;

    elapsed += delta;

    if (state === "available") {
      const pulse = 0.7 + Math.sin(elapsed * 4.2) * 0.3;
      group.position.y = baseY + Math.sin(elapsed * 2.4) * 0.12;
      wispMaterial.opacity = 0.28 + pulse * 0.18;
      wisp.scale.setScalar(0.52 + pulse * 0.08);
      return;
    }

    if (state === "collected") {
      targetDirection.copy(landmarkPosition).sub(fox.position);
      targetDirection.y = 0;
      targetDirection.normalize();
      facingDirection.set(-Math.sin(fox.rotation.y), 0, -Math.cos(fox.rotation.y));

      const distanceToClearing = fox.position.distanceTo(landmarkPosition);
      const inClearing = distanceToClearing < moonClearingConfig.radius;
      const alignment = inClearing ? 0.28 : (facingDirection.dot(targetDirection) + 1) * 0.5;
      guidanceAlignment = alignment;
      glowAmount = THREE.MathUtils.lerp(
        glowAmount,
        THREE.MathUtils.lerp(charmConfig.dimIntensity, charmConfig.brightIntensity, alignment),
        1 - Math.exp(-delta * 5),
      );

      orbitOffset.set(
        Math.cos(elapsed * 1.4) * 0.1,
        0.66 + Math.sin(elapsed * 2.2) * 0.06,
        0,
      );
      const sideX = Math.cos(cameraYaw) * charmConfig.orbitRadius;
      const sideZ = -Math.sin(cameraYaw) * charmConfig.orbitRadius;
      group.position.set(
        fox.position.x + sideX + orbitOffset.x,
        fox.position.y + orbitOffset.y,
        fox.position.z + sideZ,
      );
      wispMaterial.opacity = 0.22 + glowAmount * 0.2;
      wispMaterial.color.set(charmConfig.glowColor).multiplyScalar(glowAmount);
      wisp.scale.setScalar(0.46 + glowAmount * 0.08);
    }
  }

  return {
    group,
    appear,
    collect,
    update,
    get state() {
      return state;
    },
    get isAvailable() {
      return state === "available";
    },
    get isCollected() {
      return state === "collected";
    },
    get guidanceAlignment() {
      return guidanceAlignment;
    },
  };
}
