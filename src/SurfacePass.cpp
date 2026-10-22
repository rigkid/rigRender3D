#include "SurfacePass.h"

#include "rendering/U_gladGlfw.h"
#include "CAssetRef.h"
#include "CRenderEnvironment.h"
#include "CSphere.h"
#include "Image.h"
#include "MeshFaces.h"
#include "PrimitiveVertices3D.h"
#include "SurfaceIbl.h"
#include "shaders/SurfaceShader.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

namespace rigkit {
namespace ecs {
namespace {

constexpr int kShadowSize = 1024;
constexpr int kStride = 8;

bool gShaderTried = false;
bool gShaderFailed = false;
bool gAssetFailed = false;

struct SurfaceGl {
	bool ready = false;
	bool surfaceOk = false;
	bool depthOk = false;
	bool normalOk = false;
	bool aoOk = false;

	GLuint surfaceProg = 0;
	GLint uMvp = -1;
	GLint uModel = -1;
	GLint uLightViewProj = -1;
	GLint uCamPos = -1;
	GLint uSh[9]{};
	GLint uLightDirMode = -1;
	GLint uLightPosI = -1;
	GLint uLightColorAmb = -1;
	GLint uAlbedoMetal = -1;
	GLint uEmissiveRough = -1;
	GLint uShadowAo = -1;
	GLint uOriginSize = -1;
	GLint uIblIntensity = -1;
	GLint uAlbedo = -1;
	GLint uShadow = -1;
	GLint uAo = -1;

	GLuint depthProg = 0;
	GLint uDepthMvp = -1;
	GLuint normalProg = 0;
	GLint uNormalMvp = -1;
	GLint uNormalModel = -1;
	GLuint aoProg = 0;
	GLint uAoDepth = -1;
	GLint uAoNormal = -1;
	GLint uAoNoise = -1;
	GLint uAoViewProj = -1;
	GLint uAoInvViewProj = -1;
	GLint uAoCam = -1;
	GLint uAoRadius = -1;

	GLuint surfaceVao = 0;
	GLuint depthVao = 0;
	GLuint normalVao = 0;
	GLuint meshVbo = 0;
	GLuint fsVao = 0;
	GLuint fsVbo = 0;

	GLuint whiteTex = 0;
	GLuint noiseTex = 0;

	GLuint shadowFbo = 0;
	GLuint shadowTex = 0;
	bool shadowReady = false;

	GLuint gFbo = 0;
	GLuint gColor = 0;
	GLuint gDepth = 0;
	int gW = 0;
	int gH = 0;
	bool gReady = false;

	GLuint aoFbo = 0;
	GLuint aoTex = 0;
	int aoW = 0;
	int aoH = 0;
	bool aoReady = false;

