#include "SMeshPresent3D.h"

#include "rendering/U_gladGlfw.h"
#include "CCamera.h"
#include "CDrawStyle.h"
#include "CFaceSelection.h"
#include "CIndexedAtlas.h"
#include "CInstance.h"
#include "CLight.h"
#include "CMesh.h"
#include "CPalette.h"
#include "CRenderMaterial.h"
#include "CRenderVisibility.h"
#include "CSelection.h"
#include "CSphere.h"
#include "CTransform.h"
#include "CVertexSelection.h"
#include "MeshFaces.h"
#include "PrimitiveVertices3D.h"
#include "SHierarchy.h"
#include "SurfacePass.h"
#include "shaders/BlitShader.h"
#include "shaders/MeshShader.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>

namespace rigkit {
namespace ecs {
namespace {

float bayer4(int x, int y) {
	static constexpr float k[16] = {
		0.f / 16.f,	 8.f / 16.f, 2.f / 16.f,  10.f / 16.f, 12.f / 16.f, 4.f / 16.f,
		14.f / 16.f, 6.f / 16.f, 3.f / 16.f,  11.f / 16.f, 1.f / 16.f,	9.f / 16.f,
		15.f / 16.f, 7.f / 16.f, 13.f / 16.f, 5.f / 16.f,
	};
	return k[static_cast<size_t>(((y & 3) << 2) | (x & 3))];
}

bool compileShader(GLenum type, const char* src, GLuint& out) {
	out = glCreateShader(type);
	glShaderSource(out, 1, &src, nullptr);
	glCompileShader(out);
	GLint ok = 0;
	glGetShaderiv(out, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog(out, 512, nullptr, log);
		spdlog::error("[rigRender3D] shader: {}", log);
		glDeleteShader(out);
		out = 0;
		return false;
	}
	return true;
}

bool linkProgram(GLuint vs, GLuint fs, GLuint& out) {
	out = glCreateProgram();
	glAttachShader(out, vs);
	glAttachShader(out, fs);
	glLinkProgram(out);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(out, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetProgramInfoLog(out, 512, nullptr, log);
		spdlog::error("[rigRender3D] program link: {}", log);
		glDeleteProgram(out);
		out = 0;
		return false;
	}
	return true;
}

struct Present3DGl {
	GLuint program = 0;
	GLuint vao = 0;
	GLuint vbo = 0;
	GLint uMvp = -1;
	GLint uModel = -1;
	GLint uLightDir = -1;
	GLint uLightPos = -1;
	GLint uLightColor = -1;
	GLint uLightIntensity = -1;
	GLint uAmbient = -1;
	GLint uLightMode = -1;
	GLint uBanded = -1;
	GLint uBands = -1;
	GLint uDither = -1;

	GLuint blitProgram = 0;
	GLuint blitVao = 0;
	GLuint blitVbo = 0;
	GLint uBlitTex = -1;

	GLuint fbo = 0;
	GLuint colorTex = 0;
	GLuint depthRb = 0;
	int fboW = 0;
	int fboH = 0;

	struct MeshGpu {
		GLuint vbo = 0;
		GLsizei nVerts = 0;
		uint64_t stamp = 0;
		bool used = false;
	};
	std::unordered_map<uint32_t, MeshGpu> meshGpu;

	void bindMeshVbo(GLuint buf) {
		constexpr int kStride = 10;
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, buf);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride * sizeof(float),
							  reinterpret_cast<void*>(0));
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride * sizeof(float),
							  reinterpret_cast<void*>(3 * sizeof(float)));
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, kStride * sizeof(float),
							  reinterpret_cast<void*>(6 * sizeof(float)));
	}

	void sweepMeshGpu() {
		for (auto it = meshGpu.begin(); it != meshGpu.end();) {
			if (!it->second.used) {
				if (it->second.vbo) {
					glDeleteBuffers(1, &it->second.vbo);
				}
				it = meshGpu.erase(it);
			} else {
				it->second.used = false;
				++it;
			}
		}
	}

	bool ready = false;

	bool ensure() {
		if (ready) {
			return true;
		}

#if defined(RIGKIT_GLES)
		const char* vsSrc = shaders::kMeshVertexGLES;
		const char* fsSrc = shaders::kMeshFragmentGLES;
		const char* blitVs = shaders::kBlitVertexGLES;
		const char* blitFs = shaders::kBlitFragmentGLES;
#else
		const char* vsSrc = shaders::kMeshVertexGL;
		const char* fsSrc = shaders::kMeshFragmentGL;
		const char* blitVs = shaders::kBlitVertexGL;
		const char* blitFs = shaders::kBlitFragmentGL;
#endif

		GLuint vs = 0, fs = 0;
		if (!compileShader(GL_VERTEX_SHADER, vsSrc, vs) ||
			!compileShader(GL_FRAGMENT_SHADER, fsSrc, fs)) {
			return false;
		}
#if defined(RIGKIT_GLES)
		program = glCreateProgram();
		glAttachShader(program, vs);
		glAttachShader(program, fs);
		glBindAttribLocation(program, 0, "aPos");
		glBindAttribLocation(program, 1, "aNormal");
		glBindAttribLocation(program, 2, "aColor");
		glLinkProgram(program);
		glDeleteShader(vs);
		glDeleteShader(fs);
		GLint ok = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &ok);
		if (!ok) {
			char log[512];
			glGetProgramInfoLog(program, 512, nullptr, log);
			spdlog::error("[rigRender3D] program link: {}", log);
			glDeleteProgram(program);
			program = 0;
			return false;
		}
