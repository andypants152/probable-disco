# Probable Disco Voxel Forest

Portable C++ voxel forest engine with a shared core and thin platform layers for WebAssembly and Nintendo Switch homebrew.

The engine core lives under `engine/core` and does not include browser, WebGPU/WebGL, libnx, deko3d, or Switch-specific APIs. Platform code owns rendering, input, and lifecycle.

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
- Shared camera state and update loop.
- Browser platform renders the generated mesh with C++ WebGL2 through Emscripten.
- Switch platform renders through deko3d behind the same renderer interface.

## Web Build

Install and activate Emscripten, then build:

```sh
npm run build:web
```

Serve the generated files:

```sh
npm run serve:web
```

Open `http://localhost:8080`. The build output is `build-web/index.html`, `build-web/index.js`, and `build-web/index.wasm`.

Controls:

- `WASD`: move the fox
- Gamepad left stick or D-pad: move the fox
- Mouse drag: orbit the third-person camera
- Gamepad right stick: orbit the third-person camera

## Switch Build

Install devkitPro with devkitA64 and libnx, then make sure `DEVKITPRO` is set:

```sh
export DEVKITPRO=/opt/devkitpro
make -f Makefile.switch
```

This produces `probable-disco.nro`. The normal Switch build uses deko3d and includes throttled nxlink timing output when launched through nxlink.

Switch controls:

- Left stick or D-pad: move the fox
- Right stick or shoulder buttons: orbit the third-person camera
- `+`: exit

To build with debug/profile compiler flags:

```sh
make -f Makefile.switch PROFILE=1
```

This still produces `probable-disco.nro`; use it for symbols or deeper diagnostics, not as a separate runtime artifact.

## Project Layout

```text
engine/core/        Shared C++ engine code
platform/web/       Emscripten browser platform and WebGL2 renderer
platform/switch/    devkitPro/libnx platform skeleton
assets/             Future shared assets
```
