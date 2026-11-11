#pragma once

namespace rigkit {
namespace ecs {
namespace shaders {

/**
 * @brief Lit mesh vertex + fragment GLSL, GLES2 and desktop GL 3.3 core variants.
 * @details Per-vertex position/normal/color in, per-fragment Lambert with optional
 * banded (posterized) shade and ordered (Bayer) dither on the band thresholds - see
 * `SMeshPresent3D.cpp` for the uniform contract (`uMvp`, `uModel`, `uLight*`, `uAmbient`,
 * `uBanded`, `uBands`, `uDither`, `uLightCone`). Spot mode uses `uLightPos` +
 * `uLightDir` with `uLightCone` = (cosInner, cosOuter, range). Kept as plain GLSL
 * text so any pack presenting `CMesh` + `CLight` can reuse the same lighting model
 * without re-deriving it.
 */

inline constexpr const char* kMeshVertexGLES = R"GLSL(#version 100
attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec4 aColor;
uniform mat4 uMvp;
uniform mat4 uModel;
varying vec3 vNormal;
varying vec3 vWorldPos;
varying vec4 vColor;
void main() {
	vec4 world = uModel * vec4(aPos, 1.0);
	vWorldPos = world.xyz;
	vNormal = mat3(uModel) * aNormal;
	vColor = aColor;
	gl_Position = uMvp * vec4(aPos, 1.0);
}
)GLSL";

inline constexpr const char* kMeshFragmentGLES = R"GLSL(#version 100
precision mediump float;
uniform vec3 uLightDir;
uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform float uLightIntensity;
uniform float uAmbient;
uniform float uLightMode;
uniform float uBanded;
uniform float uBands;
uniform float uDither;
uniform vec3 uLightCone;
varying vec3 vNormal;
varying vec3 vWorldPos;
varying vec4 vColor;
float bayer4(vec2 p) {
	vec2 xy = mod(floor(p), 4.0);
	float idx = xy.y * 4.0 + xy.x;
	if (idx < 0.5) return 0.0/16.0;
	if (idx < 1.5) return 8.0/16.0;
	if (idx < 2.5) return 2.0/16.0;
	if (idx < 3.5) return 10.0/16.0;
	if (idx < 4.5) return 12.0/16.0;
	if (idx < 5.5) return 4.0/16.0;
	if (idx < 6.5) return 14.0/16.0;
	if (idx < 7.5) return 6.0/16.0;
	if (idx < 8.5) return 3.0/16.0;
	if (idx < 9.5) return 11.0/16.0;
	if (idx < 10.5) return 1.0/16.0;
	if (idx < 11.5) return 9.0/16.0;
	if (idx < 12.5) return 15.0/16.0;
	if (idx < 13.5) return 7.0/16.0;
	if (idx < 14.5) return 13.0/16.0;
	return 5.0/16.0;
}
void main() {
	vec3 albedo = vColor.rgb;
	if (uLightMode < 0.5) {
		gl_FragColor = vColor;
		return;
	}
	vec3 N = normalize(vNormal);
	vec3 L;
	float atten = 1.0;
	if (uLightMode > 2.5) {
		vec3 toL = uLightPos - vWorldPos;
		float dist = length(toL);
		L = dist > 0.0001 ? toL / dist : vec3(0.0, 1.0, 0.0);
		if (uLightCone.z > 0.0) {
			float t = clamp(dist / uLightCone.z, 0.0, 1.0);
			atten *= (1.0 - t) * (1.0 - t);
		}
		vec3 axis = normalize(uLightDir);
		float cosTheta = clamp(dot(-L, axis), -1.0, 1.0);
		float denom = max(uLightCone.x - uLightCone.y, 0.0001);
		atten *= clamp((cosTheta - uLightCone.y) / denom, 0.0, 1.0);
	} else if (uLightMode > 1.5) {
		L = normalize(uLightPos - vWorldPos);
	} else {
		L = normalize(-uLightDir);
	}
	float ndl = max(dot(N, L), 0.0) * uLightIntensity * atten;
	ndl = clamp(ndl, 0.0, 1.0);
	float shade;
	if (uBanded > 0.5) {
		float b = max(uBands, 2.0);
		float maxT = b - 1.0;
		float t = ndl * maxT;
		float step;
		if (uDither > 0.5) {
			float lo = floor(t);
			float hi = min(lo + 1.0, maxT);
			float frac = t - lo;
			step = (bayer4(gl_FragCoord.xy) < frac) ? hi : lo;
		} else {
			step = floor(t + 0.0001);
			if (step > maxT) step = maxT;
		}
		float minStep = floor(uAmbient * maxT + 0.0001);
		step = max(step, minStep);
		shade = mix(uAmbient, 1.0, (maxT > 0.0) ? (step / maxT) : 1.0);
	} else {
		shade = uAmbient + (1.0 - uAmbient) * ndl;
	}
	vec3 lit = albedo * uLightColor * shade;
	gl_FragColor = vec4(lit, vColor.a);
}
)GLSL";

inline constexpr const char* kMeshVertexGL = R"GLSL(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec4 aColor;
uniform mat4 uMvp;
uniform mat4 uModel;
out vec3 vNormal;
out vec3 vWorldPos;
out vec4 vColor;
void main() {
	vec4 world = uModel * vec4(aPos, 1.0);
	vWorldPos = world.xyz;
	vNormal = mat3(uModel) * aNormal;
	vColor = aColor;
	gl_Position = uMvp * vec4(aPos, 1.0);
}
)GLSL";

