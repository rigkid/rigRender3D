#pragma once

/**
 * @file
 * @brief L2 spherical harmonics for a lat-long HDRI.
 * @details Coefficients are cosine-convolved, so `shEval` is irradiance, not
 * radiance. A uniform radiance of 1 evaluates to pi. No GL.
 */

#include <glm/glm.hpp>
#include <cmath>
#include <cstddef>

namespace rigkit {
namespace ecs {

struct Sh9 {
	glm::vec3 band[9]{};
};

inline float shBasis(int i, const glm::vec3& n) {
	const float x = n.x;
	const float y = n.y;
	const float z = n.z;
	switch (i) {
	case 0:
		return 0.282095f;
	case 1:
		return 0.488603f * y;
	case 2:
		return 0.488603f * z;
	case 3:
		return 0.488603f * x;
	case 4:
		return 1.092548f * x * y;
	case 5:
		return 1.092548f * y * z;
	case 6:
		return 0.315392f * (3.f * z * z - 1.f);
	case 7:
		return 1.092548f * x * z;
	case 8:
		return 0.546274f * (x * x - y * y);
	default:
		return 0.f;
	}
}

inline glm::vec3 shEval(const Sh9& sh, const glm::vec3& n) {
	glm::vec3 r(0.f);
	for (int i = 0; i < 9; ++i) {
		r += sh.band[i] * shBasis(i, n);
	}
	return r;
}

/**
 * @brief Project a lat-long RGBA float image to irradiance SH.
 * @param rgba Four floats per pixel, row 0 at +Y. Width and height at least 1.
 * @return Cosine-convolved coefficients. Empty input returns zeros.
 */
inline Sh9 shFromLatlong(const float* rgba, int w, int h) {
	Sh9 radiance{};
	if (!rgba || w < 1 || h < 1) {
		return {};
	}
	constexpr float kPi = 3.14159265f;
	const float dTheta = kPi / static_cast<float>(h);
	const float dPhi = (2.f * kPi) / static_cast<float>(w);
	for (int y = 0; y < h; ++y) {
		const float theta = (static_cast<float>(y) + 0.5f) * dTheta;
		const float sinT = std::sin(theta);
		const float cosT = std::cos(theta);
		const float weight = sinT * dTheta * dPhi;
		for (int x = 0; x < w; ++x) {
			const float phi = (static_cast<float>(x) + 0.5f) * dPhi;
			const glm::vec3 dir(sinT * std::sin(phi), cosT, sinT * std::cos(phi));
			const float* p =
				rgba +
				(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
			const glm::vec3 rad(p[0], p[1], p[2]);
			for (int i = 0; i < 9; ++i) {
				radiance.band[i] += rad * shBasis(i, dir) * weight;
			}
		}
	}
	// Band scales that turn radiance SH into irradiance (Ramamoorthi).
	constexpr float kA0 = kPi;
	constexpr float kA1 = 2.f * kPi / 3.f;
	constexpr float kA2 = kPi / 4.f;
	constexpr float kBand[9] = {kA0, kA1, kA1, kA1, kA2, kA2, kA2, kA2, kA2};
	Sh9 irradiance{};
	for (int i = 0; i < 9; ++i) {
		irradiance.band[i] = radiance.band[i] * kBand[i];
	}
	return irradiance;
}

} // namespace ecs
} // namespace rigkit
