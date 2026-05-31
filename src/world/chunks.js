import * as THREE from "three";

import {
  chunkRadius,
  chunkSize,
  chunkSpan,
  maxLeafBlocks,
  maxTrunkBlocks,
  treeStep,
} from "../config.js";
import {
  createGroundLayer,
  populateGroundLayer,
} from "./ground.js";
import {
  createForestObjectLayers,
  populateForestObjects,
} from "./forestObjects.js";
import {
  getMoonClearingInfluence,
  getMoonClearingRingInfluence,
} from "./landmarks.js";
import { createTreeGenerator } from "./trees.js";
import { fbm, hash, noise2D, treeRand } from "./noise.js";

export function createChunkManager({
  world,
  cubeGeometry,
  groundMaterial,
  objectMaterial,
  trunkMaterial,
  leafMaterial,
  dummy,
  color,
  trunkBase,
  leafBase,
}) {
  const chunks = [];
  const activeChunks = new Map();
  const treeGenerator = createTreeGenerator({
    dummy,
    color,
    trunkBase,
    leafBase,
  });

  function makeChunk() {
    const group = new THREE.Group();
    const ground = createGroundLayer({
      material: groundMaterial,
    });
    const objects = createForestObjectLayers({
      cubeGeometry,
      material: objectMaterial,
    });

    const trunks = new THREE.InstancedMesh(cubeGeometry, trunkMaterial, maxTrunkBlocks);
    const leaves = new THREE.InstancedMesh(cubeGeometry, leafMaterial, maxLeafBlocks);
    trunks.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
    leaves.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
    group.add(ground, objects.rocks, objects.stumps, objects.logs, objects.mushrooms, trunks, leaves);
    world.add(group);

    return {
      group,
      ground,
      objects,
      trunks,
      leaves,
      colliders: [],
      x: Infinity,
      z: Infinity,
    };
  }

  function addTreeCollider(chunk, worldX, worldZ) {
    const large = treeRand(worldX, worldZ, 3) > 0.96;
    chunk.colliders.push({
      x: worldX,
      z: worldZ,
      radius: large ? 0.95 : 0.58,
    });
  }

  function populateChunk(chunk, chunkX, chunkZ) {
    chunk.x = chunkX;
    chunk.z = chunkZ;
    chunk.group.position.set(chunkX * chunkSize, 0, chunkZ * chunkSize);
    chunk.trunks.count = 0;
    chunk.leaves.count = 0;
    chunk.colliders.length = 0;
    populateGroundLayer({ chunk, chunkX, chunkZ, dummy });
    populateForestObjects({ chunk, chunkX, chunkZ, dummy });

    for (let x = -chunkSize / 2 + 6; x < chunkSize / 2 - 4; x += treeStep) {
      for (let z = -chunkSize / 2 + 6; z < chunkSize / 2 - 4; z += treeStep) {
        const worldX = chunkX * chunkSize + x;
        const worldZ = chunkZ * chunkSize + z;
        const densityNoise = (fbm(worldX * 0.015, worldZ * 0.015, 4) + 1) * 0.5;
        const patchNoise = (fbm(worldX * 0.05 + 211, worldZ * 0.05 - 211, 3) + 1) * 0.5;
        const clearing = getMoonClearingInfluence(worldX, worldZ);
        const clearingRing = getMoonClearingRingInfluence(worldX, worldZ);
        const density =
          (0.24 + densityNoise * 0.5 + patchNoise * 0.16) * (1 - clearing * 0.92) +
          clearingRing * 0.12;
        const placement = hash(worldX + 211, worldZ - 211);
        if (placement > density) continue;

        // Cell jitter hides the sampling lattice; sub-voxel noise prevents snapped positions.
        const cellOffsetX = (hash(worldX + 17, worldZ - 23) * 2 - 1) * treeStep * 0.42;
        const cellOffsetZ = (hash(worldX - 31, worldZ + 47) * 2 - 1) * treeStep * 0.42;
        const offsetX = cellOffsetX + noise2D(worldX * 0.19, worldZ * 0.19) * 0.8;
        const offsetZ = cellOffsetZ + noise2D((worldX + 100) * 0.19, (worldZ + 100) * 0.19) * 0.8;
        const localX = x + offsetX;
        const localZ = z + offsetZ;
        const treeWorldX = chunkX * chunkSize + localX;
        const treeWorldZ = chunkZ * chunkSize + localZ;
        const rotationY = noise2D(treeWorldX * 0.11, treeWorldZ * 0.11) * 0.3;
        const seed = hash(Math.floor(treeWorldX * 10), Math.floor(treeWorldZ * 10));
        treeGenerator.buildTree(chunk, localX, localZ, seed, treeWorldX, treeWorldZ, rotationY);
        addTreeCollider(chunk, treeWorldX, treeWorldZ);

        if (hash(worldX + 71, worldZ - 37) > 0.9) {
          const clusterCount = 1 + Math.floor(hash(worldX - 13, worldZ + 91) * 3);
          for (let cluster = 0; cluster < clusterCount; cluster += 1) {
            const clusterOffsetX = noise2D(worldX * 0.23 + cluster * 17, worldZ * 0.23 + 11) * 5;
            const clusterOffsetZ = noise2D(worldX * 0.23 - 19, worldZ * 0.23 + cluster * 23) * 5;
            const clusterWorldX = treeWorldX + clusterOffsetX;
            const clusterWorldZ = treeWorldZ + clusterOffsetZ;
            const clusterRotationY = noise2D(clusterWorldX * 0.11, clusterWorldZ * 0.11) * 0.3;
            const clusterSeed = hash(Math.floor(clusterWorldX * 10), Math.floor(clusterWorldZ * 10));
            if (getMoonClearingInfluence(clusterWorldX, clusterWorldZ) < 0.25) {
              treeGenerator.buildTree(
                chunk,
                localX + clusterOffsetX,
                localZ + clusterOffsetZ,
                clusterSeed,
                clusterWorldX,
                clusterWorldZ,
                clusterRotationY,
              );
              addTreeCollider(chunk, clusterWorldX, clusterWorldZ);
            }
          }
        }
      }
    }

    chunk.trunks.instanceMatrix.needsUpdate = true;
    chunk.leaves.instanceMatrix.needsUpdate = true;
    if (chunk.trunks.instanceColor) chunk.trunks.instanceColor.needsUpdate = true;
    if (chunk.leaves.instanceColor) chunk.leaves.instanceColor.needsUpdate = true;
  }

  function updateChunks(cameraPosition) {
    const centerX = Math.floor(cameraPosition.x / chunkSize);
    const centerZ = Math.floor(cameraPosition.z / chunkSize);
    const needed = [];

    for (let x = centerX - chunkRadius; x <= centerX + chunkRadius; x += 1) {
      for (let z = centerZ - chunkRadius; z <= centerZ + chunkRadius; z += 1) {
        needed.push(`${x},${z}`);
      }
    }

    activeChunks.clear();
    for (const chunk of chunks) {
      if (needed.includes(`${chunk.x},${chunk.z}`)) {
        activeChunks.set(`${chunk.x},${chunk.z}`, chunk);
      }
    }

    for (const key of needed) {
      if (activeChunks.has(key)) continue;
      const freeChunk = chunks.find((chunk) => !needed.includes(`${chunk.x},${chunk.z}`));
      const [x, z] = key.split(",").map(Number);
      populateChunk(freeChunk, x, z);
      activeChunks.set(key, freeChunk);
    }
  }

  function resolveTreeCollisions(position, radius) {
    for (const chunk of activeChunks.values()) {
      for (const collider of chunk.colliders) {
        const offsetX = position.x - collider.x;
        const offsetZ = position.z - collider.z;
        const minDistance = radius + collider.radius;
        const distanceSquared = offsetX * offsetX + offsetZ * offsetZ;

        if (distanceSquared >= minDistance * minDistance) continue;

        const distance = Math.sqrt(distanceSquared);
        if (distance === 0) {
          position.x += minDistance;
          continue;
        }

        const push = (minDistance - distance) / distance;
        position.x += offsetX * push;
        position.z += offsetZ * push;
      }
    }
  }

  for (let i = 0; i < chunkSpan * chunkSpan; i += 1) {
    chunks.push(makeChunk());
  }

  return {
    updateChunks,
    resolveTreeCollisions,
    chunks,
    activeChunks,
  };
}
