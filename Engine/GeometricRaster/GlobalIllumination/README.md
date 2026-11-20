# 🧩 `Engine/GeometricRaster/GlobalIllumination/`

> **TL;DR**: Dual Zero-SDF GTX Global Illumination Renderers — **#1 `ContourSurfelIlluminationSequence`** (Analytic Lambert/Stokes Emissive Contours + 2-Bounce Bunnell Surfels + Anti-Banded Order-2 `SH9`, Denoiser-Free) and **#2 `DynamicIrradianceCacheSequence` + `WeightedPathReplaySequence`** (8×8 Octahedral Cache + 16×16 Chebyshev + 4-Candidate Owen-Sobol RIS).

---

## Files

| File | Purpose |
| :--- | :--- |
| `DeterministicIntegrators.h` | Shared Bit-Exact Owen-Scrambled Sobol (0,2) Quadrature & Dupuy-Benyoub (2023) Spherical-Cap GGX VNDF |
| `ContourSurfelIlluminationSequence.h` | GTX GI #1: Exact Clipped Spherical-Polygon Stokes Contours, 64 B `RadiantBounceSurfel` 2-Bounce Radiosity & Order-2 `SH9` Field |
| `ContourSurfelIlluminationSequence.cpp` | Parallel CPU/GPU-parity implementation of Stokes contour form factors, 2-bounce Bunnell surfel relaxation, and `SH9` evaluation |
| `DynamicIrradianceCacheSequence.h` | GTX GI #2 Part A: 8×8 Octahedral `1/pi` Irradiance Cache + 16×16 Chebyshev Radial Moments with Backface Validity Gating |
| `DynamicIrradianceCacheSequence.cpp` | 64-ray Spherical Fibonacci `WideBoundingTree` BVH4 irradiance lattice builder and Chebyshev trilinear lookup |
| `IrradianceLatticeSequence.h` | Zero-SDF Octahedral Irradiance Lattice facade wrapping `DynamicIrradianceCacheSequence` |
| `IrradianceLatticeSequence.cpp` | Implementation of `IrradianceLatticeSequence` over `WideBoundingTree` (Zero 3D SDF) |
| `WeightedPathReplaySequence.h` | GTX GI #2 Part B: 4-Candidate Owen-Sobol RIS + Temporal/Spatial Reuse + Variance-Guided Joint A-Trous Filter |
| `WeightedPathReplaySequence.cpp` | Implementation of Weighted Path Replay coupling 1-bounce `WideBoundingTree` rays with `DynamicIrradianceCacheSequence` |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `EmissiveContourTriangle`, `RadiantBounceSurfel`, `ContourSurfelConfiguration`, `ContourSurfelIlluminationSequence`, `DynamicIrradianceCacheSequence`, `IrradianceLatticeSequence`, `WeightedPathReplaySequence`
