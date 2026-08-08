#include "app.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <spdlog/spdlog.h>
#include "core/RigKitEngine.h"
#include "core/pack/MPack.h"
#include "packs/rigComponent/src/CLight.h"
#include "packs/rigComponent/src/COrbitDrive.h"
#include "packs/rigComponent/src/CTransform.h"
#include "packs/rigComponent/src/rig.h"
#include "packs/rigComponent/src/rigComponent.h"
#include "packs/rigRender3D/src/rigRender3D.h"
#include "packs/rigSystems/src/rigSystems.h"

namespace {

using Face = std::array<glm::vec3, 3>;

/// Regular icosahedron, unit radius, as 20 loose (non-indexed) triangles.
std::vector<Face> icosahedronFaces() {
	const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
	const std::array<glm::vec3, 12> v = {
		glm::normalize(glm::vec3{-1.f, t, 0.f}), glm::normalize(glm::vec3{1.f, t, 0.f}),
		glm::normalize(glm::vec3{-1.f, -t, 0.f}), glm::normalize(glm::vec3{1.f, -t, 0.f}),
		glm::normalize(glm::vec3{0.f, -1.f, t}), glm::normalize(glm::vec3{0.f, 1.f, t}),
		glm::normalize(glm::vec3{0.f, -1.f, -t}), glm::normalize(glm::vec3{0.f, 1.f, -t}),
		glm::normalize(glm::vec3{t, 0.f, -1.f}), glm::normalize(glm::vec3{t, 0.f, 1.f}),
		glm::normalize(glm::vec3{-t, 0.f, -1.f}), glm::normalize(glm::vec3{-t, 0.f, 1.f}),
	};
	static constexpr int kIdx[20][3] = {
		{0, 11, 5}, {0, 5, 1},	{0, 1, 7},	{0, 7, 10}, {0, 10, 11}, {1, 5, 9},	 {5, 11, 4},
		{11, 10, 2}, {10, 7, 6}, {7, 1, 8},	{3, 9, 4},	{3, 4, 2},	{3, 2, 6},	 {3, 6, 8},
		{3, 8, 9},	{4, 9, 5},	{2, 4, 11}, {6, 2, 10}, {8, 6, 7},	{9, 8, 1},
	};
	std::vector<Face> faces;
	faces.reserve(20);
	for (const auto& f : kIdx) {
		faces.push_back({v[static_cast<size_t>(f[0])], v[static_cast<size_t>(f[1])],
						  v[static_cast<size_t>(f[2])]});
	}
	return faces;
}

/// Split each triangle into 4 at normalized edge midpoints (one sphere-ward step).
std::vector<Face> subdivide(const std::vector<Face>& faces) {
	std::vector<Face> out;
	out.reserve(faces.size() * 4);
	for (const auto& f : faces) {
		const glm::vec3 ab = glm::normalize(f[0] + f[1]);
		const glm::vec3 bc = glm::normalize(f[1] + f[2]);
		const glm::vec3 ca = glm::normalize(f[2] + f[0]);
		out.push_back({f[0], ab, ca});
		out.push_back({ab, f[1], bc});
		out.push_back({ca, bc, f[2]});
		out.push_back({ab, bc, ca});
	}
	return out;
}

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

/// Flat grey rock, a touch lighter on top — reads as tumbling debris, not a ball.
glm::vec4 rockColor(const glm::vec3& n) {
	const float shade = 0.5f + 0.18f * n.y;
	return {shade * 0.66f, shade * 0.62f, shade * 0.58f, 1.0f};
}

/// Build a faceted sphere entity: `subdivisions` icosahedron refinements, colored per face.
template <typename ColorFn>
entt::entity makeIcosphere(rigkit::MEcs& ecs, int subdivisions, float radius, ColorFn colorAt,
							const std::string& name) {
	auto faces = icosahedronFaces();
	for (int i = 0; i < subdivisions; ++i) {
		faces = subdivide(faces);
	}

	std::vector<glm::vec3> positions;
	positions.reserve(faces.size() * 3);
	for (const auto& f : faces) {
		positions.push_back(f[0] * radius);
		positions.push_back(f[1] * radius);
		positions.push_back(f[2] * radius);
	}

	auto e = rig::makeMesh(ecs, std::move(positions), {}, rigkit::ecs::CMesh::Mode::Triangles,
						   rig::fill(0.8f, 0.8f, 0.8f), name);
	auto& mesh = ecs.getComponent<rigkit::ecs::CMesh>(e);
	mesh.faceColors.reserve(faces.size());
	for (const auto& f : faces) {
		mesh.faceColors.push_back(colorAt(glm::normalize(f[0] + f[1] + f[2])));
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
	m_planet = makeIcosphere(*ecs, 1, 1.4f, planetColor, "orbit-planet");
	m_moonRock = makeIcosphere(*ecs, 0, 0.4f, rockColor, "orbit-moon-rock");
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
