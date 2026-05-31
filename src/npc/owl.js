import * as THREE from "three";

import { owlConfig } from "../config.js";
import { makePixelCube } from "../graphics/voxelCube.js";
import { makeVoxelMaterial } from "../graphics/voxelMaterial.js";

const lookDirection = new THREE.Vector3();
const warmColor = new THREE.Color(0xffdf76);
const glowColor = new THREE.Color(owlConfig.eyeGlowColor);

function makeVoxelPart({
  geometry,
  material,
  size,
  position,
}) {
  const mesh = new THREE.Mesh(geometry, material);
  mesh.scale.set(...size);
  mesh.position.set(...position);
  return mesh;
}

function makePlainPart({
  color,
  emissive,
  size,
  position,
}) {
  const material = new THREE.MeshBasicMaterial({
    color,
    transparent: true,
  });
  if (emissive) {
    material.color.set(emissive);
  }

  const mesh = new THREE.Mesh(new THREE.BoxGeometry(...size), material);
  mesh.position.set(...position);
  return mesh;
}

export function createVoxelOwl({ dialogueElement } = {}) {
  const root = new THREE.Group();
  const bodyPivot = new THREE.Group();
  const headPivot = new THREE.Group();
  const perch = new THREE.Group();
  root.position.set(...owlConfig.position);
  root.rotation.y = owlConfig.rotationY;
  root.scale.setScalar(owlConfig.scale);
  root.add(perch, bodyPivot);
  bodyPivot.add(headPivot);

  const owlCube = makePixelCube({ subdivisions: 3 });
  const materials = {
    body: makeVoxelMaterial({ baseColor: 0x5f4b3a }),
    wing: makeVoxelMaterial({ baseColor: 0x3f3836 }),
    face: makeVoxelMaterial({ baseColor: 0xd8ceb2 }),
    beak: makeVoxelMaterial({ baseColor: 0xffb943 }),
    perch: makeVoxelMaterial({ baseColor: 0x8a5d3c }),
  };

  const perchPost = makeVoxelPart({
    geometry: owlCube,
    material: materials.perch,
    size: [0.42, 1.8, 0.42],
    position: [0, -1.05, 0],
  });
  const perchBranch = makeVoxelPart({
    geometry: owlCube,
    material: materials.perch,
    size: [1.5, 0.24, 0.26],
    position: [0, -0.18, 0],
  });
  perchBranch.rotation.z = 0.08;
  perch.add(perchPost, perchBranch);

  const body = makeVoxelPart({
    geometry: owlCube,
    material: materials.body,
    size: [0.82, 1.02, 0.62],
    position: [0, 0.15, 0],
  });
  const leftWing = makeVoxelPart({
    geometry: owlCube,
    material: materials.wing,
    size: [0.26, 0.82, 0.54],
    position: [-0.5, 0.1, 0.02],
  });
  const rightWing = makeVoxelPart({
    geometry: owlCube,
    material: materials.wing,
    size: [0.26, 0.82, 0.54],
    position: [0.5, 0.1, 0.02],
  });
  bodyPivot.add(body, leftWing, rightWing);

  headPivot.position.set(0, 0.78, -0.03);
  const head = makeVoxelPart({
    geometry: owlCube,
    material: materials.body,
    size: [0.86, 0.62, 0.58],
    position: [0, 0, 0],
  });
  const face = makeVoxelPart({
    geometry: owlCube,
    material: materials.face,
    size: [0.68, 0.34, 0.08],
    position: [0, -0.02, -0.32],
  });
  const beak = makeVoxelPart({
    geometry: owlCube,
    material: materials.beak,
    size: [0.14, 0.12, 0.12],
    position: [0, -0.12, -0.42],
  });
  beak.rotation.x = 0.18;

  const leftEye = makePlainPart({
    color: owlConfig.eyeGlowColor,
    emissive: owlConfig.eyeGlowColor,
    size: [0.13, 0.13, 0.05],
    position: [-0.19, 0.04, -0.38],
  });
  const rightEye = makePlainPart({
    color: owlConfig.eyeGlowColor,
    emissive: owlConfig.eyeGlowColor,
    size: [0.13, 0.13, 0.05],
    position: [0.19, 0.04, -0.38],
  });

  const leftTuft = makeVoxelPart({
    geometry: owlCube,
    material: materials.wing,
    size: [0.16, 0.34, 0.16],
    position: [-0.3, 0.42, -0.02],
  });
  leftTuft.rotation.z = -0.32;
  const rightTuft = makeVoxelPart({
    geometry: owlCube,
    material: materials.wing,
    size: [0.16, 0.34, 0.16],
    position: [0.3, 0.42, -0.02],
  });
  rightTuft.rotation.z = 0.32;

  const leftFoot = makeVoxelPart({
    geometry: owlCube,
    material: materials.beak,
    size: [0.18, 0.08, 0.18],
    position: [-0.18, -0.42, -0.08],
  });
  const rightFoot = makeVoxelPart({
    geometry: owlCube,
    material: materials.beak,
    size: [0.18, 0.08, 0.18],
    position: [0.18, -0.42, -0.08],
  });
  bodyPivot.add(leftFoot, rightFoot);
  headPivot.add(head, face, beak, leftEye, rightEye, leftTuft, rightTuft);

  let elapsed = 0;
  let state = "watching";
  let hasSpoken = false;
  let vanishTimer = 0;
  let dialogueTimer = 0;
  let blinkTimer = 1.8;
  let wingTwitchTimer = 4.5;

  function showOwlLine(line) {
    if (!dialogueElement) {
      console.log(line);
      return;
    }

    dialogueElement.textContent = line;
    dialogueElement.hidden = false;
    dialogueTimer = 3.6;
  }

  function speak(line) {
    showOwlLine(line);
  }

  function vanish() {
    state = "vanishing";
    vanishTimer = 0;
  }

  let baseRotationY = owlConfig.rotationY;

  function appearAt(position, rotationY = owlConfig.rotationY, resetIntro = false) {
    root.position.copy(position);
    baseRotationY = rotationY;
    root.rotation.y = baseRotationY;
    root.visible = true;
    root.scale.setScalar(owlConfig.scale);
    bodyPivot.visible = true;
    bodyPivot.position.set(0, 0, 0);
    bodyPivot.scale.set(1, 1, 1);
    state = "watching";
    hasSpoken = !resetIntro;
    vanishTimer = 0;
  }

  function guideToward(position) {
    // TODO: Use this hook when quest/path guidance exists.
    lookDirection.copy(position).sub(root.position);
  }

  function update(delta, foxPosition) {
    if (!root.visible) return;

    elapsed += delta;
    if (dialogueTimer > 0) {
      dialogueTimer -= delta;
      if (dialogueTimer <= 0 && dialogueElement) dialogueElement.hidden = true;
    }

    lookDirection.copy(foxPosition).sub(root.position);
    const distanceToFox = Math.hypot(lookDirection.x, lookDirection.z);
    const targetYaw = Math.atan2(-lookDirection.x, -lookDirection.z);
    const yawDelta = Math.atan2(
      Math.sin(targetYaw - baseRotationY),
      Math.cos(targetYaw - baseRotationY),
    );
    root.rotation.y = baseRotationY;
    headPivot.rotation.y = THREE.MathUtils.clamp(yawDelta, -Math.PI * 0.92, Math.PI * 0.92);
    headPivot.rotation.z = Math.sin(elapsed * 1.4) * 0.06;

    if (state !== "vanishing") {
      bodyPivot.position.y = Math.sin(elapsed * 1.8) * 0.045;
    }
    const eyePulse = 0.65 + Math.sin(elapsed * 3.2) * 0.35;
    leftEye.material.color.copy(glowColor).lerp(warmColor, eyePulse * 0.18);
    rightEye.material.color.copy(glowColor).lerp(warmColor, eyePulse * 0.18);

    blinkTimer -= delta;
    const blinking = blinkTimer < 0.12;
    leftEye.scale.y = blinking ? 0.12 : 1;
    rightEye.scale.y = blinking ? 0.12 : 1;
    if (blinkTimer <= 0) {
      blinkTimer = 2.4 + (Math.sin(elapsed * 1.7) + 1) * 1.4;
    }

    wingTwitchTimer -= delta;
    const twitch = wingTwitchTimer < 0.18 ? Math.sin(wingTwitchTimer * 55) * 0.08 : 0;
    const vanishFlap = state === "vanishing" ? Math.sin(elapsed * 18) * 0.55 : 0;
    leftWing.rotation.z = -0.08 + twitch - Math.abs(vanishFlap);
    rightWing.rotation.z = 0.08 - twitch + Math.abs(vanishFlap);
    leftWing.rotation.x = state === "vanishing" ? Math.sin(elapsed * 18) * 0.18 : 0;
    rightWing.rotation.x = state === "vanishing" ? Math.sin(elapsed * 18 + Math.PI) * 0.18 : 0;
    if (wingTwitchTimer <= 0) {
      wingTwitchTimer = 5.5 + (Math.sin(elapsed * 0.9) + 1) * 3;
    }

    if (!hasSpoken && distanceToFox < owlConfig.triggerRadius) {
      hasSpoken = true;
      speak("Oh good, you're awake.");
      vanishTimer = owlConfig.vanishDelay;
    }

    if (hasSpoken && state === "watching" && vanishTimer > 0) {
      vanishTimer -= delta;
      if (vanishTimer <= 0) vanish();
    }

    if (state === "vanishing") {
      vanishTimer += delta;
      bodyPivot.position.y += delta * 0.45;
      bodyPivot.position.z += delta * 0.22;
      bodyPivot.scale.multiplyScalar(Math.pow(0.88, delta));
      const dim = Math.max(0, 1 - vanishTimer * 0.45);
      leftEye.material.color.copy(glowColor).multiplyScalar(dim);
      rightEye.material.color.copy(glowColor).multiplyScalar(dim);
      if (vanishTimer > 2.4) bodyPivot.visible = false;
    }
  }

  return {
    group: root,
    update,
    speak,
    appearAt,
    vanish,
    guideToward,
    get state() {
      return state;
    },
    get hasSpoken() {
      return hasSpoken;
    },
  };
}
