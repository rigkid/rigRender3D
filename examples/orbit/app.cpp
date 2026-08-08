#include "app.h"

#include <algorithm>
#include <cmath>

#include <spdlog/spdlog.h>
#include "core/RigKitEngine.h"
#include "core/pack/MPack.h"
#include "packs/rigComponent/src/CLight.h"
#include "packs/rigComponent/src/COrbitDrive.h"
#include "packs/rigComponent/src/CSphere.h"
#include "packs/rigComponent/src/CTransform.h"
#include "packs/rigComponent/src/PrimitiveVertices3D.h"
#include "packs/rigComponent/src/rig.h"
#include "packs/rigComponent/src/rigComponent.h"
#include "packs/rigRender3D/src/rigRender3D.h"
#include "packs/rigSystems/src/rigSystems.h"

namespace {

/// Little-planet palette: dark ocean low, green midlands, snow caps high.
glm::vec4 planetColor(const glm::vec3& n) {
	const glm::vec3 ocean{0.10f, 0.28f, 0.55f};
	const glm::vec3 land{0.20f, 0.55f, 0.30f};
	const glm::vec3 snow{0.92f, 0.95f, 0.98f};
	glm::vec3 c;
	if (n.y < -0.15f) {
		c = ocean;
	} else if (n.y < 0.45f) {
		c = glm::mix(ocean, land, std::clamp((n.y + 0.15f) / 0.60f, 0.f, 1.f));
	} else {
		c = glm::mix(land, snow, std::clamp((n.y - 0.45f) / 0.55f, 0.f, 1.f));
	}
	return {c.r, c.g, c.b, 1.0f};
}

/**
 * @brief One-off hand-colored planet: bakes `CSphere`'s own tessellation into a
 * `CMesh` so each face can carry its own latitude-mapped color.
 * @details The generic `rig::makeSphere` stays flat-filled (live `CSphere` data,
 * no per-face colors) — reusing `verticesOf` here means this bespoke look never
 * forks the icosahedron math, just the paint job.
 */
entt::entity makePlanet(rigkit::MEcs& ecs, float radius, int subdivisions,
						const std::string& name) {
	rigkit::ecs::CSphere shape;
	shape.radius = radius;
	shape.subdivisions = subdivisions;
	auto positions = rigkit::ecs::verticesOf(shape);

	auto e = rig::makeMesh(ecs, positions, {}, rigkit::ecs::CMesh::Mode::Triangles,
						   rig::fill(0.8f, 0.8f, 0.8f), name);
	auto& mesh = ecs.getComponent<rigkit::ecs::CMesh>(e);
	mesh.faceColors.reserve(positions.size() / 3);
	for (std::size_t i = 0; i + 2 < positions.size(); i += 3) {
		const glm::vec3 centroid = (positions[i] + positions[i + 1] + positions[i + 2]) / 3.0f;
		mesh.faceColors.push_back(planetColor(glm::normalize(centroid)));
	}
	return e;
}

} // namespace

