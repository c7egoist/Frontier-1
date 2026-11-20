# 🧩 `Engine/Shaders/DirectLighting/`

> **TL;DR**: Zero-SDF Direct Solar Shadow `.slang` Shaders: Shared GTX 4-Wide BVH4 + RTX `VK_KHR_ray_query` + `GpuTaperedCapsule32` (`WideTreeRayQuery.slang`), `ContactPcss` (`ContactClipShadow.slang`), `SolarDiscRayQuery` (`SolarFrustumShadow.slang`), and `PlaneAwareShadowFilter.slang`.

---

## Files

| File | Purpose |
| :--- | :--- |
| `WideTreeRayQuery.slang` | Shared ordered 128 B 4-Wide BVH4 ray query, optional `FRONTIER_USE_RAY_QUERY` hardware `RayQuery`, and 32 B `GpuTaperedCapsule32` soft cones |
| `ContactClipShadow.slang` | Shadow #1 (`ContactPcss`): 3.91 mm/tx 2-Cascade Blocker-Adaptive PCSS + Short-Range BVH4 Contact + Capsule Rig Soft Cone |
| `SolarFrustumShadow.slang` | Shadow #2 (`SolarDiscRayQuery`): 4-Ray Vogel-Disk Solar Cone + IGN Rotation + `SurfelOccluderDisk` Penumbra |
| `PlaneAwareShadowFilter.slang` | Surface-Plane & Depth-Gradient Edge-Stopping Penumbra Filter preserving razor-sharp 0 m contact roots |
