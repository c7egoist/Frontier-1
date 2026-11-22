# Nanite Cluster LOD DAG — GeometryCompiler Parity Upgrade Report

**Date**: 2026-11-22  
**Branch**: `arena/1fb41db4-frontier-1`  
**Status**: Complete & Verified (`ClusterLodBakeValidation`, `VisibilityRasterValidation`, `NaniteViewportVehicleValidation`, `DynamicClipmapNaniteViewportValidation`, and `GeometryCompilerValidation.mjs` all green)

---

## 1. Architecture Summary

Frontier's C++ Nanite-like cluster hierarchy (`Engine/SpatialAcceleration/ClusterPyramid.{h,cpp}`) and Vulkan/CPU viewport pipelines were upgraded to match the directed acyclic simplification hierarchy, topological collapse guards, culling stages, and inspection channels of `Experimental/GeometryCompiler`:

| Subsystem | Previous Implementation | GeometryCompiler-Parity Implementation |
| --- | --- | --- |
| **Spatial Cluster Partitioning** | Flat 30-bit Morton chunking only | 30-bit Morton Z-order octant seeding + Hoare `PartitionSelectNth` balanced spatial median bisection per material slot (`<= 128` tris/cluster, 100% material-pure) |
| **Multi-Cluster Grouping** | Stride-4 index slices along Morton array | Greedy shared-border-edge adjacency grouping (`TargetGroupClusters = 4`), leftover singleton border fold (`<= 2x` cap), and local spatial reach pairing for disconnected components |
| **QEM Simplification** | $O(N^2)$ linear triangle scan, hard-locked open borders | Indexed binary min-heap (`CollapsePriorityQueue`) with 11-float area-weighted plane quadrics, `4 * area` perpendicular open-border quadrics, `FreeInterior` / `OpenSheetBorder` / `LockedBoundary` classification, `VerifyLinkCondition`, `VerifyNoLockedBridge`, `VerifyOrientationPreserved`, and material-boundary locks |
| **Monotone DAG & Spheres** | Flat per-cluster scan using min cluster distance | Explicit `ClusterGroupEntry` DAG (`Groups`, `GroupFinerClusterIndices`, `RootClusterIndices`) with nested monotone group bounding spheres (`* 1.0001f` float32 guard) and strictly monotone errors |
| **Cut Selection & Culling** | $O(\text{all clusters})$ linear scan | $O(\text{active cut})$ top-down DAG walk from `RootClusterIndices` (`SelectMonotonicCut`) with optional 6-plane local frustum subtree pruning, cluster normal-cone backface culling, forced hierarchy level (`0..N`), and exact parity with `SelectExhaustiveReferenceCut` |
| **Serialization** | `CPY1` v1/v2 (clusters + triangles only) | `CPY1` v3 appending full DAG `Groups`, `GroupFinerClusterIndices`, and `RootClusterIndices` while preserving backward-compatible v1/v2 loading |
| **Viewport Inspection & Controls** | Channels `17` (LOD Level) & `18` (Clusters); On/Off + Pixel Error | Added Channel `19` (`Nanite Groups`), Channel `20` (`Nanite Error Budget` heat ramp), analytic screen-space barycentric `Triangle Wireframe` overlay (`AtmosphereMaskPad.w` bit 4), `Force Level` (`Auto`/`L0`–`L3`), `DAG Frustum Cull`, `Normal-Cone Cull`, `.RenderSpace` persistence, and live culled/percentage footer telemetry |

---

## 2. Modified Files

