import { stickDeadzone } from "../config.js";
import { clamp } from "../world/noise.js";

export function createControls(targetElement) {
  const keys = new Set();
  const lookDelta = { x: 0, y: 0 };

  function getGamepad() {
    return Array.from(navigator.getGamepads?.() ?? []).find(
      (gamepad) => gamepad?.connected,
    );
  }

  function applyDeadzone(value) {
    const magnitude = Math.abs(value);
    if (magnitude < stickDeadzone) return 0;
    return Math.sign(value) * ((magnitude - stickDeadzone) / (1 - stickDeadzone));
  }

  function getMoveInput() {
    const gamepad = getGamepad();
    const padStrafe = applyDeadzone(gamepad?.axes[0] ?? 0);
    const padForward = -applyDeadzone(gamepad?.axes[1] ?? 0);

    return {
      forward: clamp(
        Number(keys.has("KeyW")) - Number(keys.has("KeyS")) + padForward,
        -1,
        1,
      ),
      strafe: clamp(
        Number(keys.has("KeyD")) - Number(keys.has("KeyA")) + padStrafe,
        -1,
        1,
      ),
    };
  }

  function getLookInput() {
    const gamepad = getGamepad();

    return {
      x: applyDeadzone(gamepad?.axes[2] ?? 0),
      y: applyDeadzone(gamepad?.axes[3] ?? 0),
    };
  }

  function consumeLookDelta() {
    const current = { ...lookDelta };
    lookDelta.x = 0;
    lookDelta.y = 0;
    return current;
  }

  window.addEventListener("keydown", (event) => keys.add(event.code));
  window.addEventListener("keyup", (event) => keys.delete(event.code));

  targetElement.addEventListener("click", () => {
    targetElement.requestPointerLock();
  });

  document.addEventListener("mousemove", (event) => {
    if (document.pointerLockElement !== targetElement) return;
    lookDelta.x += event.movementX;
    lookDelta.y += event.movementY;
  });

  return {
    getMoveInput,
    getLookInput,
    consumeLookDelta,
  };
}
