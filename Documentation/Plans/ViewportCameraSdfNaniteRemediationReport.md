# 🧩 Viewport Camera · Gizmo · Global Distance Field · Nanite Cluster Remediation Report

> **TL;DR**: Four root causes found and fixed — (1) RMB mouse-look was re-damped by the camera-lag component every frame (**turning ran at 23.4 % of the mouse rate, measured**), (2) the gizmo hover was driven by a **stale 5×5 GPU pick patch**, so it latched onto a rotate arc and translate handles never appeared, (3) the global distance field was **welded to the scene-bounds centre and only rebuilt by a full 64³ re-march**, so shadows stayed behind a moving vehicle and then hitched, (4) the Nanite cut had **no per-instance LOD floor** and its cluster debug views were hash-mush.

Base: `d332837` (merge of `arena/25b070ae-frontier`, containing `c1bbbfd`). Session branch: `arena/38f675ce-frontier-1`.

---

## 1. Why RMB turning was "very slow" (root cause + measurement)

`ProcessShowcaseViewportInteraction` applied the mouse delta to `Cam`, then wrote `DesiredCameraOrigin/Target = Cam`, then let
`CameraLagComponent::AdvanceLag` blend the rendered pose toward that desired pose — from the **same damped pose**. Every frame therefore kept
only `1 - exp(-16·Δt)` of the input:

| Pipeline | Turn produced by 30 frames × 6 px | Share of mouse input |
| :--- | ---: | ---: |
| Legacy (input integrated on the damped pose) | `0.134822 rad` | **23.4 %** |
| Fixed (input integrated on the desired pose) | `0.576000 rad` | **100.0 %** |

Reproduced and verified by `Exhibits/CameraLookHarness/CameraLookRateProbe.cpp` (`RESULT: PASS`).

### Changes
- Input now integrates on the **desired** pose, seeded from `DesiredCameraOrigin/Target` at the top of the tick; the lag component damps the rendered pose toward it. Full-rate look **and** camera lag survive together.
- Look is now **state**, not a re-derived look-at vector: `EditorFlyYawRadians` / `EditorFlyPitchRadians` (`PrimeEditorFlyLookAngles`, `EvaluateEditorFlyForward`). This removes float drift in `Target - Origin`, which is what swung the whole view ("glitch") once the eye was far from the pivot.
- Pitch clamped to ±89°, so the basis can never degenerate.
- Zoom directives (`ZoomIn` / `ZoomOut` / `FitView`) now also refresh the desired pose — previously the next tick re-seeded from a stale desired pose and silently undid the zoom.
- While a gizmo drag owns the pointer the lag component is snapped, so releasing a handle no longer sweeps the view afterwards.
- A stale `PrimaryPointer.Down` can no longer veto RMB look (`LeftHeld` only honours the pointer flag while RMB is free).
- Lag vs. sync behaviour is unchanged for WASD/QE/Shift-sprint and the wheel fly-speed scalar.

---

## 2. Gizmo: "stuck on rotate, move handles missing"

Two independent defects:

1. **Stale GPU pick patch.** `WorldGizmoOverlayRasterization::QueryLastGpuPickAtPixel` searched the cached 5×5 patch even when the cursor had moved away from that patch's anchor, returning whatever handle had been under the *old* cursor (typically a rotation arc). The hover therefore never re-acquired translate arrows. Fixed: the patch is only consulted when it actually contains the queried pixel (+2 px), otherwise the analytic CPU ray hover takes over.
2. **Constant-world-size gizmo.** `SynchronizeShowcaseGizmoAnchor` clamped the gizmo radius to `1.85 m`, so past ~11 m the gizmo stopped growing, shrank to a few pixels, and its handles stopped picking. Fixed: `clamp(DistToCam * 0.17, 0.10, 24.0)` — constant screen size at any distance.
3. A filter change (`W`/`E`/`R`/`Q`) now clears the hovered handle and is blocked during a drag, and the active mode is always on screen (`Gizmo Universal (W/E/R/Q)` in the viewport footer) so a latched filter can never look like a broken gizmo again.

---

## 3. Global distance field: incremental object updates + view-following cascades

### Diagnosis
- Cascade centres came from `(RootBoundsMin + RootBoundsMax) * 0.5` — **fixed at the scene centre** for the session ("static at the centre").
- `DispatchGpuScenePrecompute` only baked when `RebuildGeometryFields || !Ready`; a gizmo drag passed `RebuildShadows = false`, so **nothing** updated while the vehicle moved (shadow/GI lag), and the drag-end path then did one **full 2-cascade 64³ march + 8 radiosity phases** while blocking the CPU (`AwaitTimelineTick`) — the visible hitch.

