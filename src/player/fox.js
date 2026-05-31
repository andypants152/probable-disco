import * as THREE from "three";

import { foxConfig } from "../config.js";
import { makePixelCube } from "../graphics/voxelCube.js";
import { makeVoxelMaterial } from "../graphics/voxelMaterial.js";

const forwardVector = new THREE.Vector3();
const rightVector = new THREE.Vector3();
const moveVector = new THREE.Vector3();
const collisionPoint = new THREE.Vector3();
const resolvedPoint = new THREE.Vector3();

function makeVoxelPart({
  size,
  position,
  geometry,
  material,
}) {
  const mesh = new THREE.Mesh(geometry, material);
  mesh.position.set(...position);
  mesh.scale.set(...size);
  return mesh;
}

function makePlainPart({
  size,
  position,
  color,
}) {
  const mesh = new THREE.Mesh(
    new THREE.BoxGeometry(...size),
    new THREE.MeshBasicMaterial({ color, fog: true }),
  );
  mesh.position.set(...position);
  return mesh;
}

export function createFoxPlayer() {
  const root = new THREE.Group();
  root.position.set(0, 0, 0);

  const visual = new THREE.Group();
  root.add(visual);

  const orange = 0xd46f25;
  const white = 0xf1ead7;
  const dark = 0x211915;
  // The fox uses large body-part voxels with lower surface subdivision so it matches tree scale language.
  const foxCube = makePixelCube({ subdivisions: 3 });
  const materials = {
    orange: makeVoxelMaterial({ baseColor: orange }),
    white: makeVoxelMaterial({ baseColor: white }),
    dark: makeVoxelMaterial({ baseColor: dark }),
  };

  const body = makeVoxelPart({
    size: [0.86, 0.42, 1.05],
    position: [0, 0.43, 0],
    geometry: foxCube,
    material: materials.orange,
  });
  const chest = makeVoxelPart({
    size: [0.38, 0.26, 0.08],
    position: [0, 0.43, -0.56],
    geometry: foxCube,
    material: materials.white,
  });
  const head = makeVoxelPart({
    size: [0.56, 0.44, 0.48],
    position: [0, 0.8, -0.68],
    geometry: foxCube,
    material: materials.orange,
  });
  const muzzle = makeVoxelPart({
    size: [0.32, 0.2, 0.24],
    position: [0, 0.73, -1.04],
    geometry: foxCube,
    material: materials.white,
  });

  const leftEye = makePlainPart({
    size: [0.055, 0.055, 0.025],
    position: [-0.14, 0.88, -0.93],
    color: dark,
  });
  const rightEye = makePlainPart({
    size: [0.055, 0.055, 0.025],
    position: [0.14, 0.88, -0.93],
    color: dark,
  });

  const leftEar = makeVoxelPart({
    size: [0.18, 0.34, 0.16],
    position: [-0.2, 1.12, -0.68],
    geometry: foxCube,
    material: materials.dark,
  });
  leftEar.rotation.z = -0.24;
  const rightEar = makeVoxelPart({
    size: [0.18, 0.34, 0.16],
    position: [0.2, 1.12, -0.68],
    geometry: foxCube,
    material: materials.dark,
  });
  rightEar.rotation.z = 0.24;

  const legs = [
    makeVoxelPart({ size: [0.16, 0.36, 0.18], position: [-0.28, 0.18, -0.34], geometry: foxCube, material: materials.dark }),
    makeVoxelPart({ size: [0.16, 0.36, 0.18], position: [0.28, 0.18, -0.34], geometry: foxCube, material: materials.dark }),
    makeVoxelPart({ size: [0.16, 0.36, 0.18], position: [-0.28, 0.18, 0.38], geometry: foxCube, material: materials.dark }),
    makeVoxelPart({ size: [0.16, 0.36, 0.18], position: [0.28, 0.18, 0.38], geometry: foxCube, material: materials.dark }),
  ];

  const tailPivot = new THREE.Group();
  tailPivot.position.set(0, 0.5, 0.56);
  visual.add(tailPivot);

  const tailSegments = [
    makeVoxelPart({ size: [0.36, 0.34, 0.44], position: [0, 0.05, 0.24], geometry: foxCube, material: materials.orange }),
    makeVoxelPart({ size: [0.31, 0.3, 0.4], position: [0, 0.1, 0.6], geometry: foxCube, material: materials.orange }),
    makeVoxelPart({ size: [0.26, 0.25, 0.34], position: [0, 0.14, 0.92], geometry: foxCube, material: materials.orange }),
    makeVoxelPart({ size: [0.24, 0.23, 0.24], position: [0, 0.17, 1.18], geometry: foxCube, material: materials.white }),
  ];
  tailPivot.add(...tailSegments);

  visual.add(body, chest, head, muzzle, leftEar, rightEar, ...legs, leftEye, rightEye);

  const baseLegY = legs.map((leg) => leg.position.y);
  let elapsed = 0;

  function resolveFoxCollisions(resolveCollisions) {
    if (!resolveCollisions) return;

    const colliders = [
      { x: 0, z: -0.82, radius: foxConfig.headCollisionRadius },
      { x: 0, z: -0.05, radius: foxConfig.collisionRadius },
      { x: 0, z: 1.1, radius: foxConfig.tailCollisionRadius },
    ];

    // A few broad samples match the simplified fox silhouette without expensive mesh collision.
    for (let pass = 0; pass < 2; pass += 1) {
      for (const collider of colliders) {
        const sin = Math.sin(root.rotation.y);
        const cos = Math.cos(root.rotation.y);
        collisionPoint.set(
          root.position.x + collider.x * cos + collider.z * sin,
          root.position.y,
          root.position.z - collider.x * sin + collider.z * cos,
        );
        resolvedPoint.copy(collisionPoint);
        resolveCollisions(resolvedPoint, collider.radius);
        root.position.x += resolvedPoint.x - collisionPoint.x;
        root.position.z += resolvedPoint.z - collisionPoint.z;
      }
    }
  }

  function update(delta, input, cameraYaw, resolveCollisions) {
    elapsed += delta;

    forwardVector.set(-Math.sin(cameraYaw), 0, -Math.cos(cameraYaw));
    rightVector.set(Math.cos(cameraYaw), 0, -Math.sin(cameraYaw));
    moveVector
      .copy(forwardVector)
      .multiplyScalar(input.forward)
      .addScaledVector(rightVector, input.strafe);

    const moving = moveVector.lengthSq() > 0.0001;
    if (moving) {
      moveVector.normalize();
      root.position.addScaledVector(moveVector, foxConfig.moveSpeed * delta);

      const targetYaw = Math.atan2(-moveVector.x, -moveVector.z);
      const yawDelta = Math.atan2(
        Math.sin(targetYaw - root.rotation.y),
        Math.cos(targetYaw - root.rotation.y),
      );
      root.rotation.y += yawDelta * Math.min(1, foxConfig.turnSpeed * delta);
    }

    resolveFoxCollisions(resolveCollisions);

    const walkAmount = moving ? 1 : 0;
    const stride = elapsed * 11;
    visual.position.y = Math.sin(stride) * 0.025 * walkAmount;

    legs.forEach((leg, index) => {
      const phase = index % 2 === 0 ? 0 : Math.PI;
      leg.rotation.x = Math.sin(stride + phase) * 0.32 * walkAmount;
      leg.position.y = baseLegY[index] + Math.max(0, Math.sin(stride + phase)) * 0.04 * walkAmount;
    });

    tailPivot.rotation.x = -0.48 + Math.sin(elapsed * (moving ? 7 : 2.2)) * (moving ? 0.1 : 0.06);
    tailPivot.rotation.y = Math.sin(elapsed * (moving ? 9 : 1.8)) * (moving ? 0.14 : 0.08);
  }

  return {
    group: root,
    update,
    get position() {
      return root.position;
    },
  };
}
