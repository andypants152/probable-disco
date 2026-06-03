#version 460

layout (binding = 0) uniform sampler2D subtitleTexture;

layout (location = 0) in vec2 inTexCoord;
layout (location = 1) in float inAlpha;
layout (location = 2) in float inSolidTest;
layout (location = 0) out vec4 outColor;

void main() {
  vec4 color = texture(subtitleTexture, inTexCoord);
  color.a *= inAlpha;
  if (inSolidTest > 0.5) {
    color = vec4(0.0, 1.0, 0.85, 0.85);
  }
  outColor = color;
}
