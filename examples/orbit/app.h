#pragma once
#include <entt/entt.hpp>
#include "core/U_core.h"

/**
 * @brief rigRender3D hero — a tiny low-poly planet system.
 * @details Faceted planet + two orbiting moons, a circling point light, and a
 * camera that orbits the whole scene via `COrbitDrive`. Shows off `CMesh`
 * face colors, `CLight` banded shade, and the GLES/desktop present path in one
 * frame.
 */
class OrbitApp : public rigkit::IApp {
  public:
	OrbitApp() {
		window().width = 960;
		window().height = 600;
		window().title = "rigRender3D — orbit";
	}
	void setup() override;
	void update(float dt) override;
	void draw() override {}

  private:
	entt::entity m_camera = entt::null;
	entt::entity m_light = entt::null;
	entt::entity m_planet = entt::null;
	entt::entity m_moonRock = entt::null;
	entt::entity m_moonBox = entt::null;
	float m_time = 0.f;
};
