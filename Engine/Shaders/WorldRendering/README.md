# 🧩 `Engine/Shaders/WorldRendering/`

> **TL;DR**: Unified Zero-SDF 5-Engine World Viewport `.slang` Shaders (`WorldViewportRadiometricCompute.slang`, `SpecularGlassRaytrace.slang`, and `WorldViewportBlit.slang`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `WorldViewportRadiometricCompute.slang` | 30-Buffer Zero-SDF 5-Engine GPU Compute (`MatCap`, `OpenPbrDirect`, `ContourSurfelGi`, `DynamicIrradianceGi`, `DeterministicPathTrace`), 2-Cascade Shadows, Atmosphere & Fluid |
| `SpecularGlassRaytrace.slang` | Coherent GGX VNDF Specular Reflection & 3-Channel Abbe-Dispersed Dielectric Glass Refraction Compute Kernel |
| `WorldViewportBlit.slang` | Fullscreen Triangle Viewport Blit & Linear-to-sRGB Presentation Shader |
