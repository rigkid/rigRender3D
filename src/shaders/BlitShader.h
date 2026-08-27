#pragma once

namespace rigkit {
namespace ecs {
namespace shaders {

/**
 * @brief Fullscreen textured-quad blit, GLES2 and desktop GL 3.3 core variants.
 * @details Draws `uTex` over clip space unchanged - no transform, no lighting.
 * Generic enough for any pack that renders to an offscreen color texture (a
 * pixel-present FBO, a compositor layer, ...) and needs to present it to the
 * default framebuffer; not specific to mesh present.
 */

inline constexpr const char* kBlitVertexGLES = R"GLSL(#version 100
attribute vec2 aPos;
attribute vec2 aUv;
varying vec2 vUv;
void main() {
	vUv = aUv;
	gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

inline constexpr const char* kBlitFragmentGLES = R"GLSL(#version 100
precision mediump float;
uniform sampler2D uTex;
varying vec2 vUv;
void main() {
	gl_FragColor = texture2D(uTex, vUv);
}
)GLSL";

inline constexpr const char* kBlitVertexGL = R"GLSL(#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
	vUv = aUv;
	gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

inline constexpr const char* kBlitFragmentGL = R"GLSL(#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uTex;
void main() {
	FragColor = texture(uTex, vUv);
}
)GLSL";

} // namespace shaders
} // namespace ecs
} // namespace rigkit