- `Engine/SpatialAcceleration/ClusterPyramid.{h,cpp}` — Shared-edge DAG builder, `CollapsePriorityQueue` guarded QEM simplifier, monotone group spheres/errors, top-down DAG cut (`SelectMonotonicCut`) + oracle (`SelectExhaustiveReferenceCut`), and `CPY1` v3 binary serialization.
- `Engine/GeometricRaster/Visibility/ClusterCullEncoder.{h,cpp}` — Scale-aware `PixelsPerRadian`, local-space 5-plane frustum construction, and forced-level forwarding into `SelectMonotonicCut`.
- `Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.{h,cpp}` — Extended `WorldRenderConfiguration` with Nanite culling/wireframe/forced-level settings and CPU debug channels `19` (`Nanite Groups`) & `20` (`Nanite Error Budget`).
- `Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.{h,cpp}` — Extended `NaniteViewportLodStats` (total clusters, frustum/backface culled counts, tested DAG groups), packed `GroupOrdinal` (`Edge02Pad.w`) and `ErrorRatio` (`Normal0Pad.w`) into `GpuShadedTriangle96` (`96 B` preserved), and wired `NaniteWireframeOverlay` into `WorldViewportUniformBlockStd140` (`2960 B` preserved).
- `Engine/Shaders/GeometricRaster/WorldViewportRadiometricCompute.slang` — Unpacked `GroupOrdinal`, `ErrorRatio`, and analytic screen-space `EdgePixelDist` in `TraceSceneClosest`; implemented debug modes `19`, `20`, and crisp wireframe overlay across Lit/MatCap/Debug views.
- `Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.cpp` — Configured `ControlVehicle` pyramid bake with explicit `ClusterLodBakeConfiguration` (`MaximumLevels = 8`, `TargetGroupClusters = 4`, `LockMaterialBoundaries = true`).
- `Engine/Components/Panes/ViewportPanel.{h,cpp}`, `Engine/Lifecycle/EditorLifecycle.cpp`, `Engine/Codecs/RenderSpaceCodec.cpp` — Exposed Nanite `Force Level`, `DAG Frustum Cull`, `Normal-Cone Cull`, `Triangle Wireframe`, quick `Inspect` pills (`Lit`/`LOD`/`Cls`/`Grp`/`Err`), debug channels `19`–`20`, `.RenderSpace` encode/decode, and live footer telemetry.
- `Exhibits/ClusterLodBakeValidation.cpp` & `Exhibits/NaniteViewportVehicleValidation.cpp` — Added 2,048-tri & 8,192-tri closed watertight torus DAG tests (10/10 camera poses x 2 error budgets exact oracle match + 0 open / 0 non-manifold seam edges) and GPU contact sheet `Exhibits/Images/NaniteViewport/VehicleGeometryCompilerDagSheet.png`.

---

## 3. Automated Validation & Visual Proofs

1. **`ClusterLodBakeValidation`**: **PASS** — Two-material grid (`800 -> 120` tris, 100% single-material clusters, cap & `CPY1` v3 round-trip) + Closed Watertight Torus DAG (`5` levels, `8` groups, `2048 -> 128` tris, `20/20` exact DAG-vs-oracle matches, `0` open edges & `0` non-manifold edges across mixed-LOD cuts, frustum + normal-cone culling verified).
2. **`VisibilityRasterValidation`**: **PASS (`15/15`)** — Reversed-Z Hi-Z occlusion, Phase-2 disocclusion rescue, `<4 px` software vs `>=4 px` hardware cluster split, 2-layer glass depth peel, and analytic barycentric/Jacobian resolve (`Exhibits/Images/Visibility/01-VisibilityBufferAndAnalyticResolve.png`).
3. **`NaniteViewportVehicleValidation`**: **PASS** — Vulkan SwiftShader GPU viewport (`ControlVehicle`, `25,068` L0 tris across `228` material-pure clusters -> `16,116` near tris -> `9,370` far tris across `L0`/`L1`/`L2`), rendering both `VehicleLodContactSheet.png` and `VehicleGeometryCompilerDagSheet.png` (Simplification Groups `19` + Wireframe & Error Budget `20`).
4. **`DynamicClipmapNaniteViewportValidation`**: **PASS (`24/24`)** — Dynamic SDF cascade tracking, contact AO, and monotonic Nanite triangle scaling (`21,314 -> 10,584 -> 9,064` tris across `5.8 m -> 15.5 m -> 34.0 m`, `00_DynamicClipmapAoNaniteComparison.png`).
5. **`GeometryCompilerValidation.mjs`**: **PASS (`6/6` JS reference suites)**.
