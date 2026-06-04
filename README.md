# Probable Disco Voxel Forest

Portable C++ voxel forest engine with shared game code, a shared renderer layer, and thin platform layers for WebAssembly and Nintendo Switch homebrew.

The active renderer path is SDL/OpenGL ES-style platform glue plus shared C++ renderer code. Browser and Switch builds now submit the same `RenderFrame` command stream for voxel meshes, simple point lights, sprite/quad overlays, and bitmap-font subtitles. New gameplay/render features should go through this shared path.

## Current Milestone

- Fixed 32x32x32 voxel chunk.
- Voxel types: air, grass, dirt, stone, bark, leaves.
- Deterministic low-variation terrain sampled by world coordinate.
- Streamed procedural forest window around the fox.
- Face-culling mesher that emits only visible cube faces.
- Dark moonlit art direction with dense forest fog and voxel face outlines.
- Static forest dressing: rocks, stumps, fallen logs, mushrooms, a moon clearing, an owl perch, and a cyan charm set piece.
- Shared-core voxel fox mesh added to the scene.
- Third-person fox movement and camera follow in the shared core.
- Platform-neutral mesh with positions, normals, packed colors, micro face coordinates, and indices.
- Shared `RenderCommand` frame submission for static mesh, dynamic mesh, and subtitle overlay draws.
- Shared `QuadBatch`/bitmap font subtitle path. The test subtitle is `Oh good, you're awake.`
- Shared camera state and update loop.
- Browser platform renders through Emscripten WebGL2 using the shared GLES renderer.
- Switch platform renders through SDL2 and the same shared GLES renderer.

## Web Build

Install and activate Emscripten, then build:

```sh
npm run build:web
```

Serve the generated files:

```sh
npm run serve:web
```

Open `http://localhost:8080`. The deployable static output is written to `dist/web/`:

```text
dist/web/index.html
dist/web/index.js
dist/web/index.wasm
```

`build/web/` is only the CMake build directory. Do not deploy it directly.

Controls:

- `WASD`: move the fox
- Gamepad left stick or D-pad: move the fox
- Mouse drag: orbit the third-person camera
- Gamepad right stick: orbit the third-person camera
- On phones and tablets, touch controls appear automatically:
  left stick moves, right stick looks/turns, and `A` interacts.

## GitHub Pages Deployment

The workflow at `.github/workflows/pages.yml` builds the Web/WASM target on every push to `main` and uploads `dist/web/` to GitHub Pages. It can also be run manually from the Actions tab.

To enable it in GitHub:

```text
Settings -> Pages -> Source -> GitHub Actions
```

The deployed page will be available at:

```text
https://USERNAME.github.io/REPOSITORY_NAME/
```

The web build is static and uses relative generated file paths, so it can run from a repository subpath instead of only from a domain root.

## Switch Build

Install devkitPro with devkitA64 and libnx, then make sure `DEVKITPRO` is set:

```sh
export DEVKITPRO=/opt/devkitpro
make -f Makefile.switch
```

The npm script packages the Switch artifacts in `dist/switch/`:

```sh
npm run build:switch
```

```text
dist/switch/probable-disco.nro
dist/switch/probable-disco.elf
```

The underlying Makefile output remains in `build/switch/`. The normal Switch build uses SDL2 plus the shared GLES renderer and includes throttled nxlink timing output when launched through nxlink.

Switch controls:

- Left stick or D-pad: move the fox
- Right stick: orbit the third-person camera
- `+`: exit
- Dev controls: tap `L`/`R` to lower/raise the gameplay light cap, `ZL` to collect the active lantern's fireflies, `ZR` to force-deposit and advance the lantern

To build with debug/profile compiler flags:

```sh
make -f Makefile.switch PROFILE=1
```

This produces `build/switch-profile/probable-disco.nro`; use it for symbols or deeper diagnostics, not as a separate runtime artifact.

## Project Layout

```text
engine/core/                     Shared app, audio, platform interface, and subtitles
engine/game/                     Shared camera and fox gameplay code
engine/world/                    Shared voxel chunks, generation, and meshing
engine/render/                   Shared renderer API, RenderCommand frame data, GLES renderer, QuadBatch, bitmap font subtitles
engine/math/                     Shared math helpers
platform/web/                    Emscripten browser entry point and WebGL context glue
platform/switch/                 devkitPro/libnx entry point and SDL/GLES context glue
assets/                          Shared assets; subtitle font files are no longer used by the active bitmap-font path
```

Platform-specific files should own lifecycle, input, window/context creation, and presentation only. Game state, voxel meshing, lighting data, sprite/quad batching, and subtitle composition belong in shared code.