void OrbitApp::setup() {
	spdlog::info("orbit — rigRender3D low-poly planet system (camera + light + mesh present)");
	m_engine->setClearColor(0.03f, 0.03f, 0.07f, 1.0f);

	auto* packs = m_engine->getPackManager();
	if (!packs) {
		return;
	}
	packs->registerPack(std::make_shared<rigkit::rigComponent>());
	packs->registerPack(std::make_shared<rigkit::rigSystems>());
	packs->registerPack(std::make_shared<rigkit::rigRender3D>());
	packs->initAll();
	packs->setupAll();

	auto* ecs = m_engine->getECSManager();
	if (!ecs) {
		return;
	}

	m_camera = rig::makeCamera(*ecs, {0.0f, 3.0f, 7.5f}, true, "orbit-camera");
	rigkit::ecs::COrbitDrive orbit;
	orbit.speed = 0.18f;
	orbit.radius = 7.5f;
	orbit.height = 3.0f;
	ecs->addComponent<rigkit::ecs::COrbitDrive>(m_camera, orbit);
	rig::lookAt(ecs->getComponent<rigkit::ecs::CTransform>(m_camera), {0.0f, orbit.height, orbit.radius},
				{0.0f, 0.0f, 0.0f});

	// Point light so it reads as a moving sun sweeping bands across the facets.
	m_light =
		rig::makeLight(*ecs, {3.5f, 2.5f, 3.5f}, rigkit::ecs::CLight::Type::Point, "orbit-light");
	auto& lightData = ecs->getComponent<rigkit::ecs::CLight>(m_light);
	lightData.intensity = 1.3f;
	lightData.ambient = 0.30f;
	lightData.banded = true;
	lightData.bands = 4;
	lightData.colorR = 1.0f;
	lightData.colorG = 0.97f;
	lightData.colorB = 0.90f;

	// One subdivision keeps the planet low-poly (80 faces) but round enough to read as a globe.
	m_planet = makePlanet(*ecs, 1.4f, 1, "orbit-planet");
	// Plain rig::makeSphere: flat CDrawStyle fill, live CSphere data, no per-face paint job.
	m_moonRock = rig::makeSphere(*ecs, 0.4f, 0, rig::fill(0.60f, 0.58f, 0.55f), "orbit-moon-rock");
	m_moonBox = rig::makeMeshBox(*ecs, 0.5f, rig::fill(0.95f, 0.55f, 0.2f), "orbit-moon-box");
}

void OrbitApp::update(float dt) {
	m_time += dt;
	auto* ecs = m_engine->getECSManager();
	if (!ecs) {
		return;
	}

	if (m_camera != entt::null && ecs->hasComponent<rigkit::ecs::COrbitDrive>(m_camera)) {
		const auto& orbit = ecs->getComponent<rigkit::ecs::COrbitDrive>(m_camera);
		const float yaw = m_time * orbit.speed;
		const glm::vec3 eye{std::sin(yaw) * orbit.radius, orbit.height, std::cos(yaw) * orbit.radius};
		rig::lookAt(ecs->getComponent<rigkit::ecs::CTransform>(m_camera), eye, {0.0f, 0.0f, 0.0f});
	}

	if (m_light != entt::null && ecs->hasComponent<rigkit::ecs::CTransform>(m_light)) {
		auto& lx = ecs->getComponent<rigkit::ecs::CTransform>(m_light);
		const float t = m_time * 0.5f;
		lx.position = {std::cos(t) * 3.5f, 2.2f + 0.6f * std::sin(t * 1.3f), std::sin(t) * 3.5f};
	}

	if (m_planet != entt::null && ecs->hasComponent<rigkit::ecs::CTransform>(m_planet)) {
		auto& t = ecs->getComponent<rigkit::ecs::CTransform>(m_planet);
		t.setEulerRadians({0.0f, m_time * 0.22f, 0.0f});
	}

	if (m_moonRock != entt::null && ecs->hasComponent<rigkit::ecs::CTransform>(m_moonRock)) {
		auto& t = ecs->getComponent<rigkit::ecs::CTransform>(m_moonRock);
		const float a = m_time * 0.9f;
		t.position = {std::cos(a) * 2.6f, 0.5f * std::sin(a * 1.7f), std::sin(a) * 2.6f};
		t.setEulerRadians({m_time * 1.4f, m_time * 0.6f, 0.0f});
	}

	if (m_moonBox != entt::null && ecs->hasComponent<rigkit::ecs::CTransform>(m_moonBox)) {
		auto& t = ecs->getComponent<rigkit::ecs::CTransform>(m_moonBox);
		const float a = 2.1f - m_time * 0.55f;
		t.position = {std::cos(a) * 3.6f, 0.9f + 0.3f * std::sin(m_time * 0.8f), std::sin(a) * 3.6f};
		t.setEulerRadians(
			{0.4f * std::sin(m_time), m_time * 1.1f, 0.2f * std::cos(m_time * 0.7f)});
	}
}