#else
		if (!linkProgram(vs, fs, program)) {
			return false;
		}
#endif

		uMvp = glGetUniformLocation(program, "uMvp");
		uModel = glGetUniformLocation(program, "uModel");
		uLightDir = glGetUniformLocation(program, "uLightDir");
		uLightPos = glGetUniformLocation(program, "uLightPos");
		uLightColor = glGetUniformLocation(program, "uLightColor");
		uLightIntensity = glGetUniformLocation(program, "uLightIntensity");
		uAmbient = glGetUniformLocation(program, "uAmbient");
		uLightMode = glGetUniformLocation(program, "uLightMode");
		uBanded = glGetUniformLocation(program, "uBanded");
		uBands = glGetUniformLocation(program, "uBands");
		uDither = glGetUniformLocation(program, "uDither");

		constexpr int kStride = 10;
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * kStride * 8192, nullptr, GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride * sizeof(float),
							  reinterpret_cast<void*>(0));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride * sizeof(float),
							  reinterpret_cast<void*>(3 * sizeof(float)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, kStride * sizeof(float),
							  reinterpret_cast<void*>(6 * sizeof(float)));
		glBindVertexArray(0);

		GLuint bvs = 0, bfs = 0;
		if (!compileShader(GL_VERTEX_SHADER, blitVs, bvs) ||
			!compileShader(GL_FRAGMENT_SHADER, blitFs, bfs)) {
			return false;
		}
#if defined(RIGKIT_GLES)
		blitProgram = glCreateProgram();
		glAttachShader(blitProgram, bvs);
		glAttachShader(blitProgram, bfs);
		glBindAttribLocation(blitProgram, 0, "aPos");
		glBindAttribLocation(blitProgram, 1, "aUv");
		glLinkProgram(blitProgram);
		glDeleteShader(bvs);
		glDeleteShader(bfs);
		GLint bok = 0;
		glGetProgramiv(blitProgram, GL_LINK_STATUS, &bok);
		if (!bok) {
			spdlog::error("[rigRender3D] blit program link failed");
			return false;
		}
#else
		if (!linkProgram(bvs, bfs, blitProgram)) {
			return false;
		}
#endif
		uBlitTex = glGetUniformLocation(blitProgram, "uTex");

		const float quad[] = {
			-1.f, -1.f, 0.f, 0.f, 1.f, -1.f, 1.f, 0.f, -1.f, 1.f, 0.f, 1.f,
			-1.f, 1.f,	0.f, 1.f, 1.f, -1.f, 1.f, 0.f, 1.f,	 1.f, 1.f, 1.f,
		};
		glGenVertexArrays(1, &blitVao);
		glGenBuffers(1, &blitVbo);
		glBindVertexArray(blitVao);
		glBindBuffer(GL_ARRAY_BUFFER, blitVbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
							  reinterpret_cast<void*>(0));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
							  reinterpret_cast<void*>(2 * sizeof(float)));
		glBindVertexArray(0);

		ready = true;
		spdlog::info("[rigRender3D] GL present ready (banded shade + dither + pixel FBO)");
		return true;
	}

	bool ensureFbo(int w, int h) {
		if (w <= 0 || h <= 0) {
			return false;
		}
		if (fbo != 0 && fboW == w && fboH == h) {
			return true;
		}
		if (colorTex) {
			glDeleteTextures(1, &colorTex);
			colorTex = 0;
		}
		if (depthRb) {
			glDeleteRenderbuffers(1, &depthRb);
			depthRb = 0;
		}
		if (fbo) {
			glDeleteFramebuffers(1, &fbo);
			fbo = 0;
		}

		glGenTextures(1, &colorTex);
		glBindTexture(GL_TEXTURE_2D, colorTex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

		glGenRenderbuffers(1, &depthRb);
		glBindRenderbuffer(GL_RENDERBUFFER, depthRb);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);

		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb);
		const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			spdlog::error("[rigRender3D] pixel FBO incomplete ({})", static_cast<int>(status));
			return false;
		}
		fboW = w;
		fboH = h;
		return true;
	}

	void blitToWindow(int windowW, int windowH) {
		GLint prevFbo = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		const float srcAspect = static_cast<float>(fboW) / static_cast<float>(fboH);
		const float dstAspect = static_cast<float>(windowW) / static_cast<float>(windowH);
		int vpX = 0, vpY = 0, vpW = windowW, vpH = windowH;
		if (dstAspect > srcAspect) {
			vpW = static_cast<int>(static_cast<float>(windowH) * srcAspect + 0.5f);
			vpX = (windowW - vpW) / 2;
		} else {
			vpH = static_cast<int>(static_cast<float>(windowW) / srcAspect + 0.5f);
			vpY = (windowH - vpH) / 2;
		}

		glViewport(vpX, vpY, vpW, vpH);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glUseProgram(blitProgram);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, colorTex);
		glUniform1i(uBlitTex, 0);
		glBindVertexArray(blitVao);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
	}
};

Present3DGl& glState() {
	static Present3DGl s;
	return s;
}

struct LightFrame {
	int mode = 0;
	glm::vec3 dir{0.0f, -1.0f, 0.0f};
	glm::vec3 pos{0.0f};
	glm::vec3 color{1.0f};
	float intensity = 1.0f;
	float ambient = 0.35f;
	bool banded = true;
	int bands = 3;
	bool dither = false;
};

