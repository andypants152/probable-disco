import * as THREE from "three";

import {
  chunkSize,
  groundStep,
  maxGroundBlocks,
} from "../config.js";
import {
  clamp,
  hash,
  noise2D,
} from "./noise.js";
import { getMoonClearingInfluence } from "./landmarks.js";

const groundPalettes = [
  new THREE.Color(0x395638),
  new THREE.Color(0x4d613c),
  new THREE.Color(0x5a4a33),
  new THREE.Color(0x31433b),
];
const moonlitGround = new THREE.Color(0x60756d);
const groundColor = new THREE.Color();

export function sampleGroundHeight(worldX, worldZ) {
  return 0;
}

function makeGroundTopGeometry() {
  const geometry = new THREE.BufferGeometry();
  const subdivisions = 5;
  const cell = 1 / subdivisions;
  const halfTile = cell * 0.39;
  const positions = [];
  const normals = [];
  const colors = [];
  const microPositions = [];

  for (let u = 0; u < subdivisions; u += 1) {
    for (let v = 0; v < subdivisions; v += 1) {
      const centerU = -0.5 + cell * (u + 0.5);
      const centerV = -0.5 + cell * (v + 0.5);
      const pixelNoise = hash(u * 17 + 31, v * 29 + 37) * 0.16 - 0.08;
      const shade = clamp(1.2 + pixelNoise, 0.48, 1.24);
      const corners = [
        [-1, -1],
        [1, -1],
        [1, 1],
        [-1, -1],
        [1, 1],
        [-1, 1],
      ];

      for (const [cornerU, cornerV] of corners) {
        positions.push(centerU + cornerU * halfTile, 0, centerV + cornerV * halfTile);
        normals.push(0, 1, 0);
        colors.push(shade, shade, shade);
        microPositions.push(cornerU * 0.5, 0.5, cornerV * 0.5);
      }
    }
  }

  geometry.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  geometry.setAttribute("normal", new THREE.Float32BufferAttribute(normals, 3));
  geometry.setAttribute("color", new THREE.Float32BufferAttribute(colors, 3));
  geometry.setAttribute("microPosition", new THREE.Float32BufferAttribute(microPositions, 3));
  geometry.computeBoundingSphere();
  return geometry;
}

export function createGroundLayer({ material }) {
  const mesh = new THREE.InstancedMesh(makeGroundTopGeometry(), material, maxGroundBlocks);
  mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
  return mesh;
}

export function populateGroundLayer({ chunk, chunkX, chunkZ, dummy }) {
  let count = 0;

  for (let x = -chunkSize / 2 + groundStep / 2; x < chunkSize / 2; x += groundStep) {
    for (let z = -chunkSize / 2 + groundStep / 2; z < chunkSize / 2; z += groundStep) {
      if (count >= chunk.ground.instanceMatrix.count) break;

      const worldX = chunkX * chunkSize + x;
      const worldZ = chunkZ * chunkSize + z;
      const top = sampleGroundHeight(worldX, worldZ);
      const colorSeed = hash(Math.floor(worldX / groundStep), Math.floor(worldZ / groundStep));
      const damp = noise2D(worldX * 0.06 - 9, worldZ * 0.06 + 13) > 0.38;
      const paletteIndex = damp ? 3 : Math.floor(colorSeed * 3);
      const clearing = getMoonClearingInfluence(worldX, worldZ);
      groundColor.copy(groundPalettes[paletteIndex]).lerp(moonlitGround, clearing * 0.45);

      dummy.position.set(x, top, z);
      dummy.rotation.set(0, 0, 0);
      dummy.scale.set(groundStep * 1.02, 1, groundStep * 1.02);
      dummy.updateMatrix();
      chunk.ground.setMatrixAt(count, dummy.matrix);
      chunk.ground.setColorAt(count, groundColor);
      count += 1;
    }
  }

  chunk.ground.count = count;
  chunk.ground.instanceMatrix.needsUpdate = true;
  if (chunk.ground.instanceColor) chunk.ground.instanceColor.needsUpdate = true;
}
