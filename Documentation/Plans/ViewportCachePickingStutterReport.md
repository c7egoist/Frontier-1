# Viewport Stutter, Disk Cache & Exact Picking — Remediation Report

**Date**: 2026-11-22  
**Branch**: `arena/1fb41db4-frontier-1`  
**Status**: Complete & Verified (`ViewportCachePickingValidation` `45/45`, `DynamicClipmapNaniteViewportValidation` `24/24`, `ClusterLodBakeValidation`, `VisibilityRasterValidation` `15/15`, `NaniteViewportVehicleValidation` all green; `Frontier` / `FrontierEditor` link clean)

---

## 1. Reported Symptoms → Root Causes → Permanent Fixes

`FrontierTiming.log` recorded `957` `[SPIKE]` frames (`302` `SceneSync>=1 ms`, `21` `Gizmo>=5 ms`, `177` `UI>=10 ms`, `73` `Present>=10 ms`, and `692` `UnaccountedGap > 15 ms` with `67–147 ms` gaps at `F=6127..6728`). Every category traced back to a concrete, reproducible defect:

| # | Symptom | Root Cause | Permanent Fix |
| :-- | :--- | :--- | :--- |
| 1 | `Present` spikes + `UnaccountedGap` | One in-flight swapchain slot, one acquire semaphore, one fence reused every frame; each single-time upload leaked its `VkCommandBuffer` | `ImGuiOverlayRasterization` now owns `2` in-flight slots (`ImageAcquireSemaphores`, `PresentCompletionSignals`, `InFlightCommandBuffers`), per-swapchain-image render-complete semaphores + image fences, and `LastPresentedImageIndex` readback; `VulkanWorldRenderSequence::UploadToDeviceBuffer` frees its one-shot command buffer |
| 2 | Idle `UnaccountedGap` (`67–147 ms`) | Idle loop blocked in `glfwWaitEvents` with no upper bound | `DesktopWorkspaceLaunch` waits with `WaitOperatingEventsTimeout(1/120 s - elapsed)` only while the pointer is still, no button is held, and no text field is focused |
| 3 | Per-spike file stalls | Telemetry opened/closed `FrontierTiming.log` per emitted line | `TimingLogBufferedWriter` keeps one `ofstream` open with a `32 KiB` staging buffer and explicit `Flush()` |
| 4 | `Gizmo>=5 ms` while scrubbing | Synchronous `9x9` GPU gizmo hover readback (fence wait + `vkMapMemory`) inside the mouse-move path | `ViewportPanel` / `EditorLifecycle` evaluate hover analytically through `EvaluateRayHover` — zero GPU stalls |
| 5 | `SceneSync>=1 ms` on warm start | `ClusterPyramid` QEM bake, local BVH4 SAH build, and local SDF voxelisation re-ran on every launch | Derived asset cache in `build/AssetCache/`: `Pyr_<hash>.cpy1`, `Blas_<hash>.wbvh4`, `Sdf_<hash>.sdv1` |
| 6 | `SceneSync` on material edits | Any material edit re-uploaded the whole scene and rebuilt the SAH BVH | `SynchronizeMaterialEntriesOnly` uploads only the `GpuWorldMaterial80` array + re-dispatches radiosity; `RigidClusterInstance::DedicatedMaterialOverride` keeps the cut signature stable |
| 7 | `SceneSync` on rigid gizmo drags | Every joint transform rebuilt the scene BVH | Two-level `AssetCutBlasCache` (`std::deque`, address-stable) keyed by per-asset cluster cut + a cheap top-level BVH4 connector |
| 8 | Cloud compute every frame | Volumetric cloud pass dispatched unconditionally | `CachedCloudLayerSignature` (camera + sun + cloud params + wind) skips the dispatch; `RequestSkyCloudCacheRefresh` forces it |
| 9 | GI/SDF rebake while orbiting | Dynamic clipmap scrolling was on by default | `DynamicClipmapScrolling = false` default with explicit `RequestStaticGiBake` / `RequestSdfVolumeBake` / `RequestClusterLodRebuild` |
| 10 | Bright square on the ground plane | GDF voxel-cone self-bleed on coplanar receivers, hard clipmap box boundary, and a below-horizon cone fallback to sky radiance | Self-clear gate + smooth box-edge fade in `TraceSingleRadiosityCone`, `EvaluateAmbientEnvironmentFallback` for `N.z < 0`, edge-faded bounce term, receiver fade for SDF AO, and `BoxMin <= BoxMax` lane guard in `IntersectFourBoxes` |
| 11 | Imprecise picking | `PickShowcaseGalleryEntryAtUv` intersected a coarse world AABB | Exact local `WideBoundingTree` BLAS ray-triangle trace (AABB only used when an asset has no baked BLAS) |
| 12 | No marquee selection | Only single-click picking existed | `PickShowcaseGalleryEntriesInUvBox` + `ViewportPanel::ConfigureMarqueeSelectionRect` overlay + `EditorLifecycle` LMB drag multi-selection |

