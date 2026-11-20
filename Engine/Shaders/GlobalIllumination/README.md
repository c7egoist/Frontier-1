# 🧩 `Engine/Shaders/GlobalIllumination/`

> **TL;DR**: Zero-SDF GTX Global Illumination `.slang` Compute Shaders for **GTX GI #1 (`ContourSurfelIllumination.slang`)** and **GTX GI #2 (`DynamicIrradianceCache.slang` + `WeightedPathReplay.slang` + `WeightedTemporalFilter.slang`)**.

---

## Files

| File | Purpose |
| :--- | :--- |
| `ContourSurfelIllumination.slang` | GTX GI #1: Exact Clipped Spherical-Polygon Lambert/Stokes Emissive Contours + 2-Bounce Bunnell Surfels + Anti-Banded `SH9` (Denoiser-Free) |
| `DynamicIrradianceCache.slang` | GTX GI #2 Part A: 64 Spherical Fibonacci BVH4 Rays -> 8×8 Octahedral `1/pi` Irradiance + 16×16 Chebyshev Moments |
| `WeightedPathReplay.slang` | GTX GI #2 Part B: 4-Candidate Owen-Sobol RIS + 1-Bounce BVH4 Replay + Octahedral Multi-Bounce Tail |
| `WeightedTemporalFilter.slang` | GTX GI #2 Part C: Visibility-Gated Temporal/Spatial Sample Reuse + Variance-Guided Joint A-Trous Filter |
