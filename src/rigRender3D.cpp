#include "rigRender3D.h"

#include "core/IMui.h"
#include "core/pack/PackRegistry.h"
#include "core/RigKitEngine.h"
#include "ecs/MEcs.h"
#include "ecs/SystemRegistry.h"
#include "SMeshPresent3D.h"

#include <spdlog/spdlog.h>

namespace rigkit {

rigRender3D::rigRender3D() : IPack("rigRender3D") {
	

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
		// Match glViewport / HiDPI framebuffer - window size alone letterboxes on Retina.
		const int fullW = fbW > 0 ? fbW : engine->getWindowWidth();
		const int fullH = fbH > 0 ? fbH : engine->getWindowHeight();

		int presentW = fullW;
		int presentH = fullH;
		int originX = 0;
		int originY = 0;

		if (auto* ui = engine->getUiManager()) {
			float cx = 0.f, cy = 0.f, cw = 0.f, ch = 0.f;
			if (ui->dockPassthroughCentral() && ui->centralViewRect(cx, cy, cw, ch)) {
				const int winW = engine->getWindowWidth();
				const int winH = engine->getWindowHeight();
				if (winW > 0 && winH > 0 && cw > 1.f && ch > 1.f) {
					const float scaleX = static_cast<float>(fullW) / static_cast<float>(winW);
					const float scaleY = static_cast<float>(fullH) / static_cast<float>(winH);
					presentW = static_cast<int>(cw * scaleX + 0.5f);
					presentH = static_cast<int>(ch * scaleY + 0.5f);
					originX = static_cast<int>(cx * scaleX + 0.5f);
					// ImGui / GLFW Y is top-left; OpenGL viewport Y is bottom-left.
					originY = static_cast<int>(
						static_cast<float>(fullH) - (cy + ch) * scaleY + 0.5f);
					if (presentW < 1) {
						presentW = 1;
					}
					if (presentH < 1) {
						presentH = 1;
					}
				}
			}
		}

		ecs::SMeshPresent3D(e, presentW, presentH, true, nullptr, 0, originX, originY);
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
