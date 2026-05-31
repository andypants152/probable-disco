import {
  choose,
  clamp,
  noise2D,
  noise3D,
  pixelJitter,
  treeRand,
} from "./noise.js";

export function blockColor({ base, tintSeed, kind, color }) {
  color.copy(base);
  if (kind === "leaf") {
    color.offsetHSL((tintSeed - 0.5) * 0.05, (treeRand(tintSeed * 997, tintSeed * 463, 9) - 0.5) * 0.16, 0);
  }

  const brightness = kind === "leaf" ? 0.82 + tintSeed * 0.34 : 0.78 + tintSeed * 0.28;
  return color.multiplyScalar(brightness);
}

export function createTreeGenerator({
  dummy,
  color,
  trunkBase,
  leafBase,
}) {
  function placeBlock(mesh, index, x, y, z, scale = 1, tintSeed = 0.5, kind = "leaf", rotationY = 0) {
    if (index >= mesh.instanceMatrix.count) return false;

    dummy.position.set(
      x + pixelJitter(tintSeed, 1),
      y + pixelJitter(tintSeed, 2),
      z + pixelJitter(tintSeed, 3),
    );
    dummy.rotation.set(0, rotationY, 0);
    dummy.scale.setScalar(scale * 0.88);
    dummy.updateMatrix();
    mesh.setMatrixAt(index, dummy.matrix);
    mesh.setColorAt(index, blockColor({
      base: kind === "leaf" ? leafBase : trunkBase,
      tintSeed,
      kind,
      color,
    }));
    return true;
  }

  function buildTree(chunk, x, z, seed, worldX = x, worldZ = z, rotationY = 0) {
    const archetypeSeed = seed;
    const canopySeed = treeRand(worldX, worldZ, 2);
    const large = treeRand(worldX, worldZ, 3) > 0.96;
    const scale = large ? 2 : 1;
    const archetype =
      archetypeSeed < 0.04
        ? "dead"
        : archetypeSeed < 0.34
          ? "sparse"
          : archetypeSeed < 0.63
            ? "tall"
            : "bushy";
    const canopyShape = choose(canopySeed, ["sphere", "cone", "layers"]);
    const heightRanges = {
      tall: [9, 15],
      bushy: [4, 7],
      sparse: [7, 12],
      dead: [5, 11],
    };
    const [minHeight, maxHeight] = heightRanges[archetype];
    const height =
      Math.floor(minHeight + treeRand(worldX, worldZ, 4) * (maxHeight - minHeight + 1)) *
      scale;
    const leafCullBase = {
      tall: 0.04,
      bushy: 0.03,
      sparse: 0.08,
      dead: 1,
    }[archetype];
    let trunkCount = chunk.trunks.count;
    let leafCount = chunk.leaves.count;
    const trunkBlocks = [];
    const leanAngle = treeRand(worldX, worldZ, 5) * Math.PI * 2;
    const leanStrength = (0.18 + treeRand(worldX, worldZ, 6) * 0.55) * scale;
    const treeLeanX = Math.cos(leanAngle) * leanStrength;
    const treeLeanZ = Math.sin(leanAngle) * leanStrength;
    const bendPhaseX = treeRand(worldX, worldZ, 7) * 1000;
    const bendPhaseZ = treeRand(worldX, worldZ, 8) * 1000;
    let topLeanX = 0;
    let topLeanZ = 0;

    function treePosition(offsetX, offsetZ) {
      const sin = Math.sin(rotationY);
      const cos = Math.cos(rotationY);

      return {
        x: x + offsetX * cos - offsetZ * sin,
        z: z + offsetX * sin + offsetZ * cos,
      };
    }

    function addTrunkBlock(offsetX, blockY, offsetZ, blockScale = scale) {
      const block = treePosition(offsetX, offsetZ);
      const tintSeed = treeRand(worldX + block.x, worldZ + block.z, 140 + blockY);
      if (
        placeBlock(
          chunk.trunks,
          trunkCount,
          block.x,
          blockY,
          block.z,
          blockScale,
          tintSeed,
          "trunk",
          rotationY,
        )
      ) {
        trunkBlocks.push({ x: block.x, y: blockY, z: block.z, scale: blockScale });
        trunkCount += 1;
      }
    }

    function touchesTrunk(blockX, blockY, blockZ, blockScale = scale) {
      return trunkBlocks.some((trunk) => {
        const gap = (trunk.scale + blockScale) * 0.5;
        return (
          Math.abs(trunk.x - blockX) < gap &&
          Math.abs(trunk.y - blockY) < gap &&
          Math.abs(trunk.z - blockZ) < gap
        );
      });
    }

    for (let y = 0; y < height; y += scale) {
      const heightRatio = y / Math.max(height - scale, scale);
      // Per-tree phases prevent neighboring trunks from sharing the same lean direction.
      topLeanX = treeLeanX * heightRatio + noise2D(bendPhaseX, y * 0.33 + worldZ * 0.015) * 0.2 * scale;
      topLeanZ = treeLeanZ * heightRatio + noise2D(bendPhaseZ, y * 0.33 + worldX * 0.015) * 0.2 * scale;

      addTrunkBlock(topLeanX, y + scale * 0.5, topLeanZ);
    }

    if (archetype === "dead") {
      const branches = 1 + Math.floor(treeRand(worldX, worldZ, 50) * 2);
      for (let branch = 0; branch < branches; branch += 1) {
        const branchY = Math.floor(
          height * (0.45 + treeRand(worldX, worldZ, 51 + branch) * 0.35),
        );
        const branchX = choose(treeRand(worldX, worldZ, 53 + branch), [-1, 0, 1]) * scale;
        const branchZ =
          branchX === 0 ? choose(treeRand(worldX, worldZ, 55 + branch), [-1, 1]) * scale : 0;
        addTrunkBlock(
          topLeanX + branchX,
          branchY + scale * 0.5,
          topLeanZ + branchZ,
        );
      }
    } else {
      const baseRadius = {
        tall: 1.6,
        bushy: 3.1,
        sparse: 2.2,
      }[archetype] * scale;
      const canopyHeight = {
        tall: 3,
        bushy: 4,
        sparse: 3,
      }[archetype] * scale;
      const canopyY = height + Math.floor((treeRand(worldX, worldZ, 60) - 0.5) * 2 * scale);
      const canopyRadius = Math.max(scale, baseRadius * (canopyShape === "cone" ? 1 : 1.12));
      const verticalRadius = Math.max(scale * 2, canopyHeight * 0.72);
      const canopyCenterY = canopyShape === "cone" ? -0.2 * scale : 0.15 * canopyHeight;
      const leafCull = leafCullBase + treeRand(worldX, worldZ, 66) * 0.05;

      for (let ly = -verticalRadius; ly <= verticalRadius; ly += scale) {
        for (let lx = -canopyRadius; lx <= canopyRadius; lx += scale) {
          for (let lz = -canopyRadius; lz <= canopyRadius; lz += scale) {
            const scaledY = (ly - canopyCenterY) * (canopyRadius / verticalRadius);
            const distance = Math.sqrt(lx * lx + scaledY * scaledY + lz * lz);
            const boundaryNoise =
              noise3D((worldX + lx) * 0.28, (canopyY + ly) * 0.28, (worldZ + lz) * 0.28) *
              0.5 *
              scale;
            const interiorNoise =
              (noise3D((worldX + lx) * 1.3, (canopyY + ly) * 1.3, (worldZ + lz) * 1.3) + 1) *
              0.5;

            // A noisy sphere produces ragged voxel crowns and avoids perfect blob silhouettes.
            if (distance > canopyRadius + boundaryNoise || interiorNoise < leafCull) continue;

            const leaf = treePosition(topLeanX + lx, topLeanZ + lz);
            const leafY = canopyY + ly + noise3D(worldX + lx, canopyY + ly, worldZ + lz) * 0.2 * scale;
            // Let canopy voxels occupy trunk-adjacent space so crowns look full instead of hollow.
            const tintSeed = clamp(
              0.5 +
                noise3D((worldX + lx) * 0.7, (canopyY + ly) * 0.7, (worldZ + lz) * 0.7) * 0.5,
              0,
              1,
            );
            if (
              placeBlock(
                chunk.leaves,
                leafCount,
                leaf.x,
                leafY,
                leaf.z,
                scale,
                tintSeed,
                "leaf",
                rotationY,
              )
            ) {
              leafCount += 1;
            }
          }
        }
      }
    }

    chunk.trunks.count = trunkCount;
    chunk.leaves.count = leafCount;
  }

  return {
    buildTree,
    placeBlock,
  };
}
