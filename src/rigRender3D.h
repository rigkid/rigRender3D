#pragma once

#include "core/pack/IPack.h"

namespace rigkit {

/**
 * @brief Code pack — GLES mesh present from `CCamera` + `CMesh` + `CDrawStyle`.
 * @details Depends on `rigComponent` (data) and `rigSystems` (`SHierarchy`).
 * Optional `CLight` drives banded or smooth shade. Same GLES2 path as the Pi host.
 */
class rigRender3D : public IPack {
  public:
	rigRender3D();
	bool init() override;
	void setup() override;
};

} // namespace rigkit