inline constexpr const char* kMeshFragmentGL = R"GLSL(#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec4 vColor;
out vec4 FragColor;
uniform vec3 uLightDir;
uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform float uLightIntensity;
uniform float uAmbient;
uniform float uLightMode;
uniform float uBanded;
uniform float uBands;
uniform float uDither;
uniform vec3 uLightCone;
float bayer4(vec2 p) {
	ivec2 i = ivec2(mod(floor(p), 4.0));
	int idx = i.y * 4 + i.x;
	const float k[16] = float[](
		0.0/16.0, 8.0/16.0, 2.0/16.0, 10.0/16.0,
		12.0/16.0, 4.0/16.0, 14.0/16.0, 6.0/16.0,
		3.0/16.0, 11.0/16.0, 1.0/16.0, 9.0/16.0,
		15.0/16.0, 7.0/16.0, 13.0/16.0, 5.0/16.0);
	return k[idx];
}
void main() {
	vec3 albedo = vColor.rgb;
	if (uLightMode < 0.5) {
		FragColor = vColor;
		return;
	}
	vec3 N = normalize(vNormal);
	vec3 L;
	float atten = 1.0;
	if (uLightMode > 2.5) {
		vec3 toL = uLightPos - vWorldPos;
		float dist = length(toL);
		L = dist > 0.0001 ? toL / dist : vec3(0.0, 1.0, 0.0);
		if (uLightCone.z > 0.0) {
			float t = clamp(dist / uLightCone.z, 0.0, 1.0);
			atten *= (1.0 - t) * (1.0 - t);
		}
		vec3 axis = normalize(uLightDir);
		float cosTheta = clamp(dot(-L, axis), -1.0, 1.0);
		float denom = max(uLightCone.x - uLightCone.y, 0.0001);
		atten *= clamp((cosTheta - uLightCone.y) / denom, 0.0, 1.0);
	} else if (uLightMode > 1.5) {
		L = normalize(uLightPos - vWorldPos);
	} else {
		L = normalize(-uLightDir);
	}
	float ndl = clamp(max(dot(N, L), 0.0) * uLightIntensity * atten, 0.0, 1.0);
	float shade;
	if (uBanded > 0.5) {
		float b = max(uBands, 2.0);
		float maxT = b - 1.0;
		float t = ndl * maxT;
		float step;
		if (uDither > 0.5) {
			float lo = floor(t);
			float hi = min(lo + 1.0, maxT);
			float frac = t - lo;
			step = (bayer4(gl_FragCoord.xy) < frac) ? hi : lo;
		} else {
			step = min(floor(t + 0.0001), maxT);
		}
		float minStep = floor(uAmbient * maxT + 0.0001);
		step = max(step, minStep);
		shade = mix(uAmbient, 1.0, (maxT > 0.0) ? (step / maxT) : 1.0);
	} else {
		shade = uAmbient + (1.0 - uAmbient) * ndl;
	}
	FragColor = vec4(albedo * uLightColor * shade, vColor.a);
}
)GLSL";

} // namespace shaders
} // namespace ecs
} // namespace rigkit