LightFrame findLight(MEcs& ecs) {
	LightFrame out;
	auto lights = ecs.view<CTransform, CLight>();
	for (auto entity : lights) {
		const auto& light = lights.get<CLight>(entity);
		if (!light.enabled) {
			continue;
		}
		const auto& xf = lights.get<CTransform>(entity);
		out.color = light.color();
		out.intensity = light.intensity;
		out.ambient = light.ambient;
		out.banded = light.banded;
		out.bands = std::clamp(light.bands, 2, 4);
		out.dither = light.dither;
		if (light.type == CLight::Type::Point) {
			out.mode = 2;
			out.pos = CLight::position(xf);
		} else {
			out.mode = 1;
			out.dir = CLight::direction(xf);
		}
		break;
	}
	return out;
}

uint64_t mixU64(uint64_t h, uint64_t v) {
	h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
	return h;
}

uint64_t floatBits(float v) {
	uint32_t bits = 0;
	std::memcpy(&bits, &v, sizeof(bits));
	return bits;
}

uint64_t meshStamp(const CMesh& mesh, const CDrawStyle& style, const CPalette* palette,
				   const CIndexedAtlas* atlas) {
	uint64_t h = mixU64(mesh.revision, mesh.positions.size());
	h = mixU64(h, mesh.indices.size());
	h = mixU64(h, mesh.loops.size());
	h = mixU64(h, mesh.loopSizes.size());
	h = mixU64(h, mesh.faceColors.size());
	h = mixU64(h, mesh.facePalette.size());
	h = mixU64(h, mesh.texcoords.size());
	h = mixU64(h, static_cast<uint64_t>(mesh.mode));
	if (!mesh.positions.empty()) {
		h = mixU64(h, floatBits(mesh.positions.front().x));
		h = mixU64(h, floatBits(mesh.positions[mesh.positions.size() / 2].y));
		h = mixU64(h, floatBits(mesh.positions.back().z));
	}
	if (!mesh.indices.empty()) {
		h = mixU64(h, mesh.indices.front());
		h = mixU64(h, mesh.indices.back());
	}
	h = mixU64(h, floatBits(style.fillR));
	h = mixU64(h, floatBits(style.fillG));
	h = mixU64(h, floatBits(style.fillB));
	h = mixU64(h, floatBits(style.fillA));
	h = mixU64(h, style.hasFill ? 1ull : 0ull);
	h = mixU64(h, reinterpret_cast<uintptr_t>(palette));
	h = mixU64(h, reinterpret_cast<uintptr_t>(atlas));
	return h;
}

void pushVert(std::vector<float>& verts, const glm::vec3& p, const glm::vec3& n,
			  const glm::vec4& c) {
	verts.push_back(p.x);
	verts.push_back(p.y);
	verts.push_back(p.z);
	verts.push_back(n.x);
	verts.push_back(n.y);
	verts.push_back(n.z);
	verts.push_back(c.r);
	verts.push_back(c.g);
	verts.push_back(c.b);
	verts.push_back(c.a);
}

glm::vec3 faceNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
	const glm::vec3 n = glm::cross(b - a, c - a);
	const float len = glm::length(n);
	if (len < 1e-8f) {
		return {0.0f, 0.0f, 1.0f};
	}
	return n / len;
}

glm::vec4 faceColor(const CMesh& mesh, const CDrawStyle& style, size_t faceIndex) {
	if (faceIndex < mesh.faceColors.size()) {
		return mesh.faceColors[faceIndex];
	}
	return {style.fillR, style.fillG, style.fillB, style.fillA};
}

const CPalette* findPalette(MEcs& ecs) {
	auto view = ecs.view<CPalette>();
	for (auto entity : view) {
		return &view.get<CPalette>(entity);
	}
	return nullptr;
}

const CIndexedAtlas* findAtlas(MEcs& ecs) {
	auto view = ecs.view<CIndexedAtlas>();
	for (auto entity : view) {
		const auto& a = view.get<CIndexedAtlas>(entity);
		if (!a.indices.empty() && a.width > 0 && a.height > 0) {
			return &a;
		}
	}
	return nullptr;
}

glm::vec2 faceUvCentroid(const CMesh& mesh, uint32_t i0, uint32_t i1, uint32_t i2) {
	if (mesh.texcoords.size() != mesh.positions.size()) {
		return {0.f, 0.f};
	}
	return (mesh.texcoords[i0] + mesh.texcoords[i1] + mesh.texcoords[i2]) / 3.0f;
}

float faceNdotL(const glm::vec3& nWorld, const LightFrame& light, const glm::vec3& worldPos) {
	if (light.mode == 0) {
		return 1.0f;
	}
	glm::vec3 L;
	if (light.mode == 2) {
		L = glm::normalize(light.pos - worldPos);
	} else {
		L = glm::normalize(-light.dir);
	}
	return std::clamp(glm::dot(nWorld, L) * light.intensity, 0.0f, 1.0f);
}