	std::unordered_map<std::string, rig::image::GpuTexture> textures;
	std::string shPath;
	Sh9 sh{};
	bool shValid = false;
};

SurfaceGl& glState() {
	static SurfaceGl s;
	return s;
}

std::unordered_map<uint32_t, std::vector<float>> gVerts;

bool compileShader(GLenum type, const char* src, GLuint& out, const char* label) {
	out = glCreateShader(type);
	glShaderSource(out, 1, &src, nullptr);
	glCompileShader(out);
	GLint ok = 0;
	glGetShaderiv(out, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog(out, 1024, nullptr, log);
		spdlog::error("[rigRender3D] {} shader: {}", label, log);
		glDeleteShader(out);
		out = 0;
		return false;
	}
	return true;
}

bool linkProgram(GLuint vs, GLuint fs, GLuint& out, const char* label) {
	out = glCreateProgram();
	glAttachShader(out, vs);
	glAttachShader(out, fs);
	glLinkProgram(out);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(out, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(out, 1024, nullptr, log);
		spdlog::error("[rigRender3D] {} link: {}", label, log);
		glDeleteProgram(out);
		out = 0;
		return false;
	}
	return true;
}

bool linkNamed(const char* vsSrc, const char* fsSrc, GLuint& program, const char* label) {
	GLuint vs = 0;
	GLuint fs = 0;
	if (!compileShader(GL_VERTEX_SHADER, vsSrc, vs, label) ||
		!compileShader(GL_FRAGMENT_SHADER, fsSrc, fs, label)) {
		if (vs) {
			glDeleteShader(vs);
		}
		if (fs) {
			glDeleteShader(fs);
		}
		return false;
	}
#if defined(RIGKIT_GLES)
	program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	if (std::string(label) == "surface") {
		glBindAttribLocation(program, 0, "aPos");
		glBindAttribLocation(program, 1, "aNormal");
		glBindAttribLocation(program, 2, "aUv");
	} else if (std::string(label) == "normal") {
		glBindAttribLocation(program, 0, "aPos");
		glBindAttribLocation(program, 1, "aNormal");
	} else {
		glBindAttribLocation(program, 0, "aPos");
	}
	glLinkProgram(program);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(program, 1024, nullptr, log);
		spdlog::error("[rigRender3D] {} link: {}", label, log);
		glDeleteProgram(program);
		program = 0;
		return false;
	}
	return true;
#else
	return linkProgram(vs, fs, program, label);
#endif
}

std::string fragmentSource(const char* preamble, const char* body) {
	return std::string(preamble) + body;
}

void makeWhite(GLuint& tex) {
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	const unsigned char px[4] = {255, 255, 255, 255};
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

void makeNoise(GLuint& tex) {
	unsigned char px[4 * 16];
	unsigned n = 17;
	for (int i = 0; i < 16; ++i) {
		n = n * 1103515245u + 12345u;
		px[i * 4 + 0] = static_cast<unsigned char>(n >> 16);
		n = n * 1103515245u + 12345u;
		px[i * 4 + 1] = static_cast<unsigned char>(n >> 16);
		n = n * 1103515245u + 12345u;
		px[i * 4 + 2] = static_cast<unsigned char>(n >> 16);
		px[i * 4 + 3] = 255;
	}
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

bool ensure(SurfaceGl& gl) {
	if (gl.ready) {
		return gl.surfaceOk;
	}
	gl.ready = true;
	gShaderTried = true;

#if defined(RIGKIT_GLES)
	const char* vs = shaders::kSurfaceVertexGLES;
	const char* depthVs = shaders::kDepthVertexGLES;
	const char* depthFs = shaders::kDepthFragmentGLES;
	const char* normalVs = shaders::kNormalVertexGLES;
	const char* normalFs = shaders::kNormalFragmentGLES;
	const char* aoVs = shaders::kAoVertexGLES;
	const std::string fs = fragmentSource(shaders::kSurfaceFragmentPreambleGLES, shaders::kSurfaceFragmentBody);
	const std::string aoFs = fragmentSource(shaders::kAoFragmentGLES, shaders::kAoFragmentBody);
#else
	const char* vs = shaders::kSurfaceVertexGL;
	const char* depthVs = shaders::kDepthVertexGL;
	const char* depthFs = shaders::kDepthFragmentGL;
	const char* normalVs = shaders::kNormalVertexGL;
	const char* normalFs = shaders::kNormalFragmentGL;
	const char* aoVs = shaders::kAoVertexGL;
	const std::string fs = fragmentSource(shaders::kSurfaceFragmentPreambleGL, shaders::kSurfaceFragmentBody);
	const std::string aoFs = fragmentSource(shaders::kAoFragmentGL, shaders::kAoFragmentBody);
#endif

	gl.surfaceOk = linkNamed(vs, fs.c_str(), gl.surfaceProg, "surface");
	if (!gl.surfaceOk) {
		gShaderFailed = true;
		return false;
	}
	gl.uMvp = glGetUniformLocation(gl.surfaceProg, "uMvp");
	gl.uModel = glGetUniformLocation(gl.surfaceProg, "uModel");
	gl.uLightViewProj = glGetUniformLocation(gl.surfaceProg, "uLightViewProj");
	gl.uCamPos = glGetUniformLocation(gl.surfaceProg, "uCamPos");
	for (int i = 0; i < 9; ++i) {
		const std::string name = "uSh" + std::to_string(i);
		gl.uSh[i] = glGetUniformLocation(gl.surfaceProg, name.c_str());
	}
	gl.uLightDirMode = glGetUniformLocation(gl.surfaceProg, "uLightDirMode");
	gl.uLightPosI = glGetUniformLocation(gl.surfaceProg, "uLightPosI");
	gl.uLightColorAmb = glGetUniformLocation(gl.surfaceProg, "uLightColorAmb");
	gl.uAlbedoMetal = glGetUniformLocation(gl.surfaceProg, "uAlbedoMetal");
	gl.uEmissiveRough = glGetUniformLocation(gl.surfaceProg, "uEmissiveRough");
	gl.uShadowAo = glGetUniformLocation(gl.surfaceProg, "uShadowAo");
	gl.uOriginSize = glGetUniformLocation(gl.surfaceProg, "uOriginSize");
	gl.uIblIntensity = glGetUniformLocation(gl.surfaceProg, "uIblIntensity");
	gl.uAlbedo = glGetUniformLocation(gl.surfaceProg, "uAlbedo");
	gl.uShadow = glGetUniformLocation(gl.surfaceProg, "uShadow");
	gl.uAo = glGetUniformLocation(gl.surfaceProg, "uAo");

	gl.depthOk = linkNamed(depthVs, depthFs, gl.depthProg, "depth");
	if (gl.depthOk) {
		gl.uDepthMvp = glGetUniformLocation(gl.depthProg, "uMvp");
	}
	gl.normalOk = linkNamed(normalVs, normalFs, gl.normalProg, "normal");
	if (gl.normalOk) {
		gl.uNormalMvp = glGetUniformLocation(gl.normalProg, "uMvp");
		gl.uNormalModel = glGetUniformLocation(gl.normalProg, "uModel");
	}
	gl.aoOk = linkNamed(aoVs, aoFs.c_str(), gl.aoProg, "ssao");
	if (gl.aoOk) {
		gl.uAoDepth = glGetUniformLocation(gl.aoProg, "uDepth");
		gl.uAoNormal = glGetUniformLocation(gl.aoProg, "uNormal");
		gl.uAoNoise = glGetUniformLocation(gl.aoProg, "uNoise");
		gl.uAoViewProj = glGetUniformLocation(gl.aoProg, "uViewProj");
		gl.uAoInvViewProj = glGetUniformLocation(gl.aoProg, "uInvViewProj");
		gl.uAoCam = glGetUniformLocation(gl.aoProg, "uCamPos");
		gl.uAoRadius = glGetUniformLocation(gl.aoProg, "uRadius");
	}

	glGenVertexArrays(1, &gl.surfaceVao);
	glGenVertexArrays(1, &gl.depthVao);
	glGenVertexArrays(1, &gl.normalVao);
	glGenBuffers(1, &gl.meshVbo);
	glGenVertexArrays(1, &gl.fsVao);
	glGenBuffers(1, &gl.fsVbo);
	const float tri[6] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
	glBindVertexArray(gl.fsVao);
	glBindBuffer(GL_ARRAY_BUFFER, gl.fsVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
	glBindVertexArray(0);

	makeWhite(gl.whiteTex);
	makeNoise(gl.noiseTex);
	glBindTexture(GL_TEXTURE_2D, 0);
	return true;
}

void pushVert(std::vector<float>& verts, const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv) {
	verts.push_back(p.x);
	verts.push_back(p.y);
	verts.push_back(p.z);
	verts.push_back(n.x);
	verts.push_back(n.y);
	verts.push_back(n.z);
	verts.push_back(uv.x);
	verts.push_back(uv.y);
}

glm::vec3 faceNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
	const glm::vec3 n = glm::cross(b - a, c - a);
	const float len = glm::length(n);
	if (len < 1e-8f) {
		return {0.f, 1.f, 0.f};
	}
	return n / len;
}

std::vector<float> buildVerts(const CMesh& mesh) {
	std::vector<float> verts;
	const auto& p = mesh.positions;
	const bool hasUv = mesh.texcoords.size() == p.size();
	auto emit = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
		if (i0 >= p.size() || i1 >= p.size() || i2 >= p.size()) {
			return;
		}
		const glm::vec3 n = faceNormal(p[i0], p[i1], p[i2]);
		const glm::vec2 z{0.f, 0.f};
		pushVert(verts, p[i0], n, hasUv ? mesh.texcoords[i0] : z);
		pushVert(verts, p[i1], n, hasUv ? mesh.texcoords[i1] : z);
		pushVert(verts, p[i2], n, hasUv ? mesh.texcoords[i2] : z);
	};
	if (!mesh.loopSizes.empty()) {
		const uint32_t nFaces = meshFaceCount(mesh);
		for (uint32_t f = 0; f < nFaces; ++f) {
			uint32_t begin = 0;
			uint32_t count = 0;
			if (!meshFaceRange(mesh, f, begin, count)) {
				continue;
			}
			const uint32_t i0 = meshFaceCorner(mesh, f, 0);
			for (uint32_t k = 1; k + 1 < count; ++k) {
				emit(i0, meshFaceCorner(mesh, f, k), meshFaceCorner(mesh, f, k + 1));
			}
		}
	} else if (mesh.indices.empty()) {
		for (size_t i = 0; i + 2 < p.size(); i += 3) {
			emit(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2));
		}
	} else {
		for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
			emit(mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]);
		}
	}
	return verts;
}

