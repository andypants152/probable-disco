import * as THREE from "three";

export function makeVoxelMaterial({ baseColor = 0xffffff } = {}) {
  return new THREE.ShaderMaterial({
    fog: true,
    uniforms: {
      ...THREE.UniformsUtils.clone(THREE.UniformsLib.fog),
      baseColor: { value: new THREE.Color(baseColor) },
    },
    vertexColors: true,
    side: THREE.DoubleSide,
    vertexShader: `
            attribute vec3 microPosition;

            uniform vec3 baseColor;

            varying vec3 vLocalPosition;
            varying vec3 vMicroPosition;
            varying vec3 vNormal;
            varying vec3 vPatchShade;
            varying vec3 vInstanceColor;

            #include <fog_pars_vertex>

            void main() {
              vLocalPosition = position;
              vMicroPosition = microPosition;
              vNormal = normal;
              #ifdef USE_COLOR
              vPatchShade = color;
              #else
              vPatchShade = vec3(1.0);
              #endif

              #ifdef USE_INSTANCING_COLOR
              vInstanceColor = instanceColor;
              #else
              vInstanceColor = baseColor;
              #endif

              #ifdef USE_INSTANCING
              vec4 mvPosition = modelViewMatrix * instanceMatrix * vec4(position, 1.0);
              #else
              vec4 mvPosition = modelViewMatrix * vec4(position, 1.0);
              #endif
              gl_Position = projectionMatrix * mvPosition;

              #include <fog_vertex>
            }
          `,
    fragmentShader: `
            varying vec3 vLocalPosition;
            varying vec3 vMicroPosition;
            varying vec3 vNormal;
            varying vec3 vPatchShade;
            varying vec3 vInstanceColor;

            #include <fog_pars_fragment>

            float cubeEdge(float a, float b) {
              float width = 0.13;
              return max(step(0.5 - width, abs(a)), step(0.5 - width, abs(b)));
            }

            void main() {
              vec3 normalAxis = abs(normalize(vNormal));
              vec2 microUv = normalAxis.x > normalAxis.y && normalAxis.x > normalAxis.z
                ? vMicroPosition.yz
                : normalAxis.y > normalAxis.z
                  ? vMicroPosition.xz
                  : vMicroPosition.xy;
              float edge = cubeEdge(microUv.x, microUv.y);

              vec3 fill = vInstanceColor * vPatchShade;
              vec3 outline = vec3(0.015, 0.02, 0.018);
              gl_FragColor = vec4(mix(fill, outline, edge), 1.0);

              #include <fog_fragment>
            }
          `,
  });
}