void buildTriangleVerts(const CMesh& mesh, const CDrawStyle& style, const CPalette* palette,
						const CIndexedAtlas* atlas, const LightFrame& light, const glm::mat4& world,
						const glm::mat4& viewProj, int presentW, int presentH,
						std::vector<float>& verts, bool& outPaletteShaded) {
	verts.clear();
	outPaletteShaded = false;
	const auto& p = mesh.positions;
	const bool useAtlas =
		palette && atlas && mesh.texcoords.size() == mesh.positions.size() && !p.empty();
	const bool usePalette = !useAtlas && palette && !mesh.facePalette.empty();
	auto emit = [&](uint32_t i0, uint32_t i1, uint32_t i2, size_t faceIndex) {
		if (i0 >= p.size() || i1 >= p.size() || i2 >= p.size()) {
			return;
		}
		const glm::vec3 nLocal = faceNormal(p[i0], p[i1], p[i2]);
		const glm::vec3 nWorld = glm::normalize(glm::mat3(world) * nLocal);
		const glm::vec3 center = glm::vec3(world * glm::vec4((p[i0] + p[i1] + p[i2]) / 3.0f, 1.0f));

		glm::vec4 c;
		int idx = -1;
		if (useAtlas) {
			const glm::vec2 uv = faceUvCentroid(mesh, i0, i1, i2);
			idx = static_cast<int>(atlas->sample(uv.x, uv.y));
		} else if (usePalette && faceIndex < mesh.facePalette.size()) {
			idx = static_cast<int>(mesh.facePalette[faceIndex]);
		}
		if (idx >= 0 && palette) {
			if (light.mode != 0 && light.banded) {
				const float ndl = faceNdotL(nWorld, light, center);
				const int bands = light.bands;
				const float maxT = static_cast<float>(std::max(bands - 1, 1));
				float t = std::clamp(ndl, 0.f, 1.f) * maxT;
				int step = 0;
				int px = 0, py = 0;
				bool havePx = false;
				if (presentW > 0 && presentH > 0) {
					const glm::vec4 clip = viewProj * glm::vec4(center, 1.f);
					if (std::fabs(clip.w) > 1e-6f) {
						const float ndcX = clip.x / clip.w;
						const float ndcY = clip.y / clip.w;
						px = static_cast<int>((ndcX * 0.5f + 0.5f) * static_cast<float>(presentW));
						py = static_cast<int>((ndcY * 0.5f + 0.5f) * static_cast<float>(presentH));
						havePx = true;
					}
				}
				if (light.dither && havePx) {
					const float lo = std::floor(t);
					const float hi = std::min(lo + 1.f, maxT);
					const float frac = t - lo;
					step = (bayer4(px, py) < frac) ? static_cast<int>(hi) : static_cast<int>(lo);
				} else {
					step = static_cast<int>(std::floor(t + 1e-4f));
					if (step > bands - 1) {
						step = bands - 1;
					}
				}
				// Ambient gates hard black: cap how many shadeNext steps we walk.
				const float amb = std::clamp(light.ambient, 0.f, 1.f);
				const int maxDarker = static_cast<int>(std::floor((1.f - amb) * maxT + 1e-4f));
				int darker = (bands - 1) - step;
				if (darker > maxDarker) {
					darker = maxDarker;
				}
				if (darker < 0) {
					darker = 0;
				}
				c = palette->shaded(idx, darker);
				outPaletteShaded = true;
			} else {
				c = palette->color(idx);
			}
		} else {
			c = faceColor(mesh, style, faceIndex);
		}
		pushVert(verts, p[i0], nLocal, c);
		pushVert(verts, p[i1], nLocal, c);
		pushVert(verts, p[i2], nLocal, c);
	};

	if (!mesh.loopSizes.empty()) {
		const uint32_t nFaces = meshFaceCount(mesh);
		for (uint32_t f = 0; f < nFaces; ++f) {
			uint32_t begin = 0, count = 0;
			if (!meshFaceRange(mesh, f, begin, count)) {
				continue;
			}
			const uint32_t i0 = meshFaceCorner(mesh, f, 0);
			for (uint32_t k = 1; k + 1 < count; ++k) {
				emit(i0, meshFaceCorner(mesh, f, k), meshFaceCorner(mesh, f, k + 1), f);
			}
		}
	} else if (mesh.indices.empty()) {
		size_t face = 0;
		for (size_t i = 0; i + 2 < p.size(); i += 3, ++face) {
			emit(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1),
				 static_cast<uint32_t>(i + 2), face);
		}
	} else {
		size_t face = 0;
		for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3, ++face) {
			emit(mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2], face);
		}
	}
}

void buildLineVerts(const CMesh& mesh, const CDrawStyle& style, std::vector<float>& verts) {
	verts.clear();
	const glm::vec3 n{0.0f, 0.0f, 1.0f};
	const glm::vec4 c{style.fillR, style.fillG, style.fillB, style.fillA};
	const auto& p = mesh.positions;
	auto emit = [&](uint32_t i0, uint32_t i1) {
		if (i0 >= p.size() || i1 >= p.size()) {
			return;
		}
		pushVert(verts, p[i0], n, c);
		pushVert(verts, p[i1], n, c);
	};

	if (mesh.mode == CMesh::Mode::LineStrip) {
		if (mesh.indices.empty()) {
			for (size_t i = 0; i + 1 < p.size(); ++i) {
				emit(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1));
			}
		} else {
			for (size_t i = 0; i + 1 < mesh.indices.size(); ++i) {
				emit(mesh.indices[i], mesh.indices[i + 1]);
			}
		}
	} else if (mesh.indices.empty()) {
		for (size_t i = 0; i + 1 < p.size(); i += 2) {
			emit(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1));
		}
	} else {
		for (size_t i = 0; i + 1 < mesh.indices.size(); i += 2) {
			emit(mesh.indices[i], mesh.indices[i + 1]);
		}
	}
}