void uploadMesh(SurfaceGl& gl, GLuint vao, const std::vector<float>& verts, int attribs) {
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, gl.meshVbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(),
				 GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride * sizeof(float), nullptr);
	if (attribs > 1) {
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride * sizeof(float),
							  reinterpret_cast<void*>(3 * sizeof(float)));
	}
	if (attribs > 2) {
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride * sizeof(float),
							  reinterpret_cast<void*>(6 * sizeof(float)));
	}
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / kStride));
	glBindVertexArray(0);
}

void gather(MEcs& ecs) {
	gVerts.clear();
	auto add = [&](entt::entity entity, const CMesh& mesh) {
		if (mesh.mode != CMesh::Mode::Triangles || mesh.positions.empty()) {
			return;
		}
		auto verts = buildVerts(mesh);
		if (!verts.empty()) {
			gVerts[static_cast<uint32_t>(entity)] = std::move(verts);
		}
	};
	for (auto entity : ecs.view<CTransform, CMesh>()) {
		add(entity, ecs.getComponent<CMesh>(entity));
	}
	for (auto entity : ecs.view<CTransform, CSphere>()) {
		if (ecs.hasComponent<CMesh>(entity)) {
			continue;
		}
		CMesh mesh;
		mesh.mode = CMesh::Mode::Triangles;
		mesh.positions = verticesOf(ecs.getComponent<CSphere>(entity));
		add(entity, mesh);
	}
}

