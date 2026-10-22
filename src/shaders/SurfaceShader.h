#pragma once

namespace rigkit {
namespace ecs {
namespace shaders {

/**
 * @brief Metallic-roughness surface, GLES2 and GL 3.3.
 * @details Vertex stage holds the L2 irradiance (fragment uniform floor is 16 vec4).
 * Fragment stage is Cook-Torrance GGX plus that irradiance, a 3x3 PCF shadow, and
 * ACES into an 8-bit target. `SURF_TEX` / `SURF_OUT` are defined by the preamble.
 */

inline constexpr const char* kSurfaceVertexGLES = R"GLSL(#version 100
attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec2 aUv;
uniform mat4 uMvp;
uniform mat4 uModel;
uniform mat4 uLightViewProj;
uniform vec3 uCamPos;
uniform vec3 uSh0;
uniform vec3 uSh1;
uniform vec3 uSh2;
uniform vec3 uSh3;
uniform vec3 uSh4;
uniform vec3 uSh5;
uniform vec3 uSh6;
uniform vec3 uSh7;
uniform vec3 uSh8;
varying vec3 vNormal;
varying vec3 vWorld;
varying vec2 vUv;
varying vec4 vLightClip;
varying vec3 vIrradiance;
varying vec3 vSpecIrradiance;
varying vec3 vView;
vec3 shIrr(vec3 n) {
	float x = n.x;
	float y = n.y;
	float z = n.z;
	vec3 r = uSh0 * 0.282095;
	r += uSh1 * (0.488603 * y);
	r += uSh2 * (0.488603 * z);
	r += uSh3 * (0.488603 * x);
	r += uSh4 * (1.092548 * x * y);
	r += uSh5 * (1.092548 * y * z);
	r += uSh6 * (0.315392 * (3.0 * z * z - 1.0));
	r += uSh7 * (1.092548 * x * z);
	r += uSh8 * (0.546274 * (x * x - y * y));
	return max(r, vec3(0.0));
}
void main() {
	vec4 world = uModel * vec4(aPos, 1.0);
	vWorld = world.xyz;
	vNormal = mat3(uModel) * aNormal;
	vUv = aUv;
	vView = uCamPos - world.xyz;
	vLightClip = uLightViewProj * world;
	vec3 N = normalize(vNormal);
	vIrradiance = shIrr(N);
	vec3 V = normalize(vView);
	vSpecIrradiance = shIrr(reflect(-V, N));
	gl_Position = uMvp * vec4(aPos, 1.0);
}
)GLSL";

inline constexpr const char* kSurfaceVertexGL = R"GLSL(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUv;
uniform mat4 uMvp;
uniform mat4 uModel;
uniform mat4 uLightViewProj;
uniform vec3 uCamPos;
uniform vec3 uSh0;
uniform vec3 uSh1;
uniform vec3 uSh2;
uniform vec3 uSh3;
uniform vec3 uSh4;
uniform vec3 uSh5;
uniform vec3 uSh6;
uniform vec3 uSh7;
uniform vec3 uSh8;
out vec3 vNormal;
out vec3 vWorld;
out vec2 vUv;
out vec4 vLightClip;
out vec3 vIrradiance;
out vec3 vSpecIrradiance;
out vec3 vView;
vec3 shIrr(vec3 n) {
	float x = n.x;
	float y = n.y;
	float z = n.z;
	vec3 r = uSh0 * 0.282095;
	r += uSh1 * (0.488603 * y);
	r += uSh2 * (0.488603 * z);
	r += uSh3 * (0.488603 * x);
	r += uSh4 * (1.092548 * x * y);
	r += uSh5 * (1.092548 * y * z);
	r += uSh6 * (0.315392 * (3.0 * z * z - 1.0));
	r += uSh7 * (1.092548 * x * z);
	r += uSh8 * (0.546274 * (x * x - y * y));
	return max(r, vec3(0.0));
}
void main() {
	vec4 world = uModel * vec4(aPos, 1.0);
	vWorld = world.xyz;
	vNormal = mat3(uModel) * aNormal;
	vUv = aUv;
	vView = uCamPos - world.xyz;
	vLightClip = uLightViewProj * world;
	vec3 N = normalize(vNormal);
	vIrradiance = shIrr(N);
	vec3 V = normalize(vView);
	vSpecIrradiance = shIrr(reflect(-V, N));
	gl_Position = uMvp * vec4(aPos, 1.0);
}
)GLSL";

inline constexpr const char* kSurfaceFragmentPreambleGLES = R"GLSL(#version 100
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec3 vNormal;
varying vec3 vWorld;
varying vec2 vUv;
varying vec4 vLightClip;
varying vec3 vIrradiance;
varying vec3 vSpecIrradiance;
varying vec3 vView;
uniform sampler2D uAlbedo;
uniform sampler2D uShadow;
uniform sampler2D uAo;
#define SURF_TEX(s, uv) texture2D(s, uv)
#define SURF_OUT gl_FragColor
)GLSL";

