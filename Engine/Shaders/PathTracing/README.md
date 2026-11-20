# 🧩 `Engine/Shaders/PathTracing/`

> **TL;DR**: 6-Queue Lock-Free Wavefront Deterministic Path Tracer `.slang` Shaders compiled twice (`_GTX.comp.spv` over `WideBoundingTree` BVH4 and `_RTX.comp.spv` over `VK_KHR_ray_query`) with 64-bit split-word fixed-point accumulation (`DistinctDigests == 1`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `DeterministicSampling.slang` | Stateless 32-bit Laine-Karras Owen-Scrambled Sobol (0,2), GGX Spherical-Cap VNDF, Power-Heuristic MIS & Split-Word Fixed-Point Atomics |
| `WavefrontPrimaryTrace.slang` | Primary Camera Ray Trace (`WavefrontPrimaryTrace_{GTX,RTX}.comp.spv`) writing `SurfaceHitQueueA` & Primary AOVs |
| `WavefrontSurfaceScatter.slang` | OpenPBR NEE + VNDF/Cosine BSDF Scatter (`WavefrontSurfaceScatter.comp.spv`) enqueuing `BounceRayQueue` & `ShadowRayQueue` |
| `WavefrontBounceTrace.slang` | Secondary Closest-Hit (`WavefrontBounceTrace_{GTX,RTX}.comp.spv`) & Shadow Any-Hit (`WavefrontShadowAnyHit_{GTX,RTX}.comp.spv`) |
| `WavefrontAtrousFilter.slang` | Split-Word Fixed-Point Finalize (`WavefrontFinalizeSample.comp.spv`) & Optional Albedo-Demodulated A-Trous (`WavefrontAtrousStep.comp.spv`) |