### Changes
- **Dirty-region invalidation (host)**: `SynchronizeSceneResources` compares per-instance world positions and unions the swept bounds of every moved cluster instance (previous ∪ current footprint). That box is the dirty region.
- **Dirty-region rebake (GPU)**: `GlobalDistanceBakeControl` gained `GlobalDistanceDirtyMin/Max` (`uint`/`float4` pair appended to the 2 992-byte std140 block); `GlobalDistanceBakeEntry` and the radiosity phase 0/4 entry return early outside the dirty box (+5.5-voxel margin), **retaining previously baked values elsewhere**. `UniformStrideBytes` is 8 192, so the larger block is allocation-safe.
- **Streaming instead of a freeze**: a dirty bake is now dispatched on the same frames the object moves, so the field follows the object; a full rebuild is reserved for scene reload, cascade re-centre, or a non-instance scene-tree edit (bounds-change safety net).
- **View-following cascades**: `ConfigureGlobalDistanceFieldFocus` is fed the camera origin; the fine cascade re-centres on the view only after the view leaves 62 % of the current field (wide hysteresis, so ordinary orbiting never rebuilds), and the span grows so the scene stays covered.
- **Telemetry**: `GlobalDistanceFieldReport` (`Ready`, `RebuildCount`, `IncrementalUpdateCount`, `LastDirtyFraction`, `LastDirtyExtentMeters`, `LastUpdateMilliseconds`) is shown in the viewport footer: `GDF live · 37 inc / 1 full · 12.4 %`.

### Verification
`Exhibits/CameraLookHarness/GlobalDistanceIncrementalProbe.cpp` replays the exact rule in CPU space (`RESULT: PASS`):
- moved-object region **matches a full rebake exactly**;
- untouched region stays valid (identical to a full rebake);
- a 0.15 m per-frame drag step stamps **15.9 %** of the grid, a 2.5 m move **25.7 %** — the remaining cost scales with the object footprint, not the scene, and the per-voxel BVH traversal it skips was the dominant term.

### Known gap (stated plainly)
The fine cascade is still a fixed `64³` grid, so the SDF resolution around a 4.5 m vehicle is coarse; that — not the old AO filter — is why "SDF AO" reads chunky. A ring-addressed clipmap (world-voxel modulo addressing, per-slab scroll fill, per-object SDF atlas) is the correct next step and is queued in §5; it was not attempted blind here because it rewrites every sample site in `WorldViewportRadiometricCompute.slang` and cannot be runtime-verified in this sandbox (no Vulkan device, no `ExternalPackages`).

---

## 4. Nanite-like clusters

| Requirement | State |
| :--- | :--- |
| Clusters never shared across material slots | Verified: every baked cluster holds exactly one material slot (`RESULT: PASS`) |
| Per-face material survives every LOD level | Per-face `MaterialOrdinal` rides each pyramid triangle into the GPU shaded-triangle stream |
| **Minimum (coarsest) LOD level per asset/instance** | Added: `RigidClusterInstance::CoarsestLodLevel`, `ClusterPyramid::SelectMonotonicCut(..., CoarsestLevelOverride)`, effective floor = `min(asset floor, instance floor, viewport floor)`; glass shells are floored at level 1 at bake **and** at runtime |
| Viewport-wide floor control | New **Nanite → Min LOD Floor** capsule row (`L0 / L1 / L2 / L4 / Max`) |
| Debug colour views | Channel 17 = LOD level; 18 = cluster ID via a 24-entry categorical palette; **new 19 = cluster density** (triangles in cluster, blue→green→red) |
| Density data | `Edge01Pad.w` now packs `(clusterOrdinal << 8) | trianglesInCluster`; verified exact through float32 |

`Exhibits/CameraLookHarness/ClusterLodFloorProbe.cpp` (`RESULT: PASS`) checks material purity, floor caps at 1 at 0.5 m … 4 000 m, floor 0 never drops below the factory cut, and the packing round-trip.

### Honest caveat
The cut, the floor and the packing are verified on CPU. The GPU views (17/18/19) are the same data path but were **not** rendered here — no Vulkan device is available in this sandbox, so no screenshot is claimed.

---

## 5. Queued next steps

1. Ring-addressed fine clipmap (per-axis slab scroll fill) — removes the last full rebuild on camera re-centre.
2. Object-space SDF atlas pages for dynamic meshes (Unreal `Mesh Distance Field` parity) instead of re-marching scene triangles.
3. Raise the fine cascade to `128³` (buffer, radiosity mip offsets, dispatch counts) so SDF AO stops reading chunky.
4. Per-object LOD floor in the Inspector (currently asset-driven + viewport-wide).

---

## 6. Verification ledger

| Probe | Result |
| :--- | :--- |
| `Exhibits/CameraLookHarness/CameraLookRateProbe.cpp` | **PASS** — legacy 23.4 % vs fixed 100.0 % of mouse rate; identical turn at 1 m and 400 m |
| `Exhibits/CameraLookHarness/ClusterLodFloorProbe.cpp` | **PASS** — material purity, LOD floor, density packing |
| `Exhibits/CameraLookHarness/GlobalDistanceIncrementalProbe.cpp` | **PASS** — dirty rebake == full rebake inside the moved region, valid outside, bounded cost |
| Vulkan viewport render | **Not run** — no GPU device / `ExternalPackages` in this sandbox; no image proof is claimed |
