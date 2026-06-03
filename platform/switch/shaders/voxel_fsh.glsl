#version 460

layout (location = 0) in vec4 inColor;
layout (location = 1) in float inFog;
layout (location = 2) in vec3 inNormal;
layout (location = 3) in vec3 inMicroPosition;
layout (location = 4) in vec3 inWorldPosition;

layout (std140, binding = 0) uniform Transform {
  mat4 viewProjection;
  vec4 cameraPosition;
  vec4 cameraForward;
  vec4 fogColor;
  vec4 fogParams;
  vec4 lightPositionRadius[8];
  vec4 lightColorIntensity[8];
  vec4 lightParams;
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
  vec3 light = mix(moonGround, moonSky, hemi) * 0.38 + glowColor * glow * 0.24 + vec3(0.035, 0.045, 0.040);
  vec3 localLight = vec3(0.0);
  int lightCount = int(lightParams.x + 0.5);
  for (int i = 0; i < 4; ++i) {
    if (i >= lightCount) {
      break;
    }
    vec4 lightPosRadius = lightPositionRadius[i];
    vec4 lightColor = lightColorIntensity[i];
    vec3 lightDelta = inWorldPosition - lightPosRadius.xyz;
    float radius = max(lightPosRadius.w, 0.001);
    float falloff = clamp(1.0 - dot(lightDelta, lightDelta) / (radius * radius), 0.0, 1.0);
    falloff *= falloff;
    localLight += lightColor.rgb * (falloff * lightColor.w * 1.65);
  }
  vec3 outline = vec3(0.015, 0.020, 0.018);
  float emissive = clamp(1.0 - inColor.a, 0.0, 1.0);
  vec3 localVisible = localLight * (1.0 - emissive * 0.70);
  vec3 fill = inColor.rgb * light + inColor.rgb * localVisible * 1.15 + localVisible * 0.16;
  vec3 emissiveColor = mix(inColor.rgb, min(inColor.rgb * 1.25, vec3(1.0)), emissive);
  vec3 outlined = mix(fill, outline, edge * (1.0 - emissive * 0.78));
  vec3 litColor = mix(outlined, emissiveColor, emissive);
  float localStrength = clamp(max(max(localLight.r, localLight.g), localLight.b), 0.0, 1.0);
  outColor = vec4(mix(litColor, fogColor.rgb, inFog * (1.0 - emissive * 0.72) * (1.0 - localStrength * 0.35)), 1.0);
}
