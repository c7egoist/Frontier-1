# 🧩 `Engine/GeometricRaster/DirectLighting/`

> **TL;DR**: Zero-SDF Direct Lighting: 2 Production Shadow Algorithms (`ContactPcss` 3.91 mm/tx 2-Cascade + BVH4 + `DeformingCapsuleRig`, and `SolarDiscRayQuery` GTX BVH4 / RTX `VK_KHR_ray_query` + Plane-Aware Filter), `SurfelOccluderField` Bunnell AO, and Clipped-Polygon LTC Area Lights.

---

## Files

| File | Purpose |
| :--- | :--- |
| `SurfelOccluderField.h` | 32-byte stratified surface blocker disks (`SurfelOccluderDisk`) & Bunnell solid-angle soft occlusion / 2-scale chassis AO |
| `SurfelOccluderField.cpp` | Area-stratified surfel extraction from `WideBoundingTree` and analytical Bunnell disk occlusion queries |
| `SolarShadowSequence.h` | Top-2 Direct Solar Shadow Algorithms (`ContactPcss` & `SolarDiscRayQuery`), 2-Cascade Orthographic Clip, and Plane-Aware Filter |
| `SolarShadowSequence.cpp` | Implementation of 3.91 mm/tx 2-cascade Contact PCSS + BVH4 contact gate + `DeformingCapsuleRig` & 4-ray Vogel Solar Disc Ray Query |
| `LinearCosineLightIntegration.h` | Clipped-Polygon Linearly Transformed Cosine (LTC) Quad Area Lights, Point Lights, Surfel Chassis AO & Lagarde Specular AO |
| `LinearCosineLightIntegration.cpp` | Full Direct Lighting Integrator coupling `SolarShadowSequence`, LTC Area Lights, and `SurfelOccluderField` AO |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `SurfelOccluderDisk`, `SurfelOccluderField`, `SolarShadowAlgorithm`, `SolarDirectionalSource`, `OrthographicShadowCascade`, `SolarShadowFieldResult`, `SolarShadowSequence`, `PolygonalAreaQuadLight`, `OmnidirectionalPointLight`, `AmbientOcclusionSettings`, `DirectLightingOutput`, `LinearCosineLightIntegration`
