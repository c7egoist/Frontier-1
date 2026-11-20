# World Shadows, Dual GTX Global Illumination, Deterministic Path Tracer — Architecture Plan

> **Scope**: Unified **Zero-SDF** C++20 + `.slang` architecture selecting the **Top 2 Solar/Direct Shadow Algorithms**, **Top 2 GTX Global Illumination Renderers**, and **1 Unified Deterministic Path Tracer (GTX BVH4 + RTX RayQuery)** across `SultanAladin/Frontier` (`536cfe1` / `daed305`) and `SultanAladin/Frontier-1` (`arena/39c6910b-frontier-1` @ `d5bbb81`).

---

## 1. Audit of Earlier Commits & Existing Implementations

### 1.1 Confirmed Files in Git History (`SultanAladin/Frontier`)

Both validation suites you referenced exist intact in earlier commits on `SultanAladin/Frontier` (right before commit `63db120` removed GI/SDF):

| Commit | Files Confirmed | Measured Benchmark Highlights |
| :--- | :--- | :--- |
| **`2d6f15ec29d1`** | `Exhibits/CleanEmissiveGiCandidates.h` (`2,472` lines)<br>`Exhibits/CleanEmissiveGiValidation.cpp` (`1,245` lines)<br>`Exhibits/Images/CleanEmissiveGi/validation_summary.json` | Zero-denoiser `560×315` (`25,068` vehicle tris, `42` emissive tris) RMSE vs 128-SPP NEE:<br>• **Cand 1 (`StokesContourEmissiveGi`)**: **`0.2238`** (#1)<br>• **Cand 2 (`SurfelCascadeEmissiveGi`)**: **`0.2239`** (#2, zero 3D SDF)<br>• **Cand 3 (`ContinuousFieldEmissiveGi` Voxel SDF)**: `0.2259`<br>• Old `Engine DDGI`: `0.5361` |
| **`536cfe11c2f7`** | `Exhibits/CleanShadowAndGiCandidates.h` (`605` lines)<br>`Exhibits/CleanEmissiveShadowValidation.cpp` (`1,077` lines)<br>`Exhibits/Images/CleanEmissiveShadows/shadow_validation_summary.json` | Deforming 10-capsule character rig update: **`0.081 ms`** (vs `~600 ms` LocalSDF rebake).<br>Shadow RMSE vs 128-SPP BVH Solar Disc:<br>• **Cand 2 (`SurfelOccluderField + BoneCapsules`, zero 3D SDF)**: **`0.2973`** (#1 shadow)<br>• **Cand 1 (`HybridCone + BoneCapsules`)**: **`0.2983`** (#1 combined Lit HDR RMSE **`0.1340`**)<br>• **Cand 3 (`ClipmapCones` 3D Voxel SDF)**: `0.3068` (Lit HDR RMSE `0.2464`)<br>• `Before Engine VoxelSdfClipmap`: `0.4170` |

### 1.2 Confirmed Assets in `SultanAladin/Frontier-1` (`arena/39c6910b-frontier-1` @ `d5bbb81`)

| Subsystem | Location in `SultanAladin/Frontier-1` | Key Capabilities |
| :--- | :--- | :--- |
| **Shadow Lab (6 Non-SDF + SDF Modes)** | `Engine/GeometricRaster/DirectLighting/SolarShadowSequence.{h,cpp}`<br>`Engine/Shaders/DirectLighting/{ContactClipShadow,SolarFrustumShadow,PlaneAwareShadowFilter,WideTreeRayQuery}.slang`<br>`Experimental/ShadowAlgorithmLab/` | `ContactPcss` (`3.91 mm/tx` 2-cascade + `0.24 m` BVH4 contact gate), `PcssPlaneAware`, `ShadowRayMarch` (SMRT), `PcfBilinear`, `WideTreeRayQuery` (Vogel solar disc BVH4/RayQuery), plus legacy SDF modes (`5u..7u`). |
| **Emissive GI Shootout & `SH9+LTC` Winner** | `Exhibits/EmissiveGiShootoutValidation.cpp` (`460cfc8` / `d5bbb81`)<br>`Engine/GeometricRaster/GlobalIllumination/{DynamicIrradianceCacheSequence,WeightedPathReplaySequence,IrradianceLatticeSequence}.{h,cpp}` | `ExactTriFormFactor (Stokes) + SH9 Field + LTC` won the 106-emissive-triangle shootout (**`sRGB RMSE = 0.1331`** vs SDF Penumbra `0.1669`). |
| **Deterministic Path Tracer** | `Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h`<br>`Experimental/DeterministicPathTracer/Source/{RadianceIntegration,WavefrontIntegration}.wgsl`<br>`Documentation/Plans/DeterministicPathTracerVulkanOptimizationPlan.md` | Laine-Karras Owen-Scrambled Sobol `(0,2)` + Dupuy-Benyoub (2023) Spherical-Cap GGX VNDF + Stich SBVH + 6-queue lock-free Wavefront Integrator with 96-bit fixed-point `atomicAdd` (**100% bit-exact**, `1.41×–1.62×` CPU/SwiftShader speedup, `2.2×–3.8×` projected discrete GPU speedup). |

---

## 2. Architectural Verdict: Should We Still Use 3D SDF?

### Verdict: **NO — Retire 3D Voxel / Mesh SDF Volumes Completely**

1. **Empirical Quality Proof**:
   - In **Shadows** (`536cfe1`), `SurfelOccluderField + DeformingBoneCapsules` achieved **lower RMSE (`0.2973`)** than both `HybridCone` (`0.2983`) and `3D Voxel Clipmap SDF` (`0.3068`), while `Before Engine SDF` scored `0.4170` due to AABB boundary discontinuities and missing character deformation.
   - In **Emissive GI** (`2d6f15e` & `d5bbb81`), analytic **Stokes Spherical-Polygon Contour + Surfel/SH9** (`RMSE = 0.2238` / `0.1331`) and **Surfel Radiosity + Octahedral Irradiance Cache** (`RMSE = 0.2239`) beat 3D Voxel SDF Clipmaps while eliminating denoiser blotches, thin-wall light leaks, and 3D grid staircase banding.
2. **Dynamic Geometry & Memory Wins**:
   - **Zero `~600 ms` `LocalGeometrySdfAsset` rebakes** — moving vehicles, fractured convex hulls, and deforming characters work every tick without voxelization stalls.
   - **Saves `32–96 MiB` of 3D clipmap/volume VRAM** and removes `Engine/DistanceFieldSpace/` entirely.
   - **Single Unified Spatial Accelerator**: `SpatialAcceleration::WideBoundingTree` (`128 B` 4-wide BVH4 for GTX compute) + `VK_KHR_acceleration_structure` (`VK_KHR_ray_query` BLAS/TLAS for RTX) + lightweight analytic `DeformingBoneCapsule` (`32 B`/limb, `0.08 ms` update) serve **every** shadow, GI, reflection, and path-tracing unit.
3. **Only Two Distance Primitives Retained (Zero 3D Grids)**:
   - **Analytic `DeformingBoneCapsule`** (`32 B` `{JointStart, JointEnd, RadiusStart, RadiusEnd}`) for `0.08 ms` articulated character soft shadow cones & contact occlusion.
   - **2D Glyph `FontCodec::SignedDistanceField`** strictly for UI typography rendering.

---

## 3. Selected Algorithms to Implement in C++20 & `.slang`

### 3.1 Best 2 Shadow Algorithms (`Engine/GeometricRaster/DirectLighting/`)

| Slot | Identifier | Target Hardware | Core Mechanism (Zero 3D SDF) |
| :---: | :--- | :--- | :--- |
| **Shadow 1** *(Default Fast GTX)* | `SolarShadowAlgorithm::ContactPcss` | GTX / Integrated / Mobile / RTX | **2-Cascade Orthographic Contact Clip (`3.91 mm/tx` Cascade 0, `15.6 mm/tx` Cascade 1)** + **`0.28 m` `WideBoundingTree` BVH4 Contact Ray Gate** + **Analytic `DeformingBoneCapsule` Cone Evaluation** + **Vogel Blocker Search PCSS** + **3-Step Plane-Aware Edge-Preserving Filter**. Zero shadow acne, crisp grounded foot/tyre contact, smooth distance-hardening penumbra. |
| **Shadow 2** *(Exact Reference / Soft Penumbra)* | `SolarShadowAlgorithm::SolarDiscRayQuery` *(paired with `SurfelBlockerDisks` fast cone option)* | GTX (`WideBoundingTree` BVH4) & RTX (`VK_KHR_ray_query`) | **Owen-Sobol `(0,2)` Stratified Vogel Solar-Disc Raytrace (2–4 SPP live + Coplanar Dilation + Plane-Aware Filter)** with **Area-Stratified `SurfelOccluderField` + `DeformingBoneCapsule` Analytic Blocker Fallback** for zero-noise soft solar penumbras without denoiser ghosting. |

*(Note: Fast fallback `PcfBilinear` can remain as a lightweight Low-quality viewport mode inside `SolarShadowSequence` if desired, while all `VoxelSdf*` / `HybridSdf` modes are retired.)*

---

### 3.2 Best 2 GTX Global Illumination Renderers (`Engine/GeometricRaster/GlobalIllumination/`)

Both GI renderers run natively at 60 FPS on **GTX-class GPUs** via `.slang` compute (`WideBoundingTree` BVH4 + analytical integration, **zero hardware RT required**), while automatically accelerating ray queries via `VK_KHR_ray_query` when an RTX GPU is present:

| Engine | Identifier | Strengths & Scene Fit | Core Mechanism (Zero Denoiser Blotches, Zero 3D SDF) |
| :---: | :--- | :--- | :--- |
| **GTX GI #1** *(Analytic Emissive + Surfel Radiosity)* | `ContourSurfelIlluminationSequence` *(evolves Cand 1 `StokesContour` + Cand 2 `SurfelCascade` + `SH9+LTC`)* | **Best for Emissive-Heavy, Architectural, Studio & Vehicle Night/Dusk Scenes** (`RMSE = 0.1331` / `0.2238` — **zero denoiser needed!**) | 1. **Exact Stokes Spherical-Polygon Contour / LTC Integration** over clustered emissive triangles & area lights (headlights, taillights, signage) — zero Monte Carlo variance at any brightness.<br>2. **Soft Occlusion** via `SurfelOccluderField` stratified blocker disks + `0.28 m` BVH4 contact gate + `DeformingBoneCapsule` rig.<br>3. **2-Bounce Diffuse Bleed** via area-stratified `SurfelCascade` (`~300–512` surface radiosity disks projected into smooth tricubic-filtered `SH9` irradiance cells with normal-offset anti-banding). |
| **GTX GI #2** *(Dynamic Octahedral Irradiance + Sobol RIS Replay)* | `DynamicIrradianceCacheSequence` + `WeightedPathReplaySequence` | **Best for Large Dynamic Outdoor/Indoor Worlds, Sun/Sky Bounce & Moving Objects** | 1. **Dynamic Octahedral Irradiance Cache (`8×8` irradiance + `16×16` Chebyshev moments + Backface Gating `>0.25`)** traced against `WideBoundingTree` BVH4 on GTX (`VK_KHR_ray_query` on RTX).<br>2. **1-Bounce Owen-Sobol `(0,2)` 4-Candidate Weighted Path Replay (RIS)** feeding off the `DynamicIrradianceCache` for the 2nd bounce + `32 B` `BounceSlabEntry` surface summaries.<br>3. **Edge-Aware Joint Albedo/Normal/Depth Spatial-Temporal Filter** for clean 1-SPP indirect contact detail. |

---

### 3.3 Unified Deterministic Path Tracer — Both GTX & RTX (`Engine/GeometricRaster/PathTracing/`)

One single bit-exact mathematical integrator running identically on **CPU CTest**, **GTX Vulkan Compute (`WideBoundingTree` 4-Wide BVH4)**, and **RTX Vulkan (`VK_KHR_ray_query` BLAS/TLAS)**:

| Component | Mechanism |
| :--- | :--- |
| **Quasi-Monte Carlo Sampler** | Laine-Karras **Owen-Scrambled Sobol `(0,2)`** (`DeterministicIntegrators.h` / `.slang`) with coherent shared `(0,m,2)` tile scramble + Blue-Noise toroidal screen-space mask (`Zero RNG`). |
| **BSDF & Light Transport** | Full **10-lobe OpenPBR** (`OpenPbrSurfaceScattering.slang`) + **Dupuy-Benyoub (2023) Spherical-Cap GGX VNDF** + **Power-Heuristic MIS + Next-Event Estimation (NEE)** across Neckel & Labs Solar Disc, Stokes/LTC Emissives, 3-LUT Atmosphere, 6-Moon Exospheres, and BSC5 Starfield. |
| **GTX + RTX Dual Traversal** | Shared `WideTreeRayQuery.slang` compile-time specialization:<br>• **`GTX` (`FRONTIER_USE_HW_RAY_QUERY=0`)**: Stackless-first-child ordered 4-wide `WideBoundingTree` BVH4 (`128 B` nodes, `32 B` SoA triangles, 2-`vec4u` compact instances, Stich SBVH spatial splits).<br>• **`RTX` (`FRONTIER_USE_HW_RAY_QUERY=1`)**: Inline `RayQuery<RAY_FLAG_FORCE_OPAQUE>` over Vulkan `VkAccelerationStructureKHR` TLAS/BLAS using the exact same Sobol samples & shading code. |
| **6-Queue Deterministic Wavefront Compute** | Splits high-VGPR megakernel into 6 low-register `.slang` compute dispatches (`GeneratePrimary` `8×8` tiles → `PrepareQueue` → `TraceClosest` → `ShadeScatter` → `TraceShadow` → `FinalizeSample`) using **96-bit fixed-point (`2^28` scale) carry-propagated `atomicAdd`** so parallel bounce/NEE accumulation is **100% associative, commutative, and bit-exact** across GTX and RTX. |
| **Interactive Viewport Mode** | Orbit history reprojection + optional 3-step albedo/normal/depth A-Trous reconstruction at `1–4 SPP` during camera motion, converging progressively to raw bit-exact `N-SPP` ground truth when stationary (`PathTraceDenoise` toggle). |

---

## 4. Canonical `WorldRenderEngine` Lineup (5 Clean Viewport Modes)

| Ordinal | `WorldRenderEngine` Enum | Description |
| :---: | :--- | :--- |
| `0` | `MatCap` | Studio clay/metal/zebra curvature inspection (`0` shadow maps, `0` GI) |
| `1` | `OpenPbrDirect` | Direct 10-lobe OpenPBR + LTC Area Lights + Top-2 Solar Shadows + BVH4/Sky Specular Reflections |
| `2` | `ContourSurfelGi` *(GTX GI #1)* | **Denoiser-Free Analytic Stokes Contour Emissive + 2-Bounce Surfel/SH9 GI** + Top-2 Shadows + Reflections |
| `3` | `DynamicIrradianceGi` *(GTX GI #2)* | **Dynamic Octahedral Irradiance Cache + Owen-Sobol RIS BVH4 Replay GI** + Top-2 Shadows + Reflections |
| `4` | `DeterministicPathTrace` *(GTX + RTX)* | **6-Queue Wavefront Owen-Sobol `(0,2)` MIS/NEE Path Tracer** (`GTX` 4-Wide BVH4 & `RTX` `VK_KHR_ray_query`) |

---

## 5. Proposed Repository & `Engine/` Folder Structure (Zero `DistanceFieldSpace/`, Naming-Compliant)

All folder, file, class, and shader names strictly obey `Documentation/FolderStructure.md`, `Documentation/AgenticInstructions/SKILL-Naming.md`, and `Documentation/BannedWordsVocabulary.md` (**zero `And`**, **zero `Probe`**, **zero `Reservoir`**, **zero `Pass`/`Stage`/`Pipeline`/`Pool`**, **zero `DistanceFieldSpace/`**):

```text
Frontier-1/
├── CLAUDE.md
├── AGENTS.md
├── CMakeLists.txt
├── README.md
│
├── Engine/
│   ├── SpatialAcceleration/
│   │   ├── README.md
│   │   ├── ClusterPyramid.{h,cpp}                      // 128-triangle LOD cluster DAG
│   │   ├── TriangleVisibilitySpace.h                   // Compact ray/visibility triangle layout
│   │   ├── WideBoundingTree.{h,cpp}                    // 128 B 4-wide BVH4 + Stich SBVH builder (GTX backbone)
│   │   └── DeformingCapsuleRig.{h,cpp}                 // Analytic 32 B tapered bone capsules (0.08 ms pose update)
│   │
│   ├── GeometricRaster/
│   │   ├── Visibility/                                 // 64-bit Visibility Buffer, Hi-Z pyramid & analytic resolve
│   │   │   ├── DepthPyramidSequence.{h,cpp}
│   │   │   ├── ClusterCullEncoder.{h,cpp}
│   │   │   ├── VisibilityClusterRasterization.{h,cpp}
│   │   │   └── VisibilityMaterialResolve.{h,cpp}
│   │   │
│   │   ├── DirectLighting/                             // Top-2 Solar Shadows, Surfel Blockers & Analytic LTC Lights
│   │   │   ├── README.md
│   │   │   ├── SolarShadowSequence.{h,cpp}             // Shadow #1 ContactPcss (3.91mm 2-cascade + BVH4 + Capsules)
│   │   │   │                                           // Shadow #2 SolarDiscRayQuery (Sobol Vogel BVH4 / RTX RayQuery)
│   │   │   ├── SurfelOccluderField.{h,cpp}             // Area-stratified blocker disks + capsule soft cone shadow
│   │   │   └── LinearCosineLightIntegration.{h,cpp}    // Analytic polygonal LTC lights + 2.2 m chassis AO
│   │   │
│   │   ├── GlobalIllumination/                         // Top-2 GTX Global Illumination Renderers (Zero 3D SDF)
│   │   │   ├── README.md
│   │   │   ├── DeterministicIntegrators.h              // Shared Owen-Sobol (0,2) & Dupuy-Benyoub GGX VNDF
│   │   │   ├── ContourSurfelIlluminationSequence.{h,cpp}// GTX GI #1: Stokes Spherical-Polygon + 2-Bounce Surfel + SH9
│   │   │   ├── DynamicIrradianceCacheSequence.{h,cpp}  // GTX GI #2a: 8x8 Octahedral + 16x16 Chebyshev BVH4/RTX cache
│   │   │   └── WeightedPathReplaySequence.{h,cpp}      // GTX GI #2b: 4-Candidate Sobol RIS 1-bounce + joint filter
│   │   │
│   │   ├── PathTracing/                                // Unified GTX (BVH4) + RTX (RayQuery) Deterministic Path Tracer
│   │   │   ├── README.md
│   │   │   ├── WavefrontQueueLayout.h                  // SoA ray/scatter/shadow queue & 96-bit fixed-point layouts
│   │   │   └── DeterministicPathTraceSequence.{h,cpp}  // CPU reference + Vulkan 6-queue Wavefront (GTX & RTX)
│   │   │
│   │   ├── WorldRendering/                             // Unified 5-Engine World Orchestrator & Specular Reflections
│   │   │   ├── README.md
│   │   │   ├── SpecularReflectionSequence.{h,cpp}      // Coherent VNDF BVH4/RTX reflections & Abbe glass refraction
│   │   │   ├── ShowcaseGalleryScene.{h,cpp}            // Studio showcase, ControlVehicle & deforming character scene
│   │   │   ├── TieredWorldRenderSequence.{h,cpp}       // CPU 5-engine world sequence (CTest & offscreen parity)
│   │   │   └── VulkanWorldRenderSequence.{h,cpp}       // Vulkan .slang GPU 5-engine world sequence (GTX & RTX)
│   │   │
│   │   └── Atmosphere/                                 // 3-LUT Sky, Neckel & Labs Sun, PlanetaryClouds, 6 Moons, BSC5 Stars
│   │       └── ...
│   │
│   └── Shaders/
│       ├── DirectLighting/
│       │   ├── WideTreeRayQuery.slang                  // Unified GTX 4-Wide BVH4 & RTX VK_KHR_ray_query traversal
│       │   ├── ContactClipShadow.slang                 // Shadow #1: 2-Cascade PCSS + 0.28 m BVH4 + Capsule cone
│       │   ├── SolarFrustumShadow.slang                // Shadow #2: Owen-Sobol Vogel solar disc + Surfel blocker cone
│       │   └── PlaneAwareShadowFilter.slang            // Coplanar dilation + 3-step continuous-sigma edge filter
│       │
│       ├── GlobalIllumination/
│       │   ├── ContourSurfelIllumination.slang         // GTX GI #1: Stokes emissive contour + Surfel radiosity + SH9
│       │   ├── DynamicIrradianceCache.slang            // GTX GI #2a: Octahedral irradiance + Chebyshev moments
│       │   ├── WeightedPathReplay.slang                // GTX GI #2b: 4-candidate Sobol RIS 1-bounce + cache 2nd bounce
│       │   └── WeightedTemporalFilter.slang            // Joint albedo/normal/depth spatial-temporal filter
│       │
│       ├── PathTracing/
│       │   ├── DeterministicSampling.slang             // Owen-Sobol (0,2), BlueNoise mask & Spherical-Cap GGX VNDF
│       │   ├── WavefrontPrimaryTrace.slang             // 8x8 tile primary ray gen + closest-hit (GTX BVH4 / RTX RQ)
│       │   ├── WavefrontBounceTrace.slang              // Compacted bounce closest-hit + shadow NEE (GTX BVH4 / RTX RQ)
│       │   ├── WavefrontSurfaceScatter.slang           // 10-lobe OpenPBR MIS + NEE + 96-bit fixed-point atomicAdd
│       │   └── WavefrontAtrousFilter.slang             // Optional 3-step albedo/normal/depth reconstruction
│       │
│       └── WorldRendering/
│           ├── SpecularGlassRaytrace.slang             // Coherent cone-footprint specular & Abbe dielectric glass
│           ├── WorldViewportRadiometricCompute.slang   // Unified MatCap / OpenPbrDirect / GTX GI #1 / GTX GI #2 resolve
│           └── WorldViewportBlit.slang
│
├── Exhibits/
│   ├── SolarShadowValidation.cpp                       // Validates ContactPcss + SolarDiscRayQuery + DeformingCapsuleRig
│   ├── ContourSurfelGiValidation.cpp                   // Validates GTX GI #1 (Stokes + Surfel + SH9, zero denoiser)
│   ├── DynamicIrradianceGiValidation.cpp               // Validates GTX GI #2 (Octahedral Cache + WeightedPathReplay)
│   ├── DeterministicPathTraceValidation.cpp            // Validates bit-exact Sobol digests + GTX BVH4 == RTX RayQuery
│   ├── TieredWorldRenderValidation.cpp                 // 5-engine CPU/GPU parity sheet & RMSE gate
│   └── Images/
│       ├── DirectLighting/
│       ├── GlobalIllumination/
│       ├── PathTracing/
│       └── WorldRendering/
│
└── Experimental/
    ├── FrontierEditor/                                 // Live on :5173 (Sun Studio carousel completed)
    └── ProjectZeroEditor/                              // Live on :8092 (Sun Studio carousel completed)
```

*(Note: `Engine/DistanceFieldSpace/` is completely omitted, and `Engine/GeometricRaster/Baking/` drops `GpuSdfBaker` so the entire engine has zero 3D SDF maintenance or memory overhead.)*

---

## 6. Phased Implementation Roadmap

| Step | Deliverable | Verification Gate |
| :---: | :--- | :--- |
| **1** | **Import Clean Engine Baseline + `DeformingCapsuleRig`**<br>Bring `Engine/`, `EngineContent/`, `CMakeLists.txt`, `Automation/`, `Documentation/` into `c7egoist/Frontier-1` without `Engine/DistanceFieldSpace/`; extract `DeformingCapsuleRig.{h,cpp}` (`0.08 ms` pose update) from `536cfe1` into `Engine/SpatialAcceleration/`. | `WideBoundingTree` + `DeformingCapsuleRig` unit & timing validation (`< 0.5 ms` 10-capsule pose update). |
| **2** | **Top-2 Solar Shadow Pipeline (`ContactPcss` + `SolarDiscRayQuery` + `SurfelOccluderField`)**<br>Port `SolarShadowSequence.{h,cpp}` (`3.91 mm/tx` 2-cascade `ContactPcss` + `SolarDiscRayQuery`) and `SurfelOccluderField.{h,cpp}` from `536cfe1` & `d5bbb81`, coupled to `DeformingCapsuleRig` in C++20 and `.slang`. | `SolarShadowValidation` visual comparison sheet (`ContactPcss` vs `SolarDiscRayQuery` vs `128-SPP` ground truth, Pose A & Pose B, zero AABB rings). |
| **3** | **GTX GI #1 (`ContourSurfelIlluminationSequence` — Zero Denoiser)**<br>Promote `StokesContourEmissiveGi` + `SurfelCascadeEmissiveGi` (`2d6f15e`/`536cfe1`) + tricubic `SH9` (`d5bbb81`) into production `Engine/GeometricRaster/GlobalIllumination/ContourSurfelIlluminationSequence.{h,cpp}` + `ContourSurfelIllumination.slang` (zero 3D SDF). | `ContourSurfelGiValidation` (`ControlVehicle` 76k-nit headlights + taillights + studio pillars, `RMSE <= 0.224`, zero denoiser blotches). |
| **4** | **GTX GI #2 (`DynamicIrradianceCacheSequence` + `WeightedPathReplaySequence`)**<br>Port `DynamicIrradianceCacheSequence` (`8×8` octahedral + `16×16` Chebyshev + backface gating) and 4-candidate Owen-Sobol `WeightedPathReplaySequence` (`d5bbb81`) running purely on `WideBoundingTree` BVH4 (GTX) and `VK_KHR_ray_query` (RTX). | `DynamicIrradianceGiValidation` indoor/outdoor sun-bounce & dynamic moving vehicle parity on CPU & Vulkan `.slang`. |
| **5** | **Unified GTX + RTX `DeterministicPathTraceSequence` (6-Queue Wavefront)**<br>Port `DeterministicIntegrators.h` + the 6-queue lock-free Wavefront architecture (`WavefrontIntegration` with 96-bit fixed-point `atomicAdd`, Stich SBVH, 10-lobe OpenPBR MIS/NEE) into `Engine/GeometricRaster/PathTracing/` + `Engine/Shaders/PathTracing/` with dual `_GTX.comp.spv` (BVH4) and `_RTX.comp.spv` (`VK_KHR_ray_query`) targets. | `DeterministicPathTraceValidation`: `DistinctDigests == 1` across batch sizes, `AnalyticSoftbox` RMS `< 0.1%`, GTX vs RTX pixel parity. |
| **6** | **5-Engine `TieredWorldRenderSequence` + `VulkanWorldRenderSequence` + Editor UI Wiring**<br>Wire all 5 engines (`MatCap`, `OpenPbrDirect`, `ContourSurfelGi`, `DynamicIrradianceGi`, `DeterministicPathTrace`) and Top-2 Shadow modes into `ViewportPanel` AND sync the matching Viewport Render / Shadow / GI / Path Tracer controls into `FrontierEditor` & `ProjectZeroEditor`. | 5-engine side-by-side `.png` proof in `Exhibits/Images/WorldRendering/` + live preview servers (`5173` & `8092`). |