---

## 2. Disk Asset Cache Design

`ShowcaseGalleryScene.cpp` hashes each asset's local smooth triangles, authored dimensions, and a per-asset salt, then caches three derived artefacts under `build/AssetCache/`:

| File | Producer | Contains |
| :--- | :--- | :--- |
| `Pyr_<hash>.cpy1` | `ClusterPyramid::SerializeToBinaryBytes` | `CPY1` **v4**: header, positions, clustered triangles, 128 B cluster descriptors, DAG groups / finer indices / roots, and a `256 x uint32_t` per-material coarsest-allowed LOD cap trailer |
| `Blas_<hash>.wbvh4` | `WideBoundingTree::SerializeToBinaryBytes` | `WBVH4` 4-wide SAH BVH4 + `RayTrianglePrimitive` payload for exact picking and local ray queries |
| `Sdf_<hash>.sdv1` | `LocalSignedDistanceVolume::SerializeToBinaryBytes` | `SDV1` binary16 local signed distance volume (dimensions + voxel half-distance samples) |

`CPY1` was bumped from `v3` to `v4` precisely so `ConfigureMaterialCoarsestAllowedLevel` caps (e.g. `ControlVehicle` windshield glass pinned at `LOD <= 2`) survive a warm cache load instead of silently reverting to `0xFFFFFFFF`. `DeserializeFromBinaryBytes` rejects `v3` payloads, so stale caches are discarded and re-baked rather than mis-read.

`QueryShowcaseAssetCacheStats()` / `ResetShowcaseAssetCacheStats()` expose the six counters (`PyramidDiskHitCount`, `PyramidBakeCount`, `BlasDiskHitCount`, `BlasBakeCount`, `SdfDiskHitCount`, `SdfBakeCount`) used by the validation exhibit.

---

## 3. GPU Cache Telemetry

| Query | Meaning |
| :--- | :--- |
| `QueryAssetCutBlasBuildCount()` | Per-asset cluster-cut SAH builds executed (should plateau after warm-up) |
| `QueryAssetCutBlasReuseCount()` | Cache hits served from `AssetCutBlasCache` |
| `QueryNaniteLodStats().GeometryRebuildCount` | Times the clamped cut actually changed enough to re-upload the GPU BVH |
| `QueryCloudLayerDispatchCount()` | Cloud compute dispatches (increments only on signature change or explicit refresh) |
| `QueryGlobalDistanceFieldStats().FineCascadeCenterWorld` | Pinned fine clipmap center (proves static cascades stop scrolling) |

---

## 4. Ground-Plane Bright Square — Shader Changes

`Engine/Shaders/GeometricRaster/WorldViewportRadiometricCompute.slang`:

1. **`EvaluateAmbientEnvironmentFallback(ConeDir)`** — replaces the raw `EvaluateSkyDome(ConeDir) * 0.34` for rays below the horizon with a sun-lit, sky-lit ground-bounce albedo blend (`float3(0.24, 0.23, 0.22)`), so downward cones no longer pick up full sky irradiance.
2. **`TraceSingleRadiosityCone(ReceiverPos, ReceiverGeomN, StartP, ConeDir, TanHalfAngle, MaxSteps)`** — six-argument form (all four call sites updated: voxel-cone GI, glossy cone, and the two-bounce path). Each voxel contribution is scaled by:
   - `SelfClearGate = smoothstep(MipVox * 0.42, MipVox * 1.25, dot(SampleP - ReceiverPos, ReceiverGeomN))` — removes coplanar 2x2x2 mip-dilation self-bleed on flat ground/walls.
   - `BoxEdgeWeight = 1 - smoothstep(0.76, 0.96, max(|FineRel|))` — smoothly fades the contribution to zero at the fine clipmap boundary, so the box perimeter is a gradient rather than a hard bright/dark edge.