void setMeshLightUniforms(Present3DGl& gl, const CTransform& transform, const glm::mat4& viewProj,
						  const LightFrame& light, int lightMode) {
	const glm::mat4 mvp = viewProj * transform.world;
	glUseProgram(gl.program);
	glUniformMatrix4fv(gl.uMvp, 1, GL_FALSE, glm::value_ptr(mvp));
	glUniformMatrix4fv(gl.uModel, 1, GL_FALSE, glm::value_ptr(transform.world));
	glUniform3fv(gl.uLightDir, 1, glm::value_ptr(light.dir));
	glUniform3fv(gl.uLightPos, 1, glm::value_ptr(light.pos));
	glUniform3fv(gl.uLightColor, 1, glm::value_ptr(light.color));
	glUniform1f(gl.uLightIntensity, light.intensity);
	glUniform1f(gl.uAmbient, light.ambient);
	glUniform1f(gl.uLightMode, static_cast<float>(lightMode));
	glUniform1f(gl.uBanded, light.banded ? 1.0f : 0.0f);
	glUniform1f(gl.uBands, static_cast<float>(light.bands));
	glUniform1f(gl.uDither, light.dither ? 1.0f : 0.0f);
}

void drawMesh(Present3DGl& gl, entt::entity id, const CTransform& transform, const CMesh& mesh,
			  const CDrawStyle& style, const glm::mat4& viewProj, const LightFrame& light,
			  const CPalette* palette, const CIndexedAtlas* atlas, int presentW, int presentH) {
	if (mesh.positions.empty()) {
		return;
	}

	const bool hasAtlasAlbedo = palette && atlas && mesh.texcoords.size() == mesh.positions.size();
	const bool bakePaletteShade =
		palette && light.mode != 0 && light.banded && (!mesh.facePalette.empty() || hasAtlasAlbedo);
	if (mesh.mode == CMesh::Mode::Triangles && !style.hasFill && mesh.faceColors.empty() &&
		mesh.facePalette.empty() && !hasAtlasAlbedo) {
		return;
	}

	GLenum mode = mesh.mode == CMesh::Mode::Triangles ? GL_TRIANGLES : GL_LINES;
	int lightMode = mode == GL_LINES ? 0 : light.mode;
	const bool cacheable = id != entt::null && !bakePaletteShade;
	Present3DGl::MeshGpu* gpu = nullptr;
	uint64_t stamp = 0;
	if (cacheable) {
		stamp = meshStamp(mesh, style, palette, atlas);
		gpu = &gl.meshGpu[static_cast<uint32_t>(id)];
		gpu->used = true;
		if (gpu->vbo && gpu->stamp == stamp && gpu->nVerts > 0) {
			if (mode == GL_LINES) {
				glLineWidth(std::clamp(style.strokeWidth, 1.0f, maxAliasedLineWidth()));
				// Toolpath / zone wires are CAD overlays: frustum only. Depth
				// test against a metres-long far plane eats distant hairlines
				// on 24-bit GLES depth even when the far plane is correct.
				glDisable(GL_DEPTH_TEST);
				glDepthMask(GL_FALSE);
			}
			setMeshLightUniforms(gl, transform, viewProj, light, lightMode);
			gl.bindMeshVbo(gpu->vbo);
			glDrawArrays(mode, 0, gpu->nVerts);
			glBindVertexArray(0);
			if (mode == GL_LINES) {
				glDepthMask(GL_TRUE);
				glEnable(GL_DEPTH_TEST);
				glLineWidth(1.0f);
			}
			return;
		}
	}

	std::vector<float> verts;
	if (mesh.mode == CMesh::Mode::Triangles) {
		bool paletteShaded = false;
		buildTriangleVerts(mesh, style, palette, atlas, light, transform.world, viewProj, presentW,
						   presentH, verts, paletteShaded);
		if (paletteShaded) {
			lightMode = 0;
		}
	} else {
		buildLineVerts(mesh, style, verts);
		lightMode = 0;
	}
	if (verts.empty()) {
		return;
	}
	if (mode == GL_LINES) {
		// Over the driver's cap glLineWidth is an error, so clamp rather than trust.
		glLineWidth(std::clamp(style.strokeWidth, 1.0f, maxAliasedLineWidth()));
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
	}

	setMeshLightUniforms(gl, transform, viewProj, light, lightMode);

	GLuint upload = gl.vbo;
	if (gpu) {
		if (!gpu->vbo) {
			glGenBuffers(1, &gpu->vbo);
		}
		gpu->stamp = stamp;
		gpu->nVerts = static_cast<GLsizei>(verts.size() / 10);
		upload = gpu->vbo;
	}
	gl.bindMeshVbo(upload);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
				 verts.data(), gpu ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW);
	glDrawArrays(mode, 0, static_cast<GLsizei>(verts.size() / 10));
	glBindVertexArray(0);
	if (mode == GL_LINES) {
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glLineWidth(1.0f);
	}
}

