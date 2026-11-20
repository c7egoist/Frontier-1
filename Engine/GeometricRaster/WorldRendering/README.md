# 🧩 `Engine/GeometricRaster/WorldRendering/`

> **TL;DR**: Unified Zero-SDF 5-Engine World Render Orchestrators (`MatCap`, `OpenPbrDirect`, `ContourSurfelGi`, `DynamicIrradianceGi`, `DeterministicPathTrace`), Coherent VNDF Specular & Abbe Glass Raytracing, and the Canonical Showcase Gallery Scene.

---

## Files

| File | Purpose |
| :--- | :--- |
| `SpecularReflectionSequence.h` | Coherent GGX VNDF Specular Reflections & 3-Channel Cauchy/Abbe Dielectric Glass Refraction over `WideBoundingTree` |
| `SpecularReflectionSequence.cpp` | Implementation of BVH4 / Sky specular reflection and dispersed glass transmission |
| `ShowcaseGalleryScene.h` | Canonical Architectural Cornell-Studio Gallery World Scene (`SurfelOccluderField`, `DeformingCapsuleRig`, Outliner registration) |
| `ShowcaseGalleryScene.cpp` | Scene builder and fast rigid-cluster incremental acceleration rebuilds |
| `TieredWorldRenderSequence.h` | Unified 5-Engine CPU World Render Sequence (`WorldRenderEngine`, `WorldSceneSpecification`, `WorldRenderConfiguration`) |
| `TieredWorldRenderSequence.cpp` | Orchestrates Visibility, DirectLighting, `ContourSurfelGi`, `DynamicIrradianceGi`, `DeterministicPathTrace`, Atmosphere & Fluid |
| `VulkanWorldRenderSequence.h` | Hardware/SwiftShader Vulkan `.slang` Compute World Render Orchestrator (30 pure buffer bindings, zero 3D SDF images) |
| `VulkanWorldRenderSequence.cpp` | Executes `WorldViewportRadiometricCompute.slang` and 6-queue / progressive Path Tracing on Vulkan GPUs |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `WorldRenderEngine`, `WorldSceneSpecification`, `WorldRenderConfiguration`, `WorldRenderOutput`, `TieredWorldRenderSequence`, `VulkanWorldRenderSequence`, `SpecularReflectionSequence`, `ShowcaseWorldSceneBundle`
