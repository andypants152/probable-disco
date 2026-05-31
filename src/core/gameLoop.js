import * as THREE from "three";

export function startGameLoop({
  renderer,
  scene,
  camera,
  controls,
  player,
  owl,
  story,
  thirdPersonCamera,
  chunkManager,
}) {
  const timer = new THREE.Timer();
  timer.connect(document);

  function render(timestamp) {
    timer.update(timestamp);
    const delta = Math.min(timer.getDelta(), 0.05);
    thirdPersonCamera.updateOrbit(delta, controls);
    player.update(delta, controls.getMoveInput(), thirdPersonCamera.getYaw(), chunkManager.resolveTreeCollisions);
    owl?.update(delta, player.position);
    story?.update(delta, player, thirdPersonCamera.getYaw(), scene);
    chunkManager.updateChunks(player.position);
    thirdPersonCamera.updateCamera(delta, player.position);
    renderer.render(scene, camera);
    requestAnimationFrame(render);
  }

  render();
}
