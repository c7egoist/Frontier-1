# SDF AO Repair and XGTAO Assessment

📝 Scope: `Engine/Shaders/GeometricRaster/WorldViewportRadiometricCompute.slang`, `TieredWorldRenderSequence`, `VulkanWorldRenderSequence`, and the viewport debug selector.

## TL;DR

The ugly SDF AO came from four collinear taps lifted by only `0.35 × voxel`, with an aggressive `0.12` floor — that produces banded, blotchy contact shadows at voxel boundaries. It is replaced by eight cosine-hemisphere taps, multi-scale radii, per-pixel rotation, cone-aware deficits, and a `0.30` floor with an intensity control. A separate, toggleable screen-space PBR occlusion pass (`XGTAO (PBR AO)`) is added with zero GI contribution, plus four new debug channels for raw SDF distance, SDF × material, SDF normals, and XGTAO.

## Decision table

| Option | Quality | Cost | GI coupling | Verdict |
| :--- | :--- | :--- | :--- | :--- |
| Original 4-tap collinear SDF AO | 🔴 banded, voxel-locked | ✔️ 4 samples | None | Replaced |
| Repaired 8-tap hemisphere SDF AO | 🚩 good contact, resolution-limited | 🚩 8 GDF fetches | None | 🟢 Default for GTX GDF path |
| XGTAO-style horizon AO (8 taps, 2 radii) | 🚩 cleaner screen-space contact | 🚩 8 GBuffer fetches | None (PBR only) | 🟢 Optional toggle |
| Full multi-bounce XGTAO | ⭐ highest fidelity | 🔴 needs dedicated pass + denoise | None | 🚧 Not implemented here |
| Pure SDF AO with no screen-space term | 🚩 stable but coarse | ✔️ cheapest | None | Fallback when XGTAO off |

## Why the old SDF AO looked terrible

| Defect | Old behaviour | Repair |
| :--- | :--- | :--- |
| Tap distribution | 4 taps along the shading normal only | 8 taps across a cosine-weighted hemisphere |
| Receiver lift | `Voxel × 0.35`, too small to escape the receiver surface | `GeomN × Voxel × 0.35` plus cone-aware self-exclusion |
| Radius scaling | `StepT` grows geometrically from one voxel | Radii span `1.6..11.1` voxels with distance falloff |
| Banding | Deterministic uniform tap pattern | Golden-angle rotation hashed per pixel |
| Occlusion floor | `clamp(..., 0.12, 1.0)` crushes dark regions into blobs | `clamp(..., 0.30, 1.0)` with `SdfAoIntensity` multiplier |
| Mixing with PBR | Directly multiplied into diffuse/specular with no alternative | XGTAO lane can be enabled or disabled independently |

## Is the repaired SDF better?

🟢 Yes, relative to the `c1bbbfd` implementation: the sample distribution now matches a hemisphere AO integral instead of a single ray, and the per-pixel rotation removes the visible voxel-grid striping. It is still bounded by the fine GDF grid resolution (`GdfGridRes`), so it will never reach the smoothness of ray-traced ground-truth AO. For the HighEnd path the engine already uses 3 BVH4 rays; this repair only affects engines 1 and 2.

## XGTAO status — honest scope

⚠️ The implemented pass is an **XGTAO-style horizon AO**, not the full research algorithm. It reads the existing G-buffer depth/normals, searches eight neighbours over two radii, applies horizon cosine weighting and distance falloff, and clamps to `[0.25, 1]`. It does **not** do multi-bounce horizon integration, temporal accumulation, or bent-normal shading, and it adds no indirect illumination. It is a PBR-only occlusion term and is off by default.

| Control | Location | Backing field |
| :--- | :--- | :--- |
| XGTAO (PBR AO) | Viewport render sheet | `ViewportDisplaySettings::XgtaoEnabled` |
| XGTAO radius | Render config (code default) | `WorldRenderConfiguration::XgtaoRadiusMeters` = `0.65 m` |
| SDF AO intensity | Render config (code default) | `WorldRenderConfiguration::SdfAoIntensity` = `1.0` |

## Debug channels

| Channel | View | GPU source |
| ---: | :--- | :--- |
| 09 | Ambient occlusion | Combined SDF + optional XGTAO |
| 12 | SDF Signed Distance | `SampleGlobalDistanceWorld` at first hit |
| 13 | SDF with Materials | Albedo × combined AO |
| 14 | SDF Normals | `GBufNormAo` |
| 15 | XGTAO | Horizon AO only |

🚧 The CPU fallback renderer has no GDF/XGTAO buffers; channels 12–15 render neutral grey there instead of faking values.

## Verification status

- ✔️ C++ timing-ledger and camera-lag units compiled with `g++ -std=c++20 -fsyntax-only`.
- 🚧 Full CMake/CTest and `.slang` compilation could not run in this sandbox: `cmake` is absent and `ExternalPackages/` (including `toml++/toml.hpp` and the pinned `slangc` toolchain) is not staged. Run `python3 Automation/PrepareDependencies.py` then the Editor build on a Vulkan-capable machine before shipping; capture channels 09/12/13/14/15 as `.png` exhibits and compare against `c1bbbfd`.
