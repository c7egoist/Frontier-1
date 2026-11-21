# SDF Debug Views & Ambient Occlusion Assessment

**Status:** Report only (no AO shader or debug-channel code changed in this turn).
**Scope:** `c1bbbfd` workspace plus the Project Browser / ThoughtLog imports on this branch.
**Question:** Why does the current SDF AO look bad, is this SDF better than the earlier cleaner result, and should PBR use XGTAO with GI left toggleable?

---

## 1. TL;DR

- The ugly AO is the **GTX Global Distance Field 4-tap approximation** in `WorldViewportRadiometricCompute.slang`, not a physically integrated horizon AO. It has no cosine weighting, only four samples along the shading normal, and uses a quantized, clipmap-sized distance field.
- The cleaner AO remembered earlier is most likely the **CPU two-scale (0.25 m contact + 2.20 m chassis) AO with plane-aware bilateral denoise** in `LinearCosineLightIntegration.cpp`, or a differently tuned clipmap; it is a different algorithm, so this is not a straight regression of one SDF implementation.
- For the **direct PBR path**, add **XGTAO** as the default AO provider with a toggle (Off · XGTAO · BVH ray AO · GDF AO). Keep GDF/BVH shadows and radiosity GI as separate, independently toggleable systems.
- Current debug channel `9 = Ambient Occ` is a single grayscale AO readout, **not** an SDF inspector. The requested SDF-with-materials, normal-SDF and raw-SDF views do not exist yet.

---

## 2. Decision Table

| Option | AO Quality | Cost | Works with GI toggle | Debug value | Rank |
|---|---|---|---|---|---|
| **XGTAO (screen-space horizon + bilateral denoise)** | 🔴 Smooth contact + wide occlusion, stable normals | 🚩 1 compute pass + denoise, depth/normal G-buffer already exists | 🔴 Independent of GI | 🚩 Needs new debug channels | ⭐ Recommended for PBR direct |
| **Retune current GDF 4-tap AO** | 🚩 Still step-shaped, clipmap resolution bound | ✔️ Cheapest (already in shader) | 🔴 Independent | 🔴 Can expose raw GDF | 🚩 Cheap fallback / GTX low tier |
| **3/5-ray BVH AO (current HighEnd fallback)** | 🔴 Accurate, stochastic | 🔴 Ray cost scales with samples | 🔴 Independent | 🚩 No field visualization | ⭐ High-end reference |
| **Replace AO with SDF GI only** | 🚩 AO and GI conflated; contact detail lost | 🚩 Cone march cost | 🔴 Cannot toggle separately | 🔴 SDF only | 🚩 Rejected |

*Legend: ✔️ low / best cost · 🚩 medium · 🔴 high / best quality · ⭐ recommended.*

---

## 3. Root Cause: Why the Current SDF AO Looks Terrible

| Evidence | Source | Effect |
|---|---|---|
| `EvaluateGlobalDistanceAmbientOcclusion` takes **4 samples** along `N` at `StepT = Voxel0 * 0.85` then `×1.85` each step; weights `0.52 → 0.55×` | `Engine/Shaders/GeometricRaster/WorldViewportRadiometricCompute.slang:903` | Coarse, stair-stepped occlusion bands; large empty intervals receive no occlusion |
| `Deficit = saturate((StepT - max(0, Dist)) / StepT)` with `exp(-StepT * 0.55)` falloff | same | Ad-hoc heuristic, not a cosine-weighted hemisphere integral; over-darkens sloped surfaces and under-occludes creases |
| `StartP = Pos + GeomN * (Vox0 * 0.35)` and samples use `N`; no normal/shading-normal split beyond that | same | Normal-map detail and field quantization interact, producing blotches |
| `IsGlobalDistanceFieldReady()` false returns `1.0`; only engines `1..2` use this path | same, line `2016` | Engine-dependent AO jumps between modes |
| GPU debug channel `9` prints `OutDiffAo` after a **3×3** bilateral pass whose sigma depends on trace stride | same, `DenoiseCompositeEntry` around line `2760` | Denoise removes some noise but cannot restore missing angular samples; at reduced trace resolution it smears the bands |
| CPU/Tiered path displays `CombinedDiffuseAo` = `ContactAo × (0.25 + 0.75 × ChassisAo)` from two scales with its own IGN + plane-aware denoiser | `LinearCosineLightIntegration.cpp:582–684`, `TieredWorldRenderSequence.cpp:916` | Visually much cleaner — likely the earlier result being remembered; it is **not** the GDF function |

**Verdict:** the SDF/GDF implementation itself is not "better"; the *render path that was showing AO* changed. There is no single AO algorithm shared by all engines, so comparisons across modes are not apples-to-apples.

---

## 4. Proposed XGTAO + Toggle Design (not implemented)

1. **AO provider selector** in `ViewportDisplaySettings` (new `[UI]` row): `0 = Off`, `1 = XGTAO`, `2 = BVH ray AO`, `3 = GDF AO`. Default `1` for engine `1/3` PBR; `3` retained for GTX low tier.
2. **XGTAO compute unit** as `.slang` under `Engine/Shaders/GeometricRaster/`, reading the existing G-buffer position/normal/depth, with:
   - horizon-angle search on 8–16 interleaved gradient-noise directions,
   - radius in metres (default 0.8 m) and bias to reject self-occlusion,
   - normal-weighted cosine falloff and thickness-aware backface term,
   - 3×3 bilateral depth/normal denoise reusing `DenoiseCompositeEntry` structure.
3. **GI stays independent**: `RenderEngineIndex`, `SdfShadowPenumbraIndex` and radiosity toggles remain untouched; `AOProvider` multiplies only direct diffuse/specular occlusion.
4. **Quality gate**: compare 4-frame offscreen `.png` captures at `1×`, `½×`, `¼×` trace scale; require no checkerboard and `≤1–5%` pixel delta against the HighEnd BVH ray-AO reference except at contact edges, where XGTAO may improve SNR.

---

## 5. Debug View Gap Analysis

| Requested view | Current state | Proposed channel |
|---|---|---|
| SDF with materials | No dedicated view; channel `2 = Albedo` ignores SDF | New `Gdf+Albedo`: final material albedo with GDF occlusion isolines |
| Normal SDF | No view | New `GdfNormal`: gradient of the GDF decoded to RGB, zero/failed samples in magenta |
| Raw SDF / distance | No view | New `GdfDistance`: signed distance in false colour with clipmap voxel-size contour |
| GDF AO before denoise | No view | New `AoRaw` next to existing `Ambient Occ` |
| SDF shadow cone visibility | No view; channel `8 = Sun Shadow` shows final visibility only | New `SdfConeVis` using `EvaluateSolarShadowCone` sample path |
| Radiosity / clipmap mips | No view | New `GdfRadiosityMip0..3` debug selecting `SampleRadiosityMipTrilinear` |
| Existing 12 channels | Off · Depth · Albedo · Shading Nrm · Roughness · Metalness · Cluster ID · Geom Nrm · Sun Shadow · Ambient Occ · Direct Lit · Specular | Keep unchanged for compatibility; append new channels `12..17` |

All new channels must stay GPU-only `.slang` overlays/composite passes — no ImGui or CPU rasterization of debug geometry.

---

## 6. Verification Status

- This report is source-inspection only; no `.png` proof was captured because the AO shader was not changed and the desktop Vulkan host is not available in this headless workspace.
- `Documentation/UIControllableItems.md` records AO/GDF parameters as `[Backend]` and flags XGTAO as proposed, not implemented.
- Next implementation turn should capture baseline `Ambient Occ` channel `.png` at each render engine before touching the shader.
