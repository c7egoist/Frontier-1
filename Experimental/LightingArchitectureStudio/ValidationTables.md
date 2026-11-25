Image 64x36, 32 frames per measurement, reference = deterministic tier.

| Lobby | Luminaires | Area | Tree nodes | Technique | Candidates | Error | Bias | Variance / frame | ms / frame |
| :--- | ---: | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: |
| AtriumDaylight | 20 | 3 | 39 | DeterministicPolygon | 0 | 0.00% | 0.00% | 0.00e+0 | 28.5 |
| AtriumDaylight | 20 | 3 | 39 | UniformLightPick | 4 | 16.21% | -0.16% | 1.48e+0 | 2.4 |
| AtriumDaylight | 20 | 3 | 39 | LightTreeCut | 4 | 14.67% | -0.24% | 1.15e+0 | 7.3 |
| AtriumDaylight | 20 | 3 | 39 | SharedLightTreeCut | 4 | 18.46% | -0.26% | 1.76e+0 | 4.0 |
| NightArrival | 57 | 4 | 113 | DeterministicPolygon | 0 | 0.00% | 0.00% | 0.00e+0 | 48.6 |
| NightArrival | 57 | 4 | 113 | UniformLightPick | 4 | 57.95% | -0.02% | 4.10e-1 | 2.3 |
| NightArrival | 57 | 4 | 113 | LightTreeCut | 4 | 29.12% | -3.15% | 9.43e-2 | 9.8 |
| NightArrival | 57 | 4 | 113 | SharedLightTreeCut | 4 | 23.37% | -2.92% | 6.24e-2 | 6.9 |
| GalleryMixed | 78 | 5 | 155 | DeterministicPolygon | 0 | 0.00% | 0.00% | 0.00e+0 | 97.7 |
| GalleryMixed | 78 | 5 | 155 | UniformLightPick | 4 | 9.31% | -1.56% | 1.64e-1 | 3.1 |
| GalleryMixed | 78 | 5 | 155 | LightTreeCut | 4 | 8.10% | -5.00% | 3.50e-2 | 12.6 |
| GalleryMixed | 78 | 5 | 155 | SharedLightTreeCut | 4 | 8.39% | -2.66% | 5.52e-2 | 8.0 |

| Technique | Candidates | Mean / reference | Relative error | Standard error | Relative standard deviation | Frames for 5% error |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| UniformLightPick | 1 | 105.08% | 5.08% | 4.25% | 255.8% | 2618 |
| UniformLightPick | 4 | 109.67% | 9.67% | 2.80% | 161.7% | 1046 |
| UniformLightPick | 16 | 104.20% | 4.20% | 1.73% | 104.8% | 440 |
| UniformLightPick | 64 | 104.97% | 4.97% | 1.60% | 96.3% | 371 |
| LightTreeCut | 1 | 102.85% | 2.85% | 0.67% | 41.3% | 69 |
| LightTreeCut | 4 | 103.46% | 3.46% | 0.34% | 20.6% | 17 |
| LightTreeCut | 16 | 104.82% | 4.82% | 1.46% | 88.0% | 310 |
| LightTreeCut | 64 | 104.96% | 4.96% | 0.81% | 49.0% | 97 |
| SharedLightTreeCut | 1 | 103.49% | 3.49% | 1.32% | 80.8% | 261 |
| SharedLightTreeCut | 4 | 105.28% | 5.28% | 1.12% | 67.5% | 183 |
| SharedLightTreeCut | 16 | 104.60% | 4.60% | 1.02% | 61.8% | 153 |
| SharedLightTreeCut | 64 | 104.75% | 4.75% | 1.00% | 60.6% | 148 |
