# rigRender3D


![preview](examples/orbit/img/preview.png)


Code pack - GLES mesh present over [rigComponent](https://github.com/rigkid/rigComponent) `CCamera` + `CMesh` (`faceColors` / `facePalette` + `CPalette` ramps, optional `CIndexedAtlas` + `texcoords`) + `CDrawStyle`, with optional `CLight` (banded shade by default; ordered Bayer dither optional). Optional low-res present via `CCamera::presentWidth` / `presentHeight` (nearest upscale). Corner FPS is host `GraphicsSettings::showFps`, not this pack. Pi floor: same shader path as host GLES2 / ANGLE.

`CMesh::texcoords` on the Lambert path need a scene `CIndexedAtlas`. Albedo stays face or style color without one.

`CRenderMaterial` (`rig.render.material`) on a triangle mesh or `CSphere` takes the surface path: albedo map (`CAssetRef` image), metallic, roughness, emissive, GGX, ACES into the 8-bit target. `CRenderEnvironment` (`rig.render.environment`) points `hdri` at a `CAssetRef` lat-long Radiance image; this pack projects that image to L2 irradiance. A directional light gets a 1024 shadow map (3 by 3 PCF) and the frame gets half-resolution SSAO when a depth texture can be allocated. Both stay off when it cannot. Point lights are not shadowed. Specular from the HDRI is that same low-order lobe.

## Build Example

[examples/orbit](examples/orbit/) - low-poly planet system: faceted `CMesh` planet + two orbiting moons, a circling point light, and a `COrbitDrive` camera

```bash
cmake -S examples/orbit -B examples/orbit/build
cmake --build examples/orbit/build --target orbit
```

Bootstrap: register **rigComponent**, then **rigSystems**, then **rigRender3D**. When any `CCamera` is active, `rigSystems` skips 2D mesh present so this pack owns meshes.

## Style

Format first-party `src/` and `examples/` with clang-format before commit (never `third_party/`).

```bash
./tools/format.sh
./tools/install-hooks.sh    # once per clone - pre-commit formats staged C/C++
```

Hooks **fail** if you stage C/C++ and `clang-format` is missing. Put it on PATH:

- **Windows:** install [LLVM](https://github.com/llvm/llvm-project/releases) (enable "Add LLVM to the system PATH"), or `winget install LLVM.LLVM`, then open a new shell. Confirm: `clang-format --version`.
- **macOS:** `brew install clang-format`
- **Linux:** `sudo apt install clang-format`

From a RigKit host checkout, `tools\install-hooks.bat` / `./tools/install-hooks.sh` wires every pack repo. Full policy: [RigKit packs Style](https://github.com/rigkid/RigKit/blob/main/packs/README.md#style).

[API/docs](https://rigkid.github.io/rigRender3D/)