3. **`EvaluateGdfVoxelConeGi`** — returns the ambient fallback outside the fine cascade and cross-fades between the fallback and the traced irradiation with the same `0.76–0.96` receiver fade.
4. **`EvaluateGlobalDistanceAmbientOcclusion`** — the raw AO is blended toward `1.0` across `0.76–0.96` of the coarse cascade, eliminating the AO skirt boundary.
5. **Bounce term** — `SampleRadiosityPyramidMip` is now offset by `Vox0 * 1.45` and blended with `EdgeFade * saturate(RadFetch.a * 2.5)` instead of applying the full radiosity bounce everywhere.
6. **`IntersectFourBoxes`** — unused BVH4 lanes carry `Min = 0`, `Max = 0`, which satisfies `NearT <= FarT`; the new `select(BMinX <= BMaxX, HitNear, 1e30)` guard rejects them.

Measured result on `08_LitGtxSdfGi_Fixed.png` (640x400, floor band `y = 0.72..0.95`): mean `92.48`, std `36.96`, **max adjacent column-mean jump `2.38/255`**, max adjacent row-mean jump `2.56/255` — a continuous gradient with no clipmap box discontinuity. `06_SdfAmbientOcclusion_Fixed.png` open floor reads `254.70/255` while the tyre contact patch still reads `55.00/255`.

---

## 5. Exact Picking & Marquee Selection

- **`PickShowcaseGalleryEntryAtUv`** — builds (or reuses) the asset's local `WideBoundingTree` BLAS via `FindOrBuildAssetPickTree`, transforms the camera ray into asset-local space (`RotateAroundZ` un-yaw + per-axis inverse scale), and calls `TraceClosestRay`. Analytical `IntersectRayAabb` / `IntersectRaySphere` remain only as a fallback for assets with no baked triangles (procedural proxies).
- **`PickShowcaseGalleryEntriesInUvBox`** — projects the 8 oriented bounding-box corners of each visible asset plus its centre, keeps every asset whose projected extent intersects the marquee rectangle, excludes `> 10 m` architectural slabs unless their centre is inside the box, and appends visible LTC area-light quads.
- **`ViewportPanel::ConfigureMarqueeSelectionRect`** — fills `ColorRgba{232,182,92,34}` and strokes `WorkspaceSurfaceColors::SelectionAmber` for the active drag rectangle.
- **`EditorLifecycle`** — `MarqueePressCandidate` → `MarqueeBoxActive` (after a `5 px` dead zone) → marquee commit with `Shift`/`Ctrl` additive merge, otherwise `ClearSelection()` then `SelectSingle`.

Measured: across a `96x64` UV sweep, `2966` rays hit the coarse world AABB but only `1810` hit the exact mesh, i.e. **`1156` (39.0%) AABB-only false hits are now rejected**. A `0.02..0.98` marquee returns `2` visible tokens including the `ControlVehicle`; a `0.00..0.03` sky-corner marquee returns `0`.

---

## 6. Validation Results

`./build/ViewportCachePickingValidation` — **`45 passed, 0 failed`**:

| Group | Key measurements |
| :--- | :--- |
| Disk cache | Cold `384.8 ms` (`1` `.cpy1` bake, `1` `.wbvh4` build, `1` `.sdv1` bake) → warm `91.1 ms` (`1/1/1` disk hits, `0` bakes) = **`4.22x`** |
| `CPY1` v4 | Magic `CPY1`, version `4`, `2747.5 KB`, per-material caps survive all `256` slots byte-for-byte |
| Exact picking | Centre UV `(0.491, 0.438)` → vehicle token; sky pixel → invalid; `39.0%` of AABB hits rejected by the mesh |
| Marquee | Full-view box `2` tokens (vehicle included), sky corner `0`, all tokens visible |
| Cloud signature | Step 1 dispatch `1` → identical repeat `1` (skipped); after camera move `2`; after `RequestSkyCloudCacheRefresh` `3` |
| Nanite cut reuse | Identical repeat: `0` new SAH builds, `0` geometry rebuilds |
| Rigid translation | `0` new BLAS builds, `2` cached reuses; no CPU BVH rebuild |
| Material-only edit | BLAS builds `3 -> 3`, cut rebuilds `4 -> 4` |
| Static clipmap | Fine cascade pinned at `(0.000, 0.000, 0.919)` across camera motion |

