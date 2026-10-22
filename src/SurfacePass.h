#pragma once

/**
 * @file
 * @brief Metallic-roughness present for entities that carry `CRenderMaterial`.
 * @details Meshes without that component stay on the Lambert path. This pass adds
 * an albedo texture, GGX, emissive, ACES into the 8-bit target, L2 irradiance from
 * `CRenderEnvironment`, a directional 3x3 PCF shadow, and SSAO. Shadow and SSAO
 * turn off when the GL context cannot allocate a depth texture. Specular from the
 * HDR is the same low-order lobe, not a prefiltered cubemap. Point lights are not
 * shadowed.
 */

#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include "CMesh.h"
#include "CRenderMaterial.h"
#include "CTransform.h"
#include "ecs/MEcs.h"

namespace rigkit {
namespace ecs {

struct SurfaceLight {
	int mode = 0;
	glm::vec3 dir{0.f, -1.f, 0.f};
	glm::vec3 pos{0.f};
	glm::vec3 color{1.f};
	float intensity = 1.f;
	float ambient = 0.35f;
};

struct SurfaceFrame {
	bool shadow = false;
	unsigned shadowTex = 0;
	glm::mat4 lightViewProj{1.f};
	bool ao = false;
	unsigned aoTex = 0;
	bool ibl = false;
	glm::vec3 sh[9]{};
	float iblIntensity = 1.f;
	glm::vec3 cameraPos{0.f};
	glm::vec2 origin{0.f};
	glm::vec2 presentSize{1.f};
};

/** @brief True when any entity carries `CRenderMaterial`. */
bool sceneHasSurface(MEcs& ecs);

/**
 * @brief Compile the surface programs.
 * @return False when the surface program fails. Shadow and SSAO may still be off
 *         afterwards; that does not fail this call.
 */
bool surfaceShadersOk();

/** @brief True after a failed surface compile, or a missing albedo / HDRI file. */
bool surfacePresentFailed();

/**
 * @brief Shadow map, SSAO, and irradiance for one present.
 * @details Call after the present framebuffer and viewport are bound. Restores both.
 */
SurfaceFrame beginSurfaceFrame(MEcs& ecs, const SurfaceLight& light, const glm::mat4& viewProj,
							   const glm::mat4& invViewProj, const glm::vec3& cameraPos, int presentW,
							   int presentH, int originX, int originY);

/** @brief Draw one triangle mesh with the surface program. Lines stay on Lambert. */
void drawSurfaceMesh(MEcs& ecs, entt::entity id, const CTransform& transform, const CMesh& mesh,
					 const CRenderMaterial& material, const glm::mat4& viewProj,
					 const SurfaceLight& light, const SurfaceFrame& frame);

} // namespace ecs
} // namespace rigkit