void drawFaceSelection(Present3DGl& gl, const CTransform& transform, const CMesh& mesh,
					   const CFaceSelection& sel, const glm::mat4& viewProj) {
	if (sel.faces.empty() || mesh.mode != CMesh::Mode::Triangles || mesh.positions.empty()) {
		return;
	}

	std::vector<float> verts;
	const glm::vec3 n{0.f, 0.f, 1.f};
	const glm::vec4 edge{1.f, 0.95f, 0.2f, 1.f};
	const auto& p = mesh.positions;

	auto emitEdge = [&](uint32_t a, uint32_t b) {
		if (a >= p.size() || b >= p.size()) {
			return;
		}
		pushVert(verts, p[a], n, edge);
		pushVert(verts, p[b], n, edge);
	};

	for (uint32_t face : sel.faces) {
		uint32_t begin = 0, count = 0;
		if (!meshFaceRange(mesh, face, begin, count)) {
			continue;
		}
		for (uint32_t c = 0; c < count; ++c) {
			emitEdge(meshFaceCorner(mesh, face, c), meshFaceCorner(mesh, face, (c + 1) % count));
		}
	}
	if (verts.empty()) {
		return;
	}

	const glm::mat4 mvp = viewProj * transform.world;
	glUseProgram(gl.program);
	glUniformMatrix4fv(gl.uMvp, 1, GL_FALSE, glm::value_ptr(mvp));
	glUniformMatrix4fv(gl.uModel, 1, GL_FALSE, glm::value_ptr(transform.world));
	glUniform1f(gl.uLightMode, 0.0f);
	glUniform1f(gl.uBanded, 0.0f);
	glUniform1f(gl.uDither, 0.0f);
	glLineWidth(2.0f);

	glDepthFunc(GL_LEQUAL);
	gl.bindMeshVbo(gl.vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
				 verts.data(), GL_DYNAMIC_DRAW);
	glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size() / 10));
	glBindVertexArray(0);
	glDepthFunc(GL_LESS);
	glLineWidth(1.0f);
}

void drawVertexSelection(Present3DGl& gl, const CTransform& transform, const CMesh& mesh,
						 const CVertexSelection& sel, const glm::mat4& viewProj) {
	if (sel.vertices.empty() || mesh.positions.empty()) {
		return;
	}

	// Draw selected verts as tiny axis-aligned crosses (GLES-friendly, no point sprites).
	std::vector<float> verts;
	const glm::vec3 n{0.f, 0.f, 1.f};
	const glm::vec4 col{0.2f, 0.95f, 1.f, 1.f};
	const auto& p = mesh.positions;
	float extent = 0.06f;
	for (const auto& pos : p) {
		extent = std::max(extent, glm::length(pos) * 0.02f);
	}
	extent = std::clamp(extent, 0.03f, 0.2f);

	auto emitSeg = [&](const glm::vec3& a, const glm::vec3& b) {
		pushVert(verts, a, n, col);
		pushVert(verts, b, n, col);
	};

	for (uint32_t vi : sel.vertices) {
		if (vi >= p.size()) {
			continue;
		}
		const glm::vec3 c = p[vi];
		emitSeg(c + glm::vec3(extent, 0.f, 0.f), c - glm::vec3(extent, 0.f, 0.f));
		emitSeg(c + glm::vec3(0.f, extent, 0.f), c - glm::vec3(0.f, extent, 0.f));
		emitSeg(c + glm::vec3(0.f, 0.f, extent), c - glm::vec3(0.f, 0.f, extent));
	}
	if (verts.empty()) {
		return;
	}

	const glm::mat4 mvp = viewProj * transform.world;
	glUseProgram(gl.program);
	glUniformMatrix4fv(gl.uMvp, 1, GL_FALSE, glm::value_ptr(mvp));
	glUniformMatrix4fv(gl.uModel, 1, GL_FALSE, glm::value_ptr(transform.world));
	glUniform1f(gl.uLightMode, 0.0f);
	glUniform1f(gl.uBanded, 0.0f);
	glUniform1f(gl.uDither, 0.0f);
	glLineWidth(2.0f);

	glDepthFunc(GL_LEQUAL);
	gl.bindMeshVbo(gl.vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
				 verts.data(), GL_DYNAMIC_DRAW);
	glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size() / 10));
	glBindVertexArray(0);
	glDepthFunc(GL_LESS);
	glLineWidth(1.0f);
}

void drawObjectWireframe(Present3DGl& gl, const CTransform& transform, const CMesh& mesh,
						 const glm::mat4& viewProj) {
	if (mesh.mode != CMesh::Mode::Triangles || mesh.positions.empty()) {
		return;
	}

	std::vector<float> verts;
	const glm::vec3 n{0.f, 0.f, 1.f};
	const glm::vec4 edge{1.f, 0.75f, 0.25f, 1.f};
	const auto& p = mesh.positions;

	auto emitEdge = [&](uint32_t a, uint32_t b) {
		if (a >= p.size() || b >= p.size()) {
			return;
		}
		pushVert(verts, p[a], n, edge);
		pushVert(verts, p[b], n, edge);
	};

	const uint32_t nFaces = meshFaceCount(mesh);
	for (uint32_t face = 0; face < nFaces; ++face) {
		uint32_t begin = 0, count = 0;
		if (!meshFaceRange(mesh, face, begin, count)) {
			continue;
		}
		for (uint32_t c = 0; c < count; ++c) {
			emitEdge(meshFaceCorner(mesh, face, c), meshFaceCorner(mesh, face, (c + 1) % count));
		}
	}
	if (verts.empty()) {
		return;
	}

	const glm::mat4 mvp = viewProj * transform.world;
	glUseProgram(gl.program);
	glUniformMatrix4fv(gl.uMvp, 1, GL_FALSE, glm::value_ptr(mvp));
	glUniformMatrix4fv(gl.uModel, 1, GL_FALSE, glm::value_ptr(transform.world));
	glUniform1f(gl.uLightMode, 0.0f);
	glUniform1f(gl.uBanded, 0.0f);
	glUniform1f(gl.uDither, 0.0f);
	glLineWidth(2.0f);

	glDepthFunc(GL_LEQUAL);
	gl.bindMeshVbo(gl.vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
				 verts.data(), GL_DYNAMIC_DRAW);
	glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size() / 10));
	glBindVertexArray(0);
	glDepthFunc(GL_LESS);
	glLineWidth(1.0f);
}

} // namespace

