#version 460

layout (std140, binding = 0) uniform Overlay {
  vec4 rect;
  vec4 params;
};

layout (location = 0) in vec2 inPosition;
layout (location = 1) in vec2 inTexCoord;
layout (location = 0) out vec2 outTexCoord;
layout (location = 1) out float outAlpha;
layout (location = 2) out float outSolidTest;

void main() {
  vec2 position = mix(rect.xy, rect.zw, inPosition);
  gl_Position = vec4(position, 0.5, 1.0);
  outTexCoord = inTexCoord;
  outAlpha = clamp(params.x, 0.0, 1.0);
  outSolidTest = params.y;
}
