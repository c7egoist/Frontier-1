# 🧩 `Engine/GeometricRaster/PathTracing/`

> **TL;DR**: Unified 6-Queue Lock-Free Wavefront Deterministic Path Tracer (`DeterministicPathTraceSequence`) running on **both GTX (`WideBoundingTree` BVH4 compute)** and **RTX (`VK_KHR_ray_query` hardware BLAS/TLAS)** with 64-bit split-word fixed-point accumulation (`DistinctDigests == 1` across batch sizes).

---

## Files

| File | Purpose |
| :--- | :--- |
| `WavefrontQueueLayout.h` | GPU-aligned 64 B `WavefrontSurfaceHit64`, 48 B `WavefrontBounceRay48`, 48 B `WavefrontShadowRay48`, and `WavefrontPathTraceUniforms` |
| `DeterministicPathTraceSequence.h` | Dual-tier (`GtxWideBvh4Compute` & `RtxHardwareRayQuery`) 6-queue Wavefront Path Tracer & CPU reference evaluator |
| `DeterministicPathTraceSequence.cpp` | CPU reference + Vulkan 6-queue compute (`_GTX.comp.spv` & `_RTX.comp.spv`) + optional `VK_KHR_acceleration_structure` BLAS/TLAS builder |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `PathTraceExecutionTier`, `WavefrontSurfaceHit64`, `WavefrontBounceRay48`, `WavefrontShadowRay48`, `DeterministicPathTraceConfiguration`, `DeterministicPathTraceResult`, `DeterministicPathTraceSequence`
