import * as THREE from "three";

import {
  cameraConfig,
  fogDensity,
  lightConfig,
  rendererConfig,
  sceneBackgroundColor,
} from "../config.js";

export function createScene() {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(sceneBackgroundColor);
  scene.fog = new THREE.FogExp2(sceneBackgroundColor, fogDensity);

  const camera = new THREE.PerspectiveCamera(
    cameraConfig.fov,
    window.innerWidth / window.innerHeight,
    cameraConfig.near,
    cameraConfig.far,
  );
  camera.position.set(...cameraConfig.position);

  const renderer = new THREE.WebGLRenderer({ antialias: rendererConfig.antialias });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, rendererConfig.maxPixelRatio));
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.setClearColor(scene.background);
  document.body.appendChild(renderer.domElement);

  const moon = new THREE.HemisphereLight(
    lightConfig.moonSky,
    lightConfig.moonGround,
    lightConfig.moonIntensity,
  );
  scene.add(moon);

  const glow = new THREE.DirectionalLight(lightConfig.glowColor, lightConfig.glowIntensity);
  glow.position.set(...lightConfig.glowPosition);
  scene.add(glow);

  const world = new THREE.Group();
  scene.add(world);

  return {
    scene,
    camera,
    renderer,
    world,
  };
}
