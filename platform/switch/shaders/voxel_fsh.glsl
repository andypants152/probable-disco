#version 460

layout (location = 0) in vec4 inColor;
layout (location = 1) in float inFog;
layout (location = 2) in vec3 inNormal;
layout (location = 3) in vec3 inMicroPosition;

layout (std140, binding = 0) uniform Transform {
  mat4 viewProjection;
  vec4 cameraPosition;
  vec4 cameraForward;
  vec4 fogColor;
  vec4 fogParams;
};

layout (location = 0) out vec4 outColor;

float cubeEdge(float a, float b) {
  float width = 0.13;
  return max(step(0.5 - width, abs(a)), step(0.5 - width, abs(b)));
}

void main() {
  vec3 normal = normalize(inNormal);
  vec3 normalAxis = abs(normal);
  vec2 microUv = normalAxis.x > normalAxis.y && normalAxis.x > normalAxis.z
    ? inMicroPosition.yz
    : normalAxis.y > normalAxis.z
      ? inMicroPosition.xz
      : inMicroPosition.xy;
  float edge = cubeEdge(microUv.x, microUv.y);

  vec3 moonSky = vec3(0.894, 1.0, 0.969);
  vec3 moonGround = vec3(0.314, 0.384, 0.267);
  vec3 glowColor = vec3(0.949, 1.0, 0.973);
  vec3 glowDir = normalize(vec3(-0.40, 0.80, -0.30));
  float hemi = normal.y * 0.5 + 0.5;
  float glow = max(dot(normal, glowDir), 0.0);
  vec3 light = mix(moonGround, moonSky, hemi) * 0.70 + glowColor * glow * 0.48;
  vec3 fill = inColor.rgb * light;
  vec3 outline = vec3(0.015, 0.020, 0.018);
  vec3 outlined = mix(fill, outline, edge);
  outColor = vec4(mix(outlined, fogColor.rgb, inFog), inColor.a);
}