bool allocDepth(GLuint& tex, int w, int h) {
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#if !defined(RIGKIT_GLES)
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
#endif
	while (glGetError() != GL_NO_ERROR) {
	}
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT,
				 nullptr);
	if (glGetError() != GL_NO_ERROR) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT,
					 nullptr);
		if (glGetError() != GL_NO_ERROR) {
			glDeleteTextures(1, &tex);
			tex = 0;
			return false;
		}
	}
	return true;
}

bool allocColor(GLuint& tex, int w, int h, GLint filter) {
	while (glGetError() != GL_NO_ERROR) {
	}
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	return glGetError() == GL_NO_ERROR;
}

void destroyFbo(GLuint& fbo, GLuint& a, GLuint& b) {
	if (fbo) {
		glDeleteFramebuffers(1, &fbo);
		fbo = 0;
	}
	if (a) {
		glDeleteTextures(1, &a);
		a = 0;
	}
	if (b) {
		glDeleteTextures(1, &b);
		b = 0;
	}
}

bool ensureShadow(SurfaceGl& gl) {
	if (gl.shadowReady) {
		return gl.shadowTex != 0;
	}
	gl.shadowReady = true;
	if (!gl.depthOk || !allocDepth(gl.shadowTex, kShadowSize, kShadowSize)) {
		static bool logged = false;
		if (!logged) {
			spdlog::warn("[rigRender3D] depth texture unavailable; directional shadows off");
			logged = true;
		}
		return false;
	}
	glGenFramebuffers(1, &gl.shadowFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, gl.shadowFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl.shadowTex, 0);
#if !defined(RIGKIT_GLES)
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
#endif
	const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	if (!ok) {
		destroyFbo(gl.shadowFbo, gl.shadowTex, gl.shadowTex);
		gl.shadowTex = 0;
		spdlog::warn("[rigRender3D] shadow framebuffer incomplete");
		return false;
	}
	return true;
}

bool ensureGbuffer(SurfaceGl& gl, int w, int h) {
	if (gl.gReady && gl.gW == w && gl.gH == h) {
		return true;
	}
	GLuint dummy = 0;
	destroyFbo(gl.gFbo, gl.gColor, gl.gDepth);
	gl.gReady = false;
	if (!gl.normalOk || !gl.depthOk || w < 1 || h < 1) {
		return false;
	}
	if (!allocColor(gl.gColor, w, h, GL_NEAREST) || !allocDepth(gl.gDepth, w, h)) {
		destroyFbo(gl.gFbo, gl.gColor, gl.gDepth);
		return false;
	}
	glGenFramebuffers(1, &gl.gFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, gl.gFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.gColor, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl.gDepth, 0);
	(void)dummy;
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		destroyFbo(gl.gFbo, gl.gColor, gl.gDepth);
		static bool logged = false;
		if (!logged) {
			spdlog::warn("[rigRender3D] depth framebuffer incomplete; SSAO off");
			logged = true;
		}
		return false;
	}
	gl.gW = w;
	gl.gH = h;
	gl.gReady = true;
	return true;
}

