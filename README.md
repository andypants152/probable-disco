# Voxel Forest

Portable C++ voxel forest game prototype with shared engine/game code and thin platform layers for WebAssembly and Nintendo Switch homebrew.

The active renderer path is shared OpenGL ES-style renderer code behind platform-specific SDL/WebGL context setup. Browser and Switch builds submit the same `RenderFrame` command stream for terrain chunks, dynamic voxel actors, gameplay lights, quad overlays, and bitmap-font subtitles.

## Current State

- Fixed `32x32x32` voxel chunks with deterministic terrain generated from world coordinates.
- Streamed procedural forest window around the fox.
- Voxel types: air, grass, dirt, stone, bark, leaves.
- Face-culling mesher that emits only visible cube faces.
- Moonlit forest art direction with dense fog, voxel face outlines, point lights, and glow effects.
- Static forest dressing: rocks, stumps, fallen logs, mushrooms, a moon clearing, an owl perch, lanterns, and a cyan charm set piece.
- Shared-core voxel fox mesh, third-person movement, and orbit camera.
- Owl encounter with cinematic camera shots and subtitle-driven dialogue.
- Firefly and lantern loop with collectible fireflies, carried firefly lights, lantern deposits, and debug progression controls.
- Squirrel quest actors with approach, dialogue, animation, and completion events.
- Shared forest audio hooks and platform-neutral subtitle composition.
- Browser build renders through Emscripten WebGL2.
- Switch build renders through SDL2 and the same shared GLES renderer.

## Web Build

Install and activate Emscripten, then build:

```sh
make web
```

Serve the generated files:

```sh
emrun --no_browser --port 8080 dist/web
```

Open `http://localhost:8080`. The deployable static output is written to `dist/web/`:

```text
dist/web/index.html
dist/web/index.js
dist/web/index.wasm
dist/web/assets/
```

`build/web/` is only the CMake build directory. Do not deploy it directly.

Web controls:

- `WASD` or arrow keys: move the fox
- `Space`, `Enter`, or `E`: interact/confirm
- Mouse drag: orbit the third-person camera
- `Esc`: pause input
- Gamepad left stick or D-pad: move the fox
- Gamepad right stick: orbit the third-person camera
- Gamepad face button: interact/confirm
- On phones and tablets, touch controls appear automatically: left stick moves, right stick looks/turns, and `A` interacts

## Switch Build

Install devkitPro with devkitA64 and libnx, then make sure `DEVKITPRO` is set:

```sh
export DEVKITPRO=/opt/devkitpro
make switch
```

The root `switch` target packages the Switch artifacts in `dist/switch/`:

```text
dist/switch/probable-disco.nro
dist/switch/probable-disco.elf
```

The underlying Makefile output remains in `build/switch/`. The normal Switch build uses SDL2 plus the shared GLES renderer and includes throttled nxlink timing output when launched through nxlink.

Switch controls:

- Left stick or D-pad: move the fox
- Right stick: orbit the third-person camera
- `A`: interact/confirm
- `+`: exit

To build with debug/profile compiler flags:

```sh
make -f Makefile.switch PROFILE=1
```

This produces `build/switch-profile/probable-disco.nro`; use it for symbols or deeper diagnostics, not as a separate runtime artifact.

## Project Layout

```text
engine/core/        Shared app loop, audio hooks, platform interface, and subtitles
engine/game/        Shared camera, fox, owl, squirrel, conversation, and firefly gameplay code
engine/world/       Shared voxel chunks, terrain generation, streaming, and meshing
engine/render/      Shared renderer API, RenderCommand frame data, GLES renderer, QuadBatch, and bitmap font subtitles
engine/math/        Shared math helpers
platform/web/       Emscripten browser entry point and WebGL context glue
platform/switch/    devkitPro/libnx entry point and SDL/GLES context glue
assets/             Shared runtime assets
dist/               Generated deployable build output
build/              Generated local build directories
```

## Development Notes

Platform-specific files should own lifecycle, input, window/context creation, and presentation only. Game state, voxel meshing, lighting data, sprite/quad batching, audio state, and subtitle composition belong in shared code.

Use the root `Makefile` for packaged outputs:

```sh
make web
make switch
```
