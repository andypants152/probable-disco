import * as THREE from "three";

import "./styles.css";

import {
  groundColor,
  leafColor,
  trunkColor,
} from "./config.js";
import { createAudioManager } from "./audio/audioManager.js";
import { createControls } from "./core/controls.js";
import { startGameLoop } from "./core/gameLoop.js";
import { createScene } from "./core/scene.js";
import { createThirdPersonCamera } from "./core/thirdPersonCamera.js";
import { makePixelCube } from "./graphics/voxelCube.js";
import { makeVoxelMaterial } from "./graphics/voxelMaterial.js";
import { createVoxelOwl } from "./npc/owl.js";
import { createFoxPlayer } from "./player/fox.js";
import { createCharm } from "./items/charm.js";
import { createStoryController } from "./story/storyController.js";
import { createChunkManager } from "./world/chunks.js";
import { createMoonClearingLandmark } from "./world/landmarks.js";

const { scene, camera, renderer, world } = createScene();
const audio = createAudioManager();

const cube = makePixelCube();
const dummy = new THREE.Object3D();
const color = new THREE.Color();
const trunkBase = new THREE.Color(trunkColor);
const leafBase = new THREE.Color(leafColor);
const groundMaterial = makeVoxelMaterial({ baseColor: groundColor });
const objectMaterial = makeVoxelMaterial();
const trunkMaterial = makeVoxelMaterial();
const leafMaterial = makeVoxelMaterial();

const chunkManager = createChunkManager({
  world,
  cubeGeometry: cube,
  groundMaterial,
  objectMaterial,
  trunkMaterial,
  leafMaterial,
  dummy,
  color,
  trunkBase,
  leafBase,
});
const player = createFoxPlayer();
scene.add(player.group);
const owl = createVoxelOwl({
  dialogueElement: document.getElementById("dialogue"),
});
scene.add(owl.group);
const charm = createCharm();
const heartLandmark = createMoonClearingLandmark();
const story = createStoryController({
  charm,
  audio,
  dialogueElement: document.getElementById("dialogue"),
  heartLandmark,
  owl,
  scene,
});

const controls = createControls(document.body);
const thirdPersonCamera = createThirdPersonCamera(camera);

window.addEventListener("resize", () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});

chunkManager.updateChunks(player.position);
startGameLoop({
  renderer,
  scene,
  camera,
  controls,
  player,
  owl,
  story,
  thirdPersonCamera,
  chunkManager,
});