bool ensureAo(SurfaceGl& gl, int w, int h) {
	if (gl.aoReady && gl.aoW == w && gl.aoH == h) {
		return true;
	}
	GLuint unused = 0;
	destroyFbo(gl.aoFbo, gl.aoTex, unused);
	gl.aoReady = false;
	if (!gl.aoOk || !allocColor(gl.aoTex, w, h, GL_LINEAR)) {
		if (gl.aoTex) {
			glDeleteTextures(1, &gl.aoTex);
			gl.aoTex = 0;
		}
		return false;
	}
	glGenFramebuffers(1, &gl.aoFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, gl.aoFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.aoTex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		destroyFbo(gl.aoFbo, gl.aoTex, unused);
		return false;
	}
	gl.aoW = w;
	gl.aoH = h;
	gl.aoReady = true;
	return true;
}

void drawCached(MEcs& ecs, SurfaceGl& gl, GLuint vao, int attribs, const glm::mat4& viewProj, GLint uMvp,
				GLint uModel) {
	for (const auto& slot : gVerts) {
		const auto entity = static_cast<entt::entity>(slot.first);
		if (!ecs.registry().valid(entity) || !ecs.hasComponent<CTransform>(entity) || slot.second.empty()) {
			continue;
		}
		const glm::mat4& world = ecs.getComponent<CTransform>(entity).world;
		const glm::mat4 mvp = viewProj * world;
		glUniformMatrix4fv(uMvp, 1, GL_FALSE, glm::value_ptr(mvp));
		if (uModel >= 0) {
			glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(world));
		}
		uploadMesh(gl, vao, slot.second, attribs);
	}
}

void updateIbl(SurfaceGl& gl, MEcs& ecs, SurfaceFrame& frame) {
	frame.ibl = false;
	frame.iblIntensity = 0.f;
	const CRenderEnvironment* env = nullptr;
	for (auto entity : ecs.view<CRenderEnvironment>()) {
		env = &ecs.getComponent<CRenderEnvironment>(entity);
		break;
	}
	if (!env || env->hdri == entt::null || !ecs.registry().valid(env->hdri) ||
		!ecs.hasComponent<CAssetRef>(env->hdri)) {
		return;
	}
	const std::string& href = ecs.getComponent<CAssetRef>(env->hdri).path;
	if (href.empty()) {
		return;
	}
	const std::string path = rig::image::resolvePath(href);
	if (path.empty()) {
		gAssetFailed = true;
		spdlog::warn("[rigRender3D] HDRI not found: {}", href);
		return;
	}
	if (!gl.shValid || gl.shPath != path) {
		rig::image::FloatRaster raster;
		if (!rig::image::loadHdr(path, raster)) {
			gAssetFailed = true;
			gl.shValid = false;
			return;
		}
		gl.sh = shFromLatlong(raster.rgba.data(), raster.width, raster.height);
		gl.shPath = path;
		gl.shValid = true;
		spdlog::info("[rigRender3D] irradiance {}x{} from {}", raster.width, raster.height, path);
	}
	frame.ibl = true;
	frame.iblIntensity = env->intensity;
	for (int i = 0; i < 9; ++i) {
		frame.sh[i] = gl.sh.band[i];
	}
}

unsigned albedoTexture(SurfaceGl& gl, MEcs& ecs, const CRenderMaterial& material) {
	if (material.albedoMap == entt::null || !ecs.registry().valid(material.albedoMap) ||
		!ecs.hasComponent<CAssetRef>(material.albedoMap)) {
		return gl.whiteTex;
	}
	const std::string& href = ecs.getComponent<CAssetRef>(material.albedoMap).path;
	if (href.empty()) {
		return gl.whiteTex;
	}
	const std::string path = rig::image::resolvePath(href);
	if (path.empty()) {
		gAssetFailed = true;
		static std::unordered_set<std::string> logged;
		if (logged.insert(href).second) {
			spdlog::warn("[rigRender3D] albedo not found: {}", href);
		}
		return gl.whiteTex;
	}
	auto it = gl.textures.find(path);
	if (it != gl.textures.end()) {
		return it->second.tex;
	}
	rig::image::GpuTexture gpu;
	if (!rig::image::loadTexture(path, gpu) || gpu.tex == 0) {
		gAssetFailed = true;
		static std::unordered_set<std::string> logged;
		if (logged.insert(path).second) {
			spdlog::warn("[rigRender3D] albedo upload failed: {}", path);
		}
		return gl.whiteTex;
	}
	gl.textures.emplace(path, gpu);
	return gpu.tex;
}

} // namespace