inline constexpr const char* kSurfaceFragmentPreambleGL = R"GLSL(#version 330 core
in vec3 vNormal;
in vec3 vWorld;
in vec2 vUv;
in vec4 vLightClip;
in vec3 vIrradiance;
in vec3 vSpecIrradiance;
in vec3 vView;
uniform sampler2D uAlbedo;
uniform sampler2D uShadow;
uniform sampler2D uAo;
out vec4 fragColor;
#define SURF_TEX(s, uv) texture(s, uv)
#define SURF_OUT fragColor
)GLSL";

inline constexpr const char* kSurfaceFragmentBody = R"GLSL(
uniform vec4 uLightDirMode;
uniform vec4 uLightPosI;
uniform vec4 uLightColorAmb;
uniform vec4 uAlbedoMetal;
uniform vec4 uEmissiveRough;
uniform vec4 uShadowAo;
uniform vec4 uOriginSize;
uniform float uIblIntensity;
vec3 linearTex(vec3 c) {
	return pow(max(c, vec3(0.0)), vec3(2.2));
}
vec3 aces(vec3 x) {
	return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
float Dggx(float ndh, float a) {
	float a2 = a * a;
	float d = ndh * ndh * (a2 - 1.0) + 1.0;
	return a2 / (3.14159265 * d * d);
}
float Gschlick(float ndot, float k) {
	return ndot / (ndot * (1.0 - k) + k + 0.0001);
}
vec3 Fschlick(float vdh, vec3 F0) {
	return F0 + (1.0 - F0) * pow(1.0 - vdh, 5.0);
}
float shadowPcf(vec3 proj, float ndl) {
	if (uShadowAo.x < 0.5) {
		return 1.0;
	}
	if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) {
		return 1.0;
	}
	float bias = max(0.0025 * (1.0 - ndl), 0.001);
	float z = proj.z - bias;
	float t = uShadowAo.y;
	float s = 0.0;
	s += step(z, SURF_TEX(uShadow, proj.xy + vec2(-t, -t)).r);
	s += step(z, SURF_TEX(uShadow, proj.xy + vec2(0.0, -t)).r);
	s += step(z, SURF_TEX(uShadow, proj.xy + vec2(t, -t)).r);
	s += step(z, SURF_TEX(uShadow, proj.xy + vec2(-t, 0.0)).r);
	s += step(z, SURF_TEX(uShadow, proj.xy).r);
	s += step(z, SURF_TEX(uShadow, proj.xy + vec2(t, 0.0)).r);
	s += step(z, SURF_TEX(uShadow, proj.xy + vec2(-t, t)).r);
	s += step(z, SURF_TEX(uShadow, proj.xy + vec2(0.0, t)).r);
	s += step(z, SURF_TEX(uShadow, proj.xy + vec2(t, t)).r);
	return s / 9.0;
}
void main() {
	vec3 N = vNormal;
	float n2 = dot(N, N);
	if (n2 > 0.000001) {
		N *= inversesqrt(n2);
	} else {
		N = vec3(0.0, 1.0, 0.0);
	}
	vec3 V = vView;
	float v2 = dot(V, V);
	if (v2 > 0.000001) {
		V *= inversesqrt(v2);
	} else {
		V = vec3(0.0, 0.0, 1.0);
	}
	vec3 albedo = linearTex(SURF_TEX(uAlbedo, vUv).rgb) * uAlbedoMetal.rgb;
	float metallic = clamp(uAlbedoMetal.w, 0.0, 1.0);
	float rough = clamp(uEmissiveRough.w, 0.045, 1.0);
	vec3 F0 = mix(vec3(0.04), albedo, metallic);
	float ndv = max(dot(N, V), 0.001);
	vec3 amb = uLightColorAmb.rgb * uLightColorAmb.w;
	vec3 specAmb = vec3(0.0);
	if (uIblIntensity > 0.0) {
		amb = vIrradiance * uIblIntensity;
		specAmb = vSpecIrradiance * uIblIntensity;
	}
	vec3 kS = Fschlick(ndv, F0);
	vec3 kD = (1.0 - kS) * (1.0 - metallic);
	vec3 color = kD * albedo * amb + kS * specAmb;

	vec3 L;
	float atten = 1.0;
	if (uLightDirMode.w > 1.5) {
		vec3 toL = uLightPosI.xyz - vWorld;
		float dist = length(toL);
		L = dist > 0.0001 ? toL / dist : vec3(0.0, 1.0, 0.0);
		atten = 1.0 / (1.0 + dist * dist);
	} else if (uLightDirMode.w > 0.5) {
		L = normalize(-uLightDirMode.xyz);
	} else {
		L = vec3(0.0, 1.0, 0.0);
		atten = 0.0;
	}
	float ndl = max(dot(N, L), 0.0);
	if (atten > 0.0 && ndl > 0.0) {
		vec3 H = normalize(V + L);
		float ndh = max(dot(N, H), 0.0);
		float vdh = max(dot(V, H), 0.0);
		float a = rough * rough;
		float k = (rough + 1.0) * (rough + 1.0) / 8.0;
		float D = Dggx(ndh, a);
		float G = Gschlick(ndl, k) * Gschlick(ndv, k);
		vec3 F = Fschlick(vdh, F0);
		vec3 spec = (D * G * F) / (4.0 * ndv * ndl + 0.001);
		vec3 kd = (1.0 - F) * (1.0 - metallic);
		vec3 direct = (kd * albedo / 3.14159265 + spec) * ndl * uLightPosI.w * atten;
		vec3 proj = vLightClip.xyz / max(vLightClip.w, 0.0001);
		proj = proj * 0.5 + 0.5;
		color += direct * uLightColorAmb.rgb * shadowPcf(proj, ndl);
	}
	float ao = 1.0;
	if (uShadowAo.z > 0.5) {
		vec2 uv = (gl_FragCoord.xy - uOriginSize.xy) / max(uOriginSize.zw, vec2(1.0));
		ao = SURF_TEX(uAo, uv).r;
		color *= mix(1.0, ao, uShadowAo.w);
	}
	color += uEmissiveRough.rgb;
	vec3 mapped = aces(max(color, vec3(0.0)));
	SURF_OUT = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

inline constexpr const char* kDepthVertexGLES = R"GLSL(#version 100
attribute vec3 aPos;
uniform mat4 uMvp;
void main() {
	gl_Position = uMvp * vec4(aPos, 1.0);
}
)GLSL";

