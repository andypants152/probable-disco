import * as THREE from "three";

import {
  audioConfig,
  charmConfig,
  fogDensity,
  moonClearingConfig,
} from "../config.js";

const charmSpawnPosition = new THREE.Vector3();

export function createStoryController({
  charm,
  audio,
  dialogueElement,
  heartLandmark,
  owl,
  scene,
}) {
  let state = "owlIntro";
  let followUpTimer = null;
  let messageTimer = 0;
  let reachedClearing = false;
  let humTextCooldown = 0;
  let lastHumText = "";

  function showLine(line, duration = 3.5) {
    if (owl?.speak) {
      owl.speak(line);
      return;
    }

    if (!dialogueElement) {
      console.log(line);
      return;
    }

    dialogueElement.textContent = line;
    dialogueElement.hidden = false;
    messageTimer = duration;
  }

  function spawnCharm() {
    charmSpawnPosition.set(0, 1.05, -4.25);
    charm.appear(charmSpawnPosition);
    audio?.playCharmAppear();
    state = "charmAvailable";
  }

  scene.add(charm.group, heartLandmark.group);

  function getClearingOwlPlacement(foxPosition) {
    const position = new THREE.Vector3(...moonClearingConfig.position).add(
      new THREE.Vector3(...moonClearingConfig.owlOffset),
    );
    const directionToFox = new THREE.Vector3().copy(foxPosition).sub(position);
    const rotationY = Math.atan2(-directionToFox.x, -directionToFox.z);

    return { position, rotationY };
  }

  function update(delta, fox, cameraYaw, sceneInstance) {
    heartLandmark.update(delta);
    charm.update(delta, fox.group, heartLandmark.position, cameraYaw);
    const distanceToClearing = fox.position.distanceTo(heartLandmark.position);
    const clearingFog = THREE.MathUtils.clamp(
      (moonClearingConfig.radius - distanceToClearing) / moonClearingConfig.radius,
      0,
      1,
    );
    if (sceneInstance?.fog) {
      sceneInstance.fog.density = THREE.MathUtils.lerp(fogDensity, fogDensity * 0.72, clearingFog);
    }

    if (messageTimer > 0) {
      messageTimer -= delta;
      if (messageTimer <= 0 && dialogueElement) dialogueElement.hidden = true;
    }
    if (humTextCooldown > 0) humTextCooldown -= delta;

    if (state === "owlIntro" && owl.hasSpoken && followUpTimer === null) {
      followUpTimer = 1.1;
    }

    if (followUpTimer !== null) {
      followUpTimer -= delta;
      if (followUpTimer <= 0) {
        showLine("Here. Take this.");
        spawnCharm();
        followUpTimer = null;
      }
    }

    if (charm.isAvailable && fox.position.distanceTo(charm.group.position) < charmConfig.pickupRadius) {
      charm.collect();
      audio?.playCharmPickup();
      showLine("It hums when you face the right way.");
      state = "guiding";
    }

    if (charm.isCollected && !reachedClearing && humTextCooldown <= 0) {
      const line =
        charm.guidanceAlignment > audioConfig.warmHumTextThreshold
          ? "The charm hums warmly."
          : charm.guidanceAlignment > audioConfig.humTextThreshold
            ? "The charm hums."
            : "";

      if (line && line !== lastHumText) {
        showLine(line, 2.4);
        lastHumText = line;
        humTextCooldown = 8;
      }
      if (!line && charm.guidanceAlignment < audioConfig.humTextThreshold * 0.7) {
        lastHumText = "";
      }
    }

    if (!reachedClearing && charm.isCollected && distanceToClearing < moonClearingConfig.triggerRadius) {
      reachedClearing = true;
      const owlPlacement = getClearingOwlPlacement(fox.position);
      owl.appearAt(owlPlacement.position, owlPlacement.rotationY);
      audio?.playOwlAppear();
      audio?.playClearingReached();
      showLine("How do you know when you're halfway through the woods?", 5);
      if (sceneInstance?.fog?.color) {
        sceneInstance.fog.color.set(0x2b403b);
      }
    }

    audio?.update(delta, {
      orbCollected: charm.isCollected,
      guidanceAlignment: charm.guidanceAlignment,
      nearClearing: distanceToClearing < moonClearingConfig.radius,
    });
  }

  return {
    update,
    get state() {
      return state;
    },
  };
}
