import * as THREE from "three";

import {
  chunkSize,
  maxLogBlocks,
  maxMushroomBlocks,
  maxRockBlocks,
  maxStumpBlocks,
} from "../config.js";
import { hash, noise2D } from "./noise.js";
import { sampleGroundHeight } from "./ground.js";
import { getMoonClearingInfluence } from "./landmarks.js";

const rockColors = [new THREE.Color(0x737b70), new THREE.Color(0x5b665c), new THREE.Color(0x718268)];
const stumpColors = [new THREE.Color(0x8a5d3c), new THREE.Color(0x654229)];
const mushroomColors = [new THREE.Color(0xd9ecff), new THREE.Color(0xb86452), new THREE.Color(0x8ffff2)];
const mushroomStem = new THREE.Color(0xf0ddb8);

export function createForestObjectLayers({ cubeGeometry, material }) {
  const rocks = new THREE.InstancedMesh(cubeGeometry, material, maxRockBlocks);
  const stumps = new THREE.InstancedMesh(cubeGeometry, material, maxStumpBlocks);
  const logs = new THREE.InstancedMesh(cubeGeometry, material, maxLogBlocks);
  const mushrooms = new THREE.InstancedMesh(cubeGeometry, material, maxMushroomBlocks);

  for (const mesh of [rocks, stumps, logs, mushrooms]) {
    mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
  }

  return {
    rocks,
    stumps,
    logs,
    mushrooms,
  };
}

function placeBlock(mesh, count, dummy, color, x, y, z, scale, rotationY = 0) {
  if (count >= mesh.instanceMatrix.count) return count;

  dummy.position.set(x, y, z);
  dummy.rotation.set(0, rotationY, 0);
  dummy.scale.set(...scale);
  dummy.updateMatrix();
  mesh.setMatrixAt(count, dummy.matrix);
  mesh.setColorAt(count, color);
  return count + 1;
}

function placeRock({ chunk, dummy, localX, localZ, worldX, worldZ, seed }) {
  let count = chunk.objects.rocks.count;
  const blocks = 1 + Math.floor(hash(worldX + 5, worldZ - 7) * 4);

  for (let block = 0; block < blocks; block += 1) {
    const offsetX = (hash(worldX + block * 11, worldZ + 3) - 0.5) * 0.9;
    const offsetZ = (hash(worldX - 2, worldZ + block * 13) - 0.5) * 0.9;
    const blockWorldX = worldX + offsetX;
    const blockWorldZ = worldZ + offsetZ;
    const y = sampleGroundHeight(blockWorldX, blockWorldZ) + 0.08 + block * 0.05;
    const size = 0.45 + hash(worldX + block * 17, worldZ - block * 19) * 0.45;
    count = placeBlock(
      chunk.objects.rocks,
      count,
      dummy,
      rockColors[(Math.floor(seed * 10) + block) % rockColors.length],
      localX + offsetX,
      y,
      localZ + offsetZ,
      [size, size * 0.55, size],
      seed * Math.PI,
    );
  }

  chunk.objects.rocks.count = count;
}

function placeStump({ chunk, dummy, localX, localZ, worldX, worldZ, seed }) {
  let count = chunk.objects.stumps.count;
  const y = sampleGroundHeight(worldX, worldZ);
  const height = 0.45 + hash(worldX + 23, worldZ - 17) * 0.35;

  count = placeBlock(
    chunk.objects.stumps,
    count,
    dummy,
    stumpColors[0],
    localX,
    y + height * 0.5,
    localZ,
    [0.58, height, 0.58],
    seed * Math.PI,
  );
  count = placeBlock(
    chunk.objects.stumps,
    count,
    dummy,
    stumpColors[1],
    localX,
    y + height + 0.04,
    localZ,
    [0.5, 0.08, 0.5],
    seed * Math.PI,
  );

  chunk.objects.stumps.count = count;
}