MeshPresentTarget::~MeshPresentTarget() {
	destroy();
}

void MeshPresentTarget::destroy() {
	if (m_color) {
		glDeleteTextures(1, &m_color);
		m_color = 0;
	}
	if (m_depth) {
		glDeleteRenderbuffers(1, &m_depth);
		m_depth = 0;
	}
	if (m_fbo) {
		glDeleteFramebuffers(1, &m_fbo);
		m_fbo = 0;
	}
	m_w = 0;
	m_h = 0;
}

bool MeshPresentTarget::resize(int w, int h) {
	if (w <= 0 || h <= 0) {
		return false;
	}
	if (m_fbo != 0 && m_w == w && m_h == h) {
		return true;
	}
	destroy();

	glGenTextures(1, &m_color);
	glBindTexture(GL_TEXTURE_2D, m_color);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	glGenRenderbuffers(1, &m_depth);
	glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);

	glGenFramebuffers(1, &m_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		spdlog::error("[rigRender3D] view FBO incomplete ({})", static_cast<int>(status));
		destroy();
		return false;
	}
	m_w = w;
	m_h = h;
	return true;
}

float maxAliasedLineWidth() {
	static const float wMax = [] {
		GLfloat range[2] = {1.0f, 1.0f};
		glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, range);
		return std::max(1.0f, range[1]);
	}();
	return wMax;
}

void SMeshPresent3D(MEcs& ecs, int viewportW, int viewportH, bool resolveHierarchy,
					const entt::entity* only, std::size_t onlyCount, int originX, int originY) {
	entt::entity camEntity = entt::null;
	for (auto entity : ecs.view<CTransform, CCamera>()) {
		if (ecs.getComponent<CCamera>(entity).active) {
			camEntity = entity;
			break;
		}
	}
	SMeshPresent3D(ecs, camEntity, viewportW, viewportH, resolveHierarchy, only, onlyCount, originX,
				   originY, 0);
}