bool sceneHasSurface(MEcs& ecs) {
	for (auto entity : ecs.view<CRenderMaterial>()) {
		(void)entity;
		return true;
	}
	return false;
}

bool surfaceShadersOk() {
	return ensure(glState());
}

bool surfacePresentFailed() {
	return (gShaderTried && gShaderFailed) || gAssetFailed;
}

SurfaceFrame beginSurfaceFrame(MEcs& ecs, const SurfaceLight& light, const glm::mat4& viewProj,
							   const glm::mat4& invViewProj, const glm::vec3& cameraPos, int presentW,
							   int presentH, int originX, int originY) {
	SurfaceFrame frame;
	frame.cameraPos = cameraPos;
	frame.origin = {static_cast<float>(originX), static_cast<float>(originY)};
	frame.presentSize = {static_cast<float>(presentW), static_cast<float>(presentH)};
	auto& gl = glState();
	if (!ensure(gl)) {
		return frame;
	}
	gather(ecs);
	updateIbl(gl, ecs, frame);

	GLint prevFbo = 0;
	GLint prevVp[4] = {0, 0, 0, 0};
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
	glGetIntegerv(GL_VIEWPORT, prevVp);
	const GLboolean blendWas = glIsEnabled(GL_BLEND);
	const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
	const GLboolean cullWas = glIsEnabled(GL_CULL_FACE);
	const GLboolean offsetWas = glIsEnabled(GL_POLYGON_OFFSET_FILL);
	GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
	glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
	GLfloat prevClear[4] = {0.f, 0.f, 0.f, 1.f};
	glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClear);

	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	if (light.mode == 1 && ensureShadow(gl)) {
		glm::vec3 dir = light.dir;
		if (glm::length(dir) < 1e-6f) {
			dir = {0.f, -1.f, 0.f};
		}
		dir = glm::normalize(dir);
		const glm::vec3 eye = -dir * 18.f;
		const glm::vec3 up = std::abs(dir.y) > 0.9f ? glm::vec3(0.f, 0.f, 1.f) : glm::vec3(0.f, 1.f, 0.f);
		const glm::mat4 lightView = glm::lookAt(eye, glm::vec3(0.f), up);
		const glm::mat4 lightProj = glm::ortho(-14.f, 14.f, -14.f, 14.f, 0.5f, 48.f);
		frame.lightViewProj = lightProj * lightView;
		frame.shadow = true;
		frame.shadowTex = gl.shadowTex;

		glBindFramebuffer(GL_FRAMEBUFFER, gl.shadowFbo);
		glViewport(0, 0, kShadowSize, kShadowSize);
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1.5f, 4.f);
		glClear(GL_DEPTH_BUFFER_BIT);
		glUseProgram(gl.depthProg);
		drawCached(ecs, gl, gl.depthVao, 1, frame.lightViewProj, gl.uDepthMvp, -1);
		glDisable(GL_POLYGON_OFFSET_FILL);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}

	const int aoW = std::max(1, presentW / 2);
	const int aoH = std::max(1, presentH / 2);
	if (ensureGbuffer(gl, presentW, presentH) && ensureAo(gl, aoW, aoH)) {
		glBindFramebuffer(GL_FRAMEBUFFER, gl.gFbo);
		glViewport(0, 0, presentW, presentH);
		glClearColor(0.5f, 0.5f, 1.f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glUseProgram(gl.normalProg);
		drawCached(ecs, gl, gl.normalVao, 2, viewProj, gl.uNormalMvp, gl.uNormalModel);

		glBindFramebuffer(GL_FRAMEBUFFER, gl.aoFbo);
		glViewport(0, 0, aoW, aoH);
		glDisable(GL_DEPTH_TEST);
		glUseProgram(gl.aoProg);
		glUniform1i(gl.uAoDepth, 0);
		glUniform1i(gl.uAoNormal, 1);
		glUniform1i(gl.uAoNoise, 2);
		glUniformMatrix4fv(gl.uAoViewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
		glUniformMatrix4fv(gl.uAoInvViewProj, 1, GL_FALSE, glm::value_ptr(invViewProj));
		glUniform3fv(gl.uAoCam, 1, glm::value_ptr(cameraPos));
		glUniform1f(gl.uAoRadius, 0.45f);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, gl.gDepth);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, gl.gColor);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, gl.noiseTex);
		glBindVertexArray(gl.fsVao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		frame.ao = true;
		frame.aoTex = gl.aoTex;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
	glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
	if (blendWas) {
		glEnable(GL_BLEND);
	} else {
		glDisable(GL_BLEND);
	}
	if (depthWas) {
		glEnable(GL_DEPTH_TEST);
	} else {
		glDisable(GL_DEPTH_TEST);
	}
	if (cullWas) {
		glEnable(GL_CULL_FACE);
	} else {
		glDisable(GL_CULL_FACE);
	}
	if (offsetWas) {
		glEnable(GL_POLYGON_OFFSET_FILL);
	} else {
		glDisable(GL_POLYGON_OFFSET_FILL);
	}
	glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
	glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	return frame;
}