function placeLog({ chunk, dummy, localX, localZ, worldX, worldZ, seed }) {
  let count = chunk.objects.logs.count;
  const length = 3 + Math.floor(hash(worldX - 31, worldZ + 29) * 4);
  const rotationY = seed * Math.PI * 2;
  const stepX = Math.sin(rotationY) * 0.64;
  const stepZ = Math.cos(rotationY) * 0.64;

  for (let block = 0; block < length; block += 1) {
    const centered = block - (length - 1) / 2;
    const offsetX = stepX * centered;
    const offsetZ = stepZ * centered;
    const blockWorldX = worldX + offsetX;
    const blockWorldZ = worldZ + offsetZ;
    const y = sampleGroundHeight(blockWorldX, blockWorldZ) + 0.25;
    count = placeBlock(
      chunk.objects.logs,
      count,
      dummy,
      stumpColors[0],
      localX + offsetX,
      y,
      localZ + offsetZ,
      [0.55, 0.5, 0.62],
      rotationY,
    );
  }

  if (hash(worldX + 41, worldZ - 41) > 0.55) {
    count = placeBlock(
      chunk.objects.logs,
      count,
      dummy,
      stumpColors[0],
      localX + stepZ * 0.6,
      sampleGroundHeight(worldX, worldZ) + 0.55,
      localZ - stepX * 0.6,
      [0.28, 0.45, 0.28],
      rotationY + Math.PI * 0.5,
    );
  }

  chunk.objects.logs.count = count;
}

function placeMushrooms({ chunk, dummy, localX, localZ, worldX, worldZ, seed }) {
  let count = chunk.objects.mushrooms.count;
  const clusterSize = 2 + Math.floor(hash(worldX + 61, worldZ - 53) * 4);

  for (let mushroom = 0; mushroom < clusterSize; mushroom += 1) {
    const offsetX = (hash(worldX + mushroom * 17, worldZ + 71) - 0.5) * 1.8;
    const offsetZ = (hash(worldX - 67, worldZ + mushroom * 19) - 0.5) * 1.8;
    const blockWorldX = worldX + offsetX;
    const blockWorldZ = worldZ + offsetZ;
    const y = sampleGroundHeight(blockWorldX, blockWorldZ);
    const capColor = mushroomColors[(Math.floor(seed * 100) + mushroom) % mushroomColors.length];

    count = placeBlock(
      chunk.objects.mushrooms,
      count,
      dummy,
      mushroomStem,
      localX + offsetX,
      y + 0.14,
      localZ + offsetZ,
      [0.14, 0.28, 0.14],
    );
    count = placeBlock(
      chunk.objects.mushrooms,
      count,
      dummy,
      capColor,
      localX + offsetX,
      y + 0.34,
      localZ + offsetZ,
      [0.34, 0.16, 0.34],
      seed * Math.PI,
    );
  }

  chunk.objects.mushrooms.count = count;
}

export function populateForestObjects({ chunk, chunkX, chunkZ, dummy }) {
  for (const mesh of Object.values(chunk.objects)) {
    mesh.count = 0;
  }

  // TODO: When player terrain-following exists, sample the same height field for movement.
  for (let x = -chunkSize / 2 + 4; x < chunkSize / 2 - 4; x += 6) {
    for (let z = -chunkSize / 2 + 4; z < chunkSize / 2 - 4; z += 6) {
      const worldX = chunkX * chunkSize + x;
      const worldZ = chunkZ * chunkSize + z;
      const seed = hash(worldX, worldZ);
      const offsetX = noise2D(worldX * 0.17, worldZ * 0.17) * 1.8;
      const offsetZ = noise2D((worldX + 300) * 0.17, (worldZ - 300) * 0.17) * 1.8;
      const localX = x + offsetX;
      const localZ = z + offsetZ;
      const objectWorldX = worldX + offsetX;
      const objectWorldZ = worldZ + offsetZ;
      if (getMoonClearingInfluence(objectWorldX, objectWorldZ) > 0.18) continue;

      if (seed > 0.965) {
        placeRock({ chunk, dummy, localX, localZ, worldX: objectWorldX, worldZ: objectWorldZ, seed });
      } else if (seed > 0.94) {
        placeStump({ chunk, dummy, localX, localZ, worldX: objectWorldX, worldZ: objectWorldZ, seed });
      } else if (seed > 0.925 && chunk.objects.logs.count < 10) {
        placeLog({ chunk, dummy, localX, localZ, worldX: objectWorldX, worldZ: objectWorldZ, seed });
      } else if (seed < 0.018) {
        placeMushrooms({ chunk, dummy, localX, localZ, worldX: objectWorldX, worldZ: objectWorldZ, seed });
      }
    }
  }

  for (const mesh of Object.values(chunk.objects)) {
    mesh.instanceMatrix.needsUpdate = true;
    if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
  }
}
