import * as THREE from "three";

import { clamp, hash } from "../world/noise.js";

export function makePixelCube({ subdivisions = 5 } = {}) {
  const geometry = new THREE.BufferGeometry();
  const cell = 1 / subdivisions;
  const halfTile = cell * 0.39;
  const positions = [];
  const normals = [];
  const colors = [];
  const microPositions = [];

  function addFace(normal, uAxis, vAxis) {
    const [normalX, normalY, normalZ] = normal;
    const faceLight = normalY > 0 ? 1.2 : normalY < 0 ? 0.58 : normalZ > 0 ? 0.92 : 0.72;

    for (let u = 0; u < subdivisions; u += 1) {
      for (let v = 0; v < subdivisions; v += 1) {
        const centerU = -0.5 + cell * (u + 0.5);
        const centerV = -0.5 + cell * (v + 0.5);
        const pixelNoise =
          hash(u * 17 + normalX * 23 + normalY * 31, v * 29 + normalZ * 37) * 0.16 -
          0.08;
        const shade = clamp(faceLight + pixelNoise, 0.48, 1.24);
        const corners = [
          [-1, -1],
          [1, -1],
          [1, 1],
          [-1, -1],
          [1, 1],
          [-1, 1],
        ];

        for (const [cornerU, cornerV] of corners) {
          const position = [
            normalX * 0.5,
            normalY * 0.5,
            normalZ * 0.5,
          ];
          const microPosition = [
            normalX * 0.5,
            normalY * 0.5,
            normalZ * 0.5,
          ];

          position[uAxis] = centerU + cornerU * halfTile;
          position[vAxis] = centerV + cornerV * halfTile;
          microPosition[uAxis] = cornerU * 0.5;
          microPosition[vAxis] = cornerV * 0.5;

          positions.push(...position);
          normals.push(normalX, normalY, normalZ);
          colors.push(shade, shade, shade);
          microPositions.push(...microPosition);
        }
      }
    }
  }

  addFace([1, 0, 0], 1, 2);
  addFace([-1, 0, 0], 1, 2);
  addFace([0, 1, 0], 0, 2);
  addFace([0, -1, 0], 0, 2);
  addFace([0, 0, 1], 0, 1);
  addFace([0, 0, -1], 0, 1);

  geometry.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  geometry.setAttribute("normal", new THREE.Float32BufferAttribute(normals, 3));
  geometry.setAttribute("color", new THREE.Float32BufferAttribute(colors, 3));
  geometry.setAttribute("microPosition", new THREE.Float32BufferAttribute(microPositions, 3));
  geometry.computeBoundingSphere();
  return geometry;
}