inline constexpr const char* kDepthVertexGL = R"GLSL(#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uMvp;
void main() {
	gl_Position = uMvp * vec4(aPos, 1.0);
}
)GLSL";

inline constexpr const char* kDepthFragmentGLES = R"GLSL(#version 100
void main() {}
)GLSL";

inline constexpr const char* kDepthFragmentGL = R"GLSL(#version 330 core
void main() {}
)GLSL";

inline constexpr const char* kNormalVertexGLES = R"GLSL(#version 100
attribute vec3 aPos;
attribute vec3 aNormal;
uniform mat4 uMvp;
uniform mat4 uModel;
varying vec3 vNormal;
void main() {
	vNormal = mat3(uModel) * aNormal;
	gl_Position = uMvp * vec4(aPos, 1.0);
}
)GLSL";

inline constexpr const char* kNormalVertexGL = R"GLSL(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
uniform mat4 uMvp;
uniform mat4 uModel;
out vec3 vNormal;
void main() {
	vNormal = mat3(uModel) * aNormal;
	gl_Position = uMvp * vec4(aPos, 1.0);
}
)GLSL";

inline constexpr const char* kNormalFragmentGLES = R"GLSL(#version 100
precision mediump float;
varying vec3 vNormal;
void main() {
	vec3 n = normalize(vNormal);
	gl_FragColor = vec4(n * 0.5 + 0.5, 1.0);
}
)GLSL";