`./build/DynamicClipmapNaniteViewportValidation` — **`24 passed, 0 failed`** (dynamic scrolling still honoured when explicitly enabled, open floor `254.70/255`, contact AO `55.00/255`, Nanite `21,314 -> 10,584 -> 9,064` tris at `5.8 m -> 15.5 m -> 34.0 m`).

`./build/NaniteViewportVehicleValidation` — `near 16,116 tris` vs `far 9,370 tris` (`L0 1,600 / L1 3,802 / L2 3,968`) vs `far LOD-off 25,080 tris`.

### 6.1 Frame-Pacing Proof (`FrontierEditor --frames=120`)

A `120`-frame `FrontierEditor` capture on the same `2`-vCPU SwiftShader machine gives a directly comparable `FrontierTiming.log`:

| Stage | Median | p95 | Max |
| :--- | ---: | ---: | ---: |
| `SceneSync` | `0.00 ms` | `0.00 ms` | `15.92 ms` (warm-up only) |
| `Gizmo` | `0.00 ms` | `0.00 ms` | `0.61 ms` |
| `UI` | `1.16 ms` | `1.85 ms` | `10.45 ms` |
| `Present` | `195.37 ms` | `235.33 ms` | `256.41 ms` (software rasteriser) |
| **Unaccounted gap (`F >= 10`)** | **`0.08 ms`** | **`0.12 ms`** | **`0.17 ms`** |

**Zero** steady-state frames exceed the `15 ms` unaccounted-gap threshold (previously `692`), and there are **zero** `Gizmo` spikes (previously `21`, max `30.10 ms`) because the synchronous `9x9` GPU hover readback is gone. The only remaining `> 15 ms` gaps are `F=1..3` (`2,474 / 2,650 / 3,984 ms`) — one-time Vulkan device creation plus SwiftShader Subzero JIT compilation of the seven compute pipelines, which is startup work that no per-frame change can absorb.

---

## 7. Files Touched

- `Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.{h,cpp}` — 2-slot in-flight presenter.
- `Engine/Components/Diagnostics/ViewportTimingTelemetry.h` — `TimingLogBufferedWriter`.
- `Engine/Lifecycle/DesktopWorkspaceLaunch.cpp` — `120 Hz` bounded idle wait.
- `Engine/DistanceFieldSpace/LocalSignedDistanceVolume.{h,cpp}` — `SDV1` codec.
- `Engine/SpatialAcceleration/ClusterPyramid.cpp` — `CPY1` v4 (`MaterialCoarsestAllowedLevels` trailer).
- `Engine/GeometricRaster/Visibility/ClusterCullEncoder.h` — `DedicatedMaterialOverride`.
- `Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h` — `DynamicClipmapScrolling = false`.
- `Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.{h,cpp}` — explicit bake triggers, `SynchronizeMaterialEntriesOnly`, cloud signature cache, `AssetCutBlasCache` + reuse/build counters, `FreeCommandBuffer`.
- `Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.{h,cpp}` — disk cache, cache statistics, exact BLAS picking, marquee box picking.
- `Engine/Shaders/GeometricRaster/WorldViewportRadiometricCompute.slang` — bright-square remediation.
- `Engine/Components/Panes/ViewportPanel.{h,cpp}` — analytic hover + marquee overlay.
- `Engine/Lifecycle/EditorLifecycle.{h,cpp}` — marquee multi-selection + `SynchronizeMaterialEntriesOnly` wiring.
- `Exhibits/ViewportCachePickingValidation.cpp` *(new)* — `45`-check cache / stutter / picking validation.
- `CMakeLists.txt` — registers the new exhibit.
- `Exhibits/Images/DynamicClipmapNanite/*`, `Exhibits/Images/NaniteViewport/*` — regenerated proofs.
