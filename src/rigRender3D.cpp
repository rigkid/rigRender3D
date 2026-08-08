#include "rigRender3D.h"
#include <spdlog/spdlog.h>
#include "SMeshPresent3D.h"
#include "core/RigKitEngine.h"
#include "core/pack/PackRegistry.h"
#include "ecs/MEcs.h"
#include "ecs/SystemRegistry.h"

namespace rigkit {

rigRender3D::rigRender3D() : IPack("rigRender3D") {
	setDescription("GLES mesh present over CCamera + CMesh (Pi floor)");
	addDependency("rigComponent");
	addDependency("rigSystems");
}

bool rigRender3D::init() {
	spdlog::info("[rigRender3D] init");
	return true;
}

void rigRender3D::setup() {
	auto* engine = getEngine();
	if (!engine) {
		return;
	}
	auto* ecs = engine->getECSManager();
	if (!ecs) {
		return;
	}

	ecs->registerSystem("SMeshPresent3D", SystemPhase::Draw, [engine](MEcs& e) {
		int designW = 0;
		int designH = 0;
		int fbW = 0;
		int fbH = 0;
		engine->getPresentSize(designW, designH, fbW, fbH);
		// Match glViewport / HiDPI framebuffer — window size alone letterboxes on Retina.
		const int w = fbW > 0 ? fbW : engine->getWindowWidth();
		const int h = fbH > 0 ? fbH : engine->getWindowHeight();
		ecs::SMeshPresent3D(e, w, h);
	});

	spdlog::info("[rigRender3D] registered Draw SMeshPresent3D");
}

} // namespace rigkit

namespace {
struct rigRender3DRegistrar {
	rigRender3DRegistrar() {
		rigkit::PackRegistry::instance().addFactory("rigRender3D", []() {
			return std::shared_ptr<rigkit::IPack>(std::make_shared<rigkit::rigRender3D>());
		});
	}
};
static rigRender3DRegistrar rigRender3D_auto_reg;
} // namespace