void SMeshPresent3D(MEcs& ecs, entt::entity camEntity, int viewportW, int viewportH,
					bool resolveHierarchy, const entt::entity* only, std::size_t onlyCount,
					int originX, int originY, unsigned targetFbo) {
	if (viewportW <= 0 || viewportH <= 0 || camEntity == entt::null ||
		!ecs.registry().valid(camEntity) || !ecs.hasComponent<CCamera>(camEntity) ||
		!ecs.hasComponent<CTransform>(camEntity)) {
		return;
	}

	if (resolveHierarchy) {
		SHierarchy(ecs);
	}

	auto& gl = glState();
	if (!gl.ensure()) {
		return;
	}

	const auto& cam = ecs.getComponent<CCamera>(camEntity);
	const auto& camXf = ecs.getComponent<CTransform>(camEntity);
	const bool offscreen = targetFbo != 0;
	const bool pixel = !offscreen && cam.presentWidth > 0 && cam.presentHeight > 0;
	const int presentW = pixel ? cam.presentWidth : viewportW;
	const int presentH = pixel ? cam.presentHeight : viewportH;
	const float aspect = static_cast<float>(presentW) / static_cast<float>(presentH);
	const glm::mat4 viewProj = cam.projectionMatrix(aspect) * CCamera::viewMatrix(camXf);
	const glm::mat4 invViewProj = glm::inverse(viewProj);
	const glm::vec3 cameraPos(camXf.world[3]);
	const LightFrame light = findLight(ecs);
	SurfaceLight surfaceLight;
	surfaceLight.mode = light.mode;
	surfaceLight.dir = light.dir;
	surfaceLight.pos = light.pos;
	surfaceLight.color = light.color;
	surfaceLight.intensity = light.intensity;
	surfaceLight.ambient = light.ambient;
	const bool surfaceOn = sceneHasSurface(ecs) && surfaceShadersOk();
	SurfaceFrame surfaceFrame;
	if (surfaceOn) {
		surfaceFrame = beginSurfaceFrame(ecs, surfaceLight, viewProj, invViewProj, cameraPos,
										 presentW, presentH, originX, originY);
	}
	const CPalette* palette = findPalette(ecs);
	const CIndexedAtlas* atlas = findAtlas(ecs);

	GLint prevFbo = 0;
	GLint prevVp[4] = {0, 0, 0, 0};
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
	glGetIntegerv(GL_VIEWPORT, prevVp);
	if (offscreen) {
		glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
		GLfloat clear[4] = {0.f, 0.f, 0.f, 1.f};
		glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
		glClearColor(clear[0], clear[1], clear[2], clear[3]);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glViewport(0, 0, presentW, presentH);
	} else if (pixel) {
		if (!gl.ensureFbo(presentW, presentH)) {
			return;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, gl.fbo);
		GLfloat clear[4] = {0.f, 0.f, 0.f, 1.f};
		glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
		glClearColor(clear[0], clear[1], clear[2], clear[3]);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glViewport(0, 0, presentW, presentH);
	} else {
		const int vx = originX;
		const int vy = originY;
		glViewport(vx, vy, presentW, presentH);
		if (vx != 0 || vy != 0) {
			// Clear only the bed so dock chrome keeps the prior window clear.
			GLint prevScissor[4] = {0, 0, 0, 0};
			GLboolean scissorWas = glIsEnabled(GL_SCISSOR_TEST);
			glGetIntegerv(GL_SCISSOR_BOX, prevScissor);
			glEnable(GL_SCISSOR_TEST);
			glScissor(vx, vy, presentW, presentH);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			if (!scissorWas) {
				glDisable(GL_SCISSOR_TEST);
			}
			glScissor(prevScissor[0], prevScissor[1], prevScissor[2], prevScissor[3]);
		}
	}

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	auto isVisible = [&](entt::entity entity) {
		return !ecs.hasComponent<CRenderVisibility>(entity) ||
			   ecs.getComponent<CRenderVisibility>(entity).visible;
	};

	auto drawMeshEntity = [&](entt::entity entity, const CTransform& transform, const CMesh& mesh,
							  const CDrawStyle& style, const CRenderMaterial* material) {
		if (surfaceOn && mesh.mode == CMesh::Mode::Triangles && material) {
			drawSurfaceMesh(ecs, entity, transform, mesh, *material, viewProj, surfaceLight,
							surfaceFrame);
		} else {
			drawMesh(gl, entity, transform, mesh, style, viewProj, light, palette, atlas, presentW,
					 presentH);
		}
		if (!ecs.registry().valid(entity)) {
			return;
		}
		const bool faceSel = ecs.hasComponent<CFaceSelection>(entity) &&
							 !ecs.getComponent<CFaceSelection>(entity).faces.empty();
		const bool vertSel = ecs.hasComponent<CVertexSelection>(entity) &&
							 !ecs.getComponent<CVertexSelection>(entity).vertices.empty();
		if (faceSel) {
			drawFaceSelection(gl, transform, mesh, ecs.getComponent<CFaceSelection>(entity),
							  viewProj);
		}
		if (vertSel) {
			drawVertexSelection(gl, transform, mesh, ecs.getComponent<CVertexSelection>(entity),
								viewProj);
		}
		if (!faceSel && !vertSel && ecs.hasComponent<CSelection>(entity)) {
			const auto& sel = ecs.getComponent<CSelection>(entity);
			if (sel.isSelected || sel.isMultiSelected) {
				drawObjectWireframe(gl, transform, mesh, viewProj);
			}
		}
	};

	auto drawOne = [&](entt::entity entity) {
		if (!ecs.registry().valid(entity) || !ecs.hasComponent<CTransform>(entity) ||
			!isVisible(entity)) {
			return;
		}
		const auto& transform = ecs.getComponent<CTransform>(entity);
		if (ecs.hasComponent<CInstance>(entity)) {
			const auto source = ecs.getComponent<CInstance>(entity).source;
			if (!ecs.registry().valid(source) || !ecs.hasComponent<CMesh>(source) ||
				!isVisible(source)) {
				return;
			}
			CDrawStyle style;
			if (ecs.hasComponent<CDrawStyle>(source)) {
				style = ecs.getComponent<CDrawStyle>(source);
			} else {
				style.setFillColor(0.8f, 0.8f, 0.8f);
				style.hasStroke = false;
			}
			const CRenderMaterial* material =
				ecs.hasComponent<CRenderMaterial>(source)
					? &ecs.getComponent<CRenderMaterial>(source)
					: nullptr;
			drawMeshEntity(entity, transform, ecs.getComponent<CMesh>(source), style, material);
			return;
		}
		if (!ecs.hasComponent<CDrawStyle>(entity)) {
			return;
		}
		const auto& style = ecs.getComponent<CDrawStyle>(entity);
		if (ecs.hasComponent<CMesh>(entity)) {
			const CRenderMaterial* material =
				ecs.hasComponent<CRenderMaterial>(entity)
					? &ecs.getComponent<CRenderMaterial>(entity)
					: nullptr;
			drawMeshEntity(entity, transform, ecs.getComponent<CMesh>(entity), style, material);
		} else if (ecs.hasComponent<CSphere>(entity)) {
			// Parametric primitive: rebuild the loose triangle soup fresh, same deal as
			// the 2D shapes in rigSystems - CSphere stays the live data, not CMesh.
			CMesh sphereMesh;
			sphereMesh.mode = CMesh::Mode::Triangles;
			sphereMesh.positions = verticesOf(ecs.getComponent<CSphere>(entity));
			const CRenderMaterial* material =
				ecs.hasComponent<CRenderMaterial>(entity)
					? &ecs.getComponent<CRenderMaterial>(entity)
					: nullptr;
			drawMeshEntity(entity, transform, sphereMesh, style, material);
		}
	};

	if (only && onlyCount > 0) {
		for (std::size_t i = 0; i < onlyCount; ++i) {
			drawOne(only[i]);
		}
	} else {
		auto meshes = ecs.view<CTransform, CMesh, CDrawStyle>();
		for (auto entity : meshes) {
			drawOne(entity);
		}
		auto spheres = ecs.view<CTransform, CSphere, CDrawStyle>();
		for (auto entity : spheres) {
			drawOne(entity);
		}
		auto instances = ecs.view<CTransform, CInstance>();
		for (auto entity : instances) {
			drawOne(entity);
		}
	}

	glDisable(GL_DEPTH_TEST);
	if (!(only && onlyCount > 0)) {
		gl.sweepMeshGpu();
	}

	if (offscreen) {
		glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
		glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
	} else if (pixel) {
		gl.blitToWindow(viewportW, viewportH);
		glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
	}
}

} // namespace ecs
} // namespace rigkit
