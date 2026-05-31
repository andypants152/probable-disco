import * as THREE from "three";

import {
  stickLookSpeed,
  thirdPersonCameraConfig,
} from "../config.js";
import { clamp } from "../world/noise.js";

const desiredPosition = new THREE.Vector3();
const lookTarget = new THREE.Vector3();

export function createThirdPersonCamera(camera) {
  let yaw = 0;
  let pitch = 0.18;
  let initialized = false;

  function updateOrbit(delta, controls) {
    const mouseLook = controls.consumeLookDelta();
    const stickLook = controls.getLookInput();

    yaw -= mouseLook.x * thirdPersonCameraConfig.mouseLookSpeed;
    pitch -= mouseLook.y * thirdPersonCameraConfig.mouseLookSpeed;
    yaw -= stickLook.x * stickLookSpeed * delta;
    pitch -= stickLook.y * stickLookSpeed * delta;
    pitch = clamp(
      pitch,
      thirdPersonCameraConfig.minPitch,
      thirdPersonCameraConfig.maxPitch,
    );
  }

  function updateCamera(delta, playerPosition) {
    const horizontalDistance = thirdPersonCameraConfig.distance * Math.cos(pitch);
    desiredPosition.set(
      playerPosition.x + Math.sin(yaw) * horizontalDistance,
      playerPosition.y + thirdPersonCameraConfig.height + Math.sin(pitch) * thirdPersonCameraConfig.distance,
      playerPosition.z + Math.cos(yaw) * horizontalDistance,
    );

    if (initialized) {
      camera.position.lerp(
        desiredPosition,
        1 - Math.exp(-thirdPersonCameraConfig.smoothness * delta),
      );
    } else {
      camera.position.copy(desiredPosition);
      initialized = true;
    }

    lookTarget.set(
      playerPosition.x,
      playerPosition.y + thirdPersonCameraConfig.targetHeight,
      playerPosition.z,
    );
    camera.lookAt(lookTarget);
  }

  return {
    update(delta, playerPosition, controls) {
      updateOrbit(delta, controls);
      updateCamera(delta, playerPosition);
    },
    updateOrbit,
    updateCamera,
    getYaw() {
      return yaw;
    },
  };
}