inline constexpr const char* kNormalFragmentGL = R"GLSL(#version 330 core
in vec3 vNormal;
out vec4 fragColor;
void main() {
	vec3 n = normalize(vNormal);
	fragColor = vec4(n * 0.5 + 0.5, 1.0);
}
)GLSL";

inline constexpr const char* kAoVertexGLES = R"GLSL(#version 100
attribute vec2 aPos;
varying vec2 vUv;
void main() {
	vUv = aPos * 0.5 + 0.5;
	gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

inline constexpr const char* kAoVertexGL = R"GLSL(#version 330 core
layout (location = 0) in vec2 aPos;
out vec2 vUv;
void main() {
	vUv = aPos * 0.5 + 0.5;
	gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

inline constexpr const char* kAoFragmentGLES = R"GLSL(#version 100
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 vUv;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform sampler2D uNoise;
uniform mat4 uViewProj;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform float uRadius;
#define SURF_TEX(s, uv) texture2D(s, uv)
#define SURF_OUT gl_FragColor
)GLSL";

inline constexpr const char* kAoFragmentGL = R"GLSL(#version 330 core
in vec2 vUv;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform sampler2D uNoise;
uniform mat4 uViewProj;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform float uRadius;
out vec4 fragColor;
#define SURF_TEX(s, uv) texture(s, uv)
#define SURF_OUT fragColor
)GLSL";

inline constexpr const char* kAoFragmentBody = R"GLSL(
vec3 reconstruct(vec2 uv, float d) {
	vec4 ndc = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
	vec4 w = uInvViewProj * ndc;
	return w.xyz / max(w.w, 0.0001);
}
float occlude(vec3 pos, vec3 N, vec3 T, vec3 B, vec3 h) {
	vec3 sampleWorld = pos + (T * h.x + B * h.z + N * h.y) * uRadius;
	vec4 clip = uViewProj * vec4(sampleWorld, 1.0);
	vec2 uv = clip.xy / max(clip.w, 0.0001) * 0.5 + 0.5;
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
		return 0.0;
	}
	float d = SURF_TEX(uDepth, uv).r;
	vec3 hit = reconstruct(uv, d);
	float distHit = length(hit - uCamPos);
	float distSample = length(sampleWorld - uCamPos);
	float rangeFade = smoothstep(uRadius, uRadius * 0.15, length(hit - pos));
	if (distHit + 0.03 < distSample) {
		return rangeFade;
	}
	return 0.0;
}
void main() {
	float d = SURF_TEX(uDepth, vUv).r;
	if (d >= 0.999) {
		SURF_OUT = vec4(1.0);
		return;
	}
	vec3 pos = reconstruct(vUv, d);
	vec3 N = SURF_TEX(uNormal, vUv).rgb * 2.0 - 1.0;
	float n2 = dot(N, N);
	if (n2 > 0.000001) {
		N *= inversesqrt(n2);
	} else {
		N = vec3(0.0, 1.0, 0.0);
	}
	vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 T = normalize(cross(up, N));
	vec3 B = cross(N, T);
	vec3 rnd = SURF_TEX(uNoise, gl_FragCoord.xy / 4.0).xyz * 2.0 - 1.0;
	float ang = rnd.x * 3.14159265;
	float s = sin(ang);
	float c = cos(ang);
	vec3 Tr = T * c + B * s;
	vec3 Br = -T * s + B * c;
	float occ = 0.0;
	occ += occlude(pos, N, Tr, Br, vec3(0.15, 0.85, 0.10));
	occ += occlude(pos, N, Tr, Br, vec3(-0.45, 0.55, 0.20));
	occ += occlude(pos, N, Tr, Br, vec3(0.55, 0.40, -0.25));
	occ += occlude(pos, N, Tr, Br, vec3(-0.10, 0.70, -0.55));
	occ += occlude(pos, N, Tr, Br, vec3(0.30, 0.25, 0.70));
	occ += occlude(pos, N, Tr, Br, vec3(-0.70, 0.35, -0.15));
	occ += occlude(pos, N, Tr, Br, vec3(0.05, 0.95, -0.05));
	occ += occlude(pos, N, Tr, Br, vec3(-0.25, 0.45, 0.55));
	float ao = 1.0 - occ / 8.0;
	SURF_OUT = vec4(ao, ao, ao, 1.0);
}
)GLSL";

} // namespace shaders
} // namespace ecs
} // namespace rigkit