void drawSurfaceMesh(MEcs& ecs, entt::entity id, const CTransform& transform, const CMesh& mesh,
					 const CRenderMaterial& material, const glm::mat4& viewProj, const SurfaceLight& light,
					 const SurfaceFrame& frame) {
	auto& gl = glState();
	if (!gl.surfaceOk || mesh.mode != CMesh::Mode::Triangles) {
		return;
	}
	const std::vector<float>* cached = nullptr;
	std::vector<float> local;
	if (id != entt::null) {
		auto it = gVerts.find(static_cast<uint32_t>(id));
		if (it != gVerts.end()) {
			cached = &it->second;
		}
	}
	if (!cached) {
		local = buildVerts(mesh);
		cached = &local;
	}
	if (!cached || cached->empty()) {
		return;
	}

	const glm::mat4 mvp = viewProj * transform.world;
	glUseProgram(gl.surfaceProg);
	glUniformMatrix4fv(gl.uMvp, 1, GL_FALSE, glm::value_ptr(mvp));
	glUniformMatrix4fv(gl.uModel, 1, GL_FALSE, glm::value_ptr(transform.world));
	glUniformMatrix4fv(gl.uLightViewProj, 1, GL_FALSE, glm::value_ptr(frame.lightViewProj));
	glUniform3fv(gl.uCamPos, 1, glm::value_ptr(frame.cameraPos));
	for (int i = 0; i < 9; ++i) {
		glUniform3fv(gl.uSh[i], 1, glm::value_ptr(frame.sh[i]));
	}
	glUniform4f(gl.uLightDirMode, light.dir.x, light.dir.y, light.dir.z, static_cast<float>(light.mode));
	glUniform4f(gl.uLightPosI, light.pos.x, light.pos.y, light.pos.z, light.intensity);
	glUniform4f(gl.uLightColorAmb, light.color.x, light.color.y, light.color.z, light.ambient);
	glUniform4f(gl.uAlbedoMetal, material.albedoR, material.albedoG, material.albedoB, material.metallic);
	glUniform4f(gl.uEmissiveRough, material.emissiveR, material.emissiveG, material.emissiveB,
				material.roughness);
	glUniform4f(gl.uShadowAo, frame.shadow ? 1.f : 0.f, 1.f / static_cast<float>(kShadowSize),
				frame.ao ? 1.f : 0.f, 0.85f);
	glUniform4f(gl.uOriginSize, frame.origin.x, frame.origin.y, frame.presentSize.x, frame.presentSize.y);
	glUniform1f(gl.uIblIntensity, frame.ibl ? frame.iblIntensity : 0.f);
	glUniform1i(gl.uAlbedo, 0);
	glUniform1i(gl.uShadow, 1);
	glUniform1i(gl.uAo, 2);

	const unsigned albedo = albedoTexture(gl, ecs, material);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, albedo);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, frame.shadow ? frame.shadowTex : gl.whiteTex);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, frame.ao ? frame.aoTex : gl.whiteTex);

	uploadMesh(gl, gl.surfaceVao, *cached, 3);

	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace ecs
} // namespace rigkit
