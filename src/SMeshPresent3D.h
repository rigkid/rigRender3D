#pragma once

#include <cstddef>
#include <entt/entt.hpp>
#include "ecs/MEcs.h"

namespace rigkit {
namespace ecs {

/**
 * @brief Present `CMesh` entities through the active `CCamera` (GLES / desktop GL).
 * @details Calls `SHierarchy` first unless @p resolveHierarchy is false (caller already
 * filled `CTransform::world`). No-op without an active camera.
 * @param viewportW Present width in framebuffer pixels (aspect + glViewport size).
 * @param viewportH Present height in framebuffer pixels.
 * @param resolveHierarchy When false, skip `SHierarchy` (panel / offscreen present).
 * @param only When non-null and count > 0, only these entities are drawn.
 * @param originX Framebuffer X of the GL viewport (bed origin; default full window).
 * @param originY Framebuffer Y of the GL viewport (OpenGL bottom-left space).
 */
void SMeshPresent3D(MEcs& ecs, int viewportW, int viewportH, bool resolveHierarchy = true,
					const entt::entity* only = nullptr, std::size_t onlyCount = 0,
					int originX = 0, int originY = 0);

/**
 * @brief Widest aliased line this GL driver will draw, in pixels.
 * @details Line meshes take their width from `CDrawStyle::strokeWidth` clamped to
 * this. Core-profile desktop and GLES parts (Pi included) commonly report 1, i.e.
 * hairlines only — ask before offering a width control so it cannot promise more
 * than the device draws. Queried once; needs a current GL context.
 */
float maxAliasedLineWidth();

} // namespace ecs
} // namespace rigkit
