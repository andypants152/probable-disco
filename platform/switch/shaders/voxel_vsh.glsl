#version 460

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec4 inColor;
layout (location = 3) in vec3 inMicroPosition;

layout (std140, binding = 0) uniform Transform {
  mat4 viewProjection;
  vec4 cameraPosition;
  vec4 cameraForward;
  vec4 fogColor;
  vec4 fogParams;
};

layout (location = 0) out vec4 outColor;
layout (location = 1) out float outFog;
layout (location = 2) out vec3 outNormal;
layout (location = 3) out vec3 outMicroPosition;

void main() {
  gl_Position = viewProjection * vec4(inPosition, 1.0);
  gl_Position.z = gl_Position.z * 0.5 + gl_Position.w * 0.5;
  outColor = inColor;
  outNormal = inNormal;
  outMicroPosition = inMicroPosition;

  float viewDepth = dot(inPosition - cameraPosition.xyz, cameraForward.xyz);
  float fogDensity = fogParams.x;
  float fog = clamp(1.0 - exp(-fogDensity * fogDensity * viewDepth * viewDepth), 0.0, fogParams.y);
  outFog = fog;
}
