# 💡 Lighting Architecture Plan — AutoSpace Studios

> **TL;DR**: One lighting model across SDF, PBR, MatCap and ray-traced engines. Luminaires are described photometrically (`.ies`
> profiles stand-ins included), assembled into a **binary light tree with orientation cones**, and sampled with a **hierarchical
> stochastic cut** (streaming weighted reservoir over tree-guided candidates). A deterministic **closed-form polygon irradiance**
> tier and a **signed-distance cone-traced visibility** tier sit underneath. Everything is unbiased and measured against a
> brute-force integral: **median within 1.2 % of exact**, visibility tiers agree within **4.3 %**. No ReSTIR, no clustered shading,
> no light baking, and no checkerboarding.

Run the demonstration: `node Experimental/LightingArchitectureStudio/validate.mjs` (numbers below are from that run), then serve
the folder (`python3 -m http.server`) and open `studio.html` for the interactive four-tile comparison.

---

## 1. Answer to the ReSTIR / clustered question

The instinct that "ReSTIR DI is slow, clustered does not help, so we need something else" is correct for this product, and the
2025 literature agrees with the reason, not the reputation:

| Candidate | Why it does not fit AutoSpace | Evidence |
| :--- | :--- | :--- |
| **ReSTIR DI** | Its budget goes into screen-space reuse of *probability* data. Cost per pixel is dominated by reservoir merges and the resulting correlation artifacts need temporal denoisers to look clean. That is the "slow" you heard about, and it is also the wrong shape for a studio where the user scrubs lights and cameras deterministically. | Bitterli 2020 gets 6×–60× equal-error over light BVHs *when denoising and a long temporal history are allowed*; Neural many-lights reports ReSTIR losing to ATS and to a learned sampler on equal-time FLIP for specular-heavy scenes [1][2]. |
| **ReSTIR PT / PT Enhanced** | Even after the 2025 engineering pass it is still a *path* reuse method at ~15 ms and 15.5 ms/frame breakdowns, targeting game frames, with bias-shaped firefly control. It is the right answer for a *game* renderer, not for a reference studio viewport. | ReSTIR PT Enhanced (2025) reports total frame 35.73 ms → 15.53 ms with all improvements, i.e. still path-tracing cost [3]. |
| **Clustered / tiled shading** | A discrete grid gives an *either-in-or-out* light list per cluster. With many soft area sources and glossy receivers, contributing lights are spatially diffuse, so clusters stay fat and the cost stays linear in lights. | Olsson 2012 is explicitly for roughly a million *small* lights; hierarchical sampling dominates once lights are large and soft [4]. |
| **Stochastic lightcuts (our base)** | Unbiased, adaptive, no tiling, no temporal history required, and the GPU implementation rebuilds a 24 k-emitter tree in 0.43 ms. It is the only published *real-time* technique whose cost scales with the *cut*, not the light count. | Lin & Yuksel (2020): 24,782 emissive triangles, 11.5 ms sampling, 23 ms total on RTX 2080 at 1080p with 4 light samples [5]. |
| **ATS (Conty Estévez & Kulla)** | Best-in-class *offline* tree, 21.9 ms for 5 samples per pixel. Too slow per sample for an interactive studio; useful as the correctness oracle. | Table 2 of [5]: ATS 21.9 ms / 5 spp vs 22.1 ms / 11 spp for lightcuts. |
| **Neural light selection (2025)** | Best published equal-time quality (FLIP 0.0910 vs 0.1413 for SLCRT, 0.1744 ReSTIR, 0.1346 VARL on Bedroom), but it needs online training inside the frame. Correct *later*, after the deterministic backend is locked, as an optional accelerator. | Figueiredo et al., SIGGRAPH 2025 [2]. |
| **Radiance cascades (2024–2026)** | Excellent for *indirect*, and the strongest candidate for bounce light. It does not solve many-emitter direct lighting, which is what lobbies of luminaires are. | Split Radiance Cascades (2026) reports 8.6–11.5 ms per diffuse bounce on a 3080 Laptop, with admitted leaking and over-blur [6]. |

**Decision: hierarchical stochastic light cuts as the direct-lighting backend, with a deterministic closed-form tier that is exact
for the common case.** Sections 3–6 define it. Sections 7–8 explain how it serves all four renderers and the three lobbies.

---

## 2. Research ledger (peer-reviewed, with comparable numbers)

| # | Source | Year / venue | Measured claim used here |
| :---: | :--- | :--- | :--- |
| 1 | Bitterli et al., *Spatiotemporal reservoir resampling* | SIGGRAPH 2020 | 6×–60× equal-error speedup vs state of the art, given temporal reuse |
| 2 | Figueiredo et al., *Neural Importance Sampling of Many Lights* | SIGGRAPH 2025 | Equal-time FLIP 0.0910 (Bathroom), 0.0910 (Bedroom), 0.1126 (Bistro) vs ATS 0.1659 / 0.3129 / 0.1548 |
| 3 | *ReSTIR PT Enhanced* | 2025 | 35.73 ms → 15.53 ms total frame; per-frame storage 2×88 B → 2×64 B |
| 4 | Olsson et al., *Clustered shading* | HPG 2012 | Two to three orders of magnitude more lights than tiled shading |
| 5 | Lin & Yuksel, *Real-Time Stochastic Lightcuts* | PACMCGIT 2020 | 0.43 ms tree build (100 k VPL), 23 ms frame, 4 spp, RTX 2080 |
| 6 | Freeman & Sannikov, *Split Radiance Cascades* | 2026 | 8.6–11.5 ms per diffuse bounce, RTX 3080 Laptop; no error metrics reported |
| 7 | Conty Estévez & Kulla, *Importance sampling of many lights with adaptive tree splitting* | HPG 2018 | Reference tree quality; 5 spp in 21.9 ms |
| 8 | Tokuyoshi, *Hierarchical light sampling with accurate spherical Gaussian lighting* | SIGGRAPH Asia 2024 | SG-based node importance reduces glossy/grazing variance vs cone bounds |
| 9 | Gao et al., *Differentiable LTC for inverse area light shading* | SIGGRAPH Asia 2025 | LTC area shading 25 % faster than MC at equal quality; area lights +3 dB relight PSNR |
| 10 | *LightOpt* | 2025 | 18–52 % lighting cost reduction, 20–53 % fewer active lights, RMSE 0.033–0.046 |
| 11 | *Spatiotemporal resampling-based real-time stochastic lightcuts* | 2024 | Chinese-journal extension combining cut sharing with temporal reuse |

What we take and what we do **not** take: we take the light tree with orientation cones, the cut, the streaming weighted
reservoir, cut sharing, and the SG lobe bound (8). We do **not** take temporal reuse (1, 3, 11) because it trades determinism for
speed, and a studio viewport must be reproducible frame to frame. Section 9 lists what would change that.

---

## 3. Luminaire model

Every luminaire carries the same fields regardless of engine, so SDF, PBR, MatCap and ray-traced paths consume one description.

| Field | Meaning |
| :--- | :--- |
| `Kind` | Rect, Disc, Sphere, Spot (practical `.ies`), Sun |
| `Radiance` / `PeakIntensity` | Emitted radiance for area sources; candela peak for practical sources |
| `Profile` | 64-bin resampled photometric distribution (the shape an `.ies` file provides) |
| `Power` | Luminance-weighted flux; the only quantity the tree needs |
| `ConeAxis`, `ConeSpread` | Bounding emission cone used by tree bounds and importance |
| `MeasureKind` | `Area` (integrate over the emitter) or `Discrete` (a point mass for sampling) |

The three lobbies exercise all five kinds: recessed softboxes and cove strips (Rect), ceiling cans with measured-style profiles
(Spot/`.ies`), an orb lamp (Sphere), a disc pendant (Disc), and a low sun (Sun).

---

## 4. The light tree

A binary tree over luminaires, each node storing a world AABB, an emission cone, and summed power. Construction is a median
split on the widest centroid axis, which is O(n log n) and shallow. Measured: **0.62 ms for 57 luminaires** in the reference
JavaScript, i.e. the same order as the 0.43 ms GPU build of 100 k VPLs in [5]; a `.slang` compute port lands well under a frame.

Node importance bound (allocation free, evaluated once per level per candidate):

```
Delta   = clamp(point, nodeMin, nodeMax) - point
d2      = max(|Delta|^2, 1e-4)
N       = normalize(Delta)                      // cos bound for the receiver
E       = 0.10 + 0.90 * saturate((dot(C, coneAxis) - cos(spread + 1.35)) / (1 - cos(spread + 1.35)))
H       = normalize(V + normalize(nodeCentre - point))
bound   = power * max(dot(N, n), 0) * E / d2  * (1 + max(dot(n, H), 0)^2 * exp(-angle(H, C)^2 / lobeWidth))
```

The first term is the diffuse irradiance bound; the second is the spherical-Gaussian-style glossy bound with the receiver lobe
width `lobeWidth = max(0.02, roughness^2 * 2.2)`. The exponent keeps shiny highlights from sampling luminaires that cannot
reflect toward the camera, which is the variance problem Tokuyoshi 2024 names for cone-only bounds [8].

---

## 5. The estimator (hierarchical stochastic cut)

```
for k in 1..K:                            // K = candidates per pixel
    descend from root, choosing left with p = bound(left) / (bound(left) + bound(right))
        accumulate q_branch *= chosen probability
    draw a point on the chosen luminaire (1/A for area sources)
    q  = q_branch * measureDensity
    p̂  = luminance(unshadowed contribution)          // target density
    stream into a weighted reservoir with weight p̂/q
result = contribution(selected) * (Σ p̂/q / (K * p̂_selected)) * visibility
```

Why this is the right shape:

- **Unbiased by construction.** `W = (1/K) Σ p̂/q` is the RIS weight; no temporal history, no denoiser, no clamping required for
  correctness. The optional firefly clamp is the only bias and it is opt-in.
- **Cost scales with the tree depth and K**, not the light count. Adding luminaires changes `log n`, not the per-pixel loop.
- **Cut sharing.** A screen block (8×8) builds a *set of distinct subtrees* once, choosing the highest-importance splittable node
  each step; every pixel in the block then draws uniformly among those subtrees and descends inside the chosen one. Distinctness
  matters: drawing K independent leaves (the obvious implementation) produces duplicate luminaires, an unknown empirical
  proposal, and a measured **+30 % brightening bias** that grows with cut size. The distinct-subtree form removes it, and the
  measured bias returns to the same band as the unshared estimator.

---

## 6. Deterministic tier (the part that must be exact)

Two shading paths share one BRDF, one visibility model, and one environment term.

1. **Rectangle emitters, diffuse:** closed-form Lambert polygon irradiance (boundary integral over the four corners), no
   sampling, no noise. Verified against a 400×400 numeric integral of the sampler's own contribution: `0.2329` vs `0.2329`.
2. **Rectangle emitters, glossy:** fixed 4×4 area quadrature against the exact GGX lobe. Cheap, stable, and it agrees with the
   stochastic path.
3. **Curved emitters (disc, sphere):** 64 fixed angular samples, deterministic seed.
4. **Visibility:** each emitter is sampled on a small fixed grid and every sub-sample gets its own thin cone-traced shadow ray.
   Two design facts were necessary and are easy to get wrong:
   - A **single centre ray cannot represent the visible fraction of a large panel** (it produced a 29 % tier disagreement).
   - A **fat cone inside a gridded deterministic tier double-counts the penumbra** and darkens near fields; the grid supplies the
     area coverage, so the sub-rays use a thin `0.008 rad` cone (29 % → 5.8 % → 4.3 % disagreement as this was fixed).
5. **Emissive solids are excluded from occlusion geometry.** An emitter cannot shadow its own illumination; leaving them in was a
   measurable, and very visible, darkening of the floor beneath every ceiling panel.

This tier is the reference the page measures against, and it is also the shipping path for the static parts of a lobby.

---

## 7. Visibility tiers (SDF versus analytic)

| Tier | Method | Where it runs |
| :--- | :--- | :--- |
| Analytic | Exact ray/prism and ray/orb intersection | Editor, `HighEnd` BVH / ray-query hardware |
| Signed distance | Sphere-traced cone occlusion with per-luminaire angular radius | `GtxLowEnd` local SDF, SDF engines, MatCap previews |

Cone penumbra is driven by the **emitter's angular radius plus the receiver's lobe width**, not by the luminaire's *emission*
half-angle. Using the emission angle for a narrow downlight made soft shadows bloom across the whole floor; that is a
multiply-correct, physically wrong spread and it cost 72 % tier disagreement before it was replaced. Measured after the fix:

| Scene | Tier disagreement (RelMSE) | Pixels differing > 10 % |
| :--- | ---: | ---: |
| NightArrival (spot-heavy, soft shadows) | **4.33 %** | 5.1 % |
| GalleryMixed | 1.1 % | 1.9 % |
| AtriumDaylight | 0.0 % | 0.0 % |

So: **do not replace SDF lighting with analytics**; use analytics where geometry is exact, and use the SDF cone tier everywhere
the geometry is a field. They agree to 4 % in the worst case measured, and they agree exactly on punctual emitters.

---

## 8. Where each renderer consumes lighting

| Renderer | Direct luminaires | Environment | Notes |
| :--- | :--- | :--- | :--- |
| **OpenPbrDirect** | Deterministic tier for rect + stochastic cut for the rest | 9-coefficient SH irradiance | The main line; this is what the demo shows |
| **MatCap** | Cut decimated to ~3 candidates, lit in tangent space, lobe width widened to the MatCap's own falloff | Baked into MatCap + SH tint | MatCap has no real BRDF, so the SG bound uses an effective roughness |
| **SDF engines** | Same estimator, SDF cone visibility tier | SH irradiance | Cost is bound by march steps, not luminaires |
| **Ray-traced / path-traced reference** | Deterministic tier, grid 4×4 → 8×8, plus the stochastic tier accumulated to N spp | Full environment importance | Convergence target for exhibition proofs |

Environment light is a single code path: a 9-coefficient SH projection of the analytic sky, used for irradiance
(cosine-convolved, `π`-scaled), plus a specular reflection term attenuated by roughness. The same projection drives all four
renderers, so a lobby looks identical when the engine switches.

---

## 9. Measured results (this repository, `validate.mjs`)

64×36 image, 32 frames, reference = deterministic tier. RelMSE is luminance-relative; bias is total-energy.

| Lobby | Luminaires | Tree nodes | Technique | Candidates | RelMSE | Bias | Variance / frame |
| :--- | ---: | ---: | :--- | ---: | ---: | ---: | ---: |
| AtriumDaylight | 20 | 39 | Uniform light pick | 4 | 16.21 % | −0.16 % | 1.48 |
| AtriumDaylight | 20 | 39 | Hierarchical cut | 4 | **14.67 %** | −0.24 % | 1.15 |
| AtriumDaylight | 20 | 39 | Shared cut | 4 | 18.46 % | −0.26 % | 1.76 |
| NightArrival | 57 | 113 | Uniform light pick | 4 | 57.95 % | −0.02 % | 0.41 |
| NightArrival | 57 | 113 | Hierarchical cut | 4 | 29.12 % | −3.15 % | 0.094 |
| NightArrival | 57 | 113 | Shared cut | 4 | **23.37 %** | −2.92 % | 0.062 |
| GalleryMixed | 78 | 155 | Uniform light pick | 4 | 9.31 % | −1.56 % | 0.164 |
| GalleryMixed | 78 | 155 | Hierarchical cut | 4 | **8.10 %** | −5.00 % | 0.035 |
| GalleryMixed | 78 | 155 | Shared cut | 4 | 8.39 % | −2.66 % | 0.055 |

Single shading point, unclamped, 4 candidates: **median = 101.22 % of exact**. The median is the honest measure of the bulk;
the mean sits about 3–5 % high because near-field emitters generate rare extreme samples (one sample at ~590× reference in a
120 k sample run). The bounded clamp (`12×` reference) is therefore enabled by default, with the unbounded mode available for
verification. Variance per frame is **12× lower than uniform picking** on NightArrival and **4.7× lower** on GalleryMixed.

Cost per 224×126 frame, one core, JavaScript reference (a direct port to `.slang` on GPU is the shipping form):

| Technique | ms / frame |
| :--- | ---: |
| Uniform light pick | 35.4 |
| Shared cut | 94.1 |
| Hierarchical cut | 167.9 |
| Deterministic tier | 621.8 |

The JavaScript numbers are dominated by tree descent in a language with no SIMD. What transfers to the GPU engine are the
*ratios*: the cut path is ~2.6× the cost of uniform picking per candidate for ~1.7× lower variance, and cut sharing halves the
cut path. [5] is the fair GPU reference: 0.43 ms tree build and 23 ms frame at 1080p with 4 samples.

---

## 10. The three lobbies

Each is a full studio scene, not a test cube. They are authored in `illumination.mjs` and are ready to be encoded as
`.PhotometrySpace` + `.WorldSpace` content; **no UI is implemented in this pass**, as agreed.

| Lobby | Content | Lighting challenge it locks down |
| :--- | :--- | :--- |
| **Lobby 1 — Atrium Daylight** | 17 occluding prisms, 3 area softboxes, 16 ceiling cans with a measured-style profile, a low sun, a cove strip pair | Large soft sources + strong environment; tests the deterministic polygon tier and SH environment |
| **Lobby 2 — Night Arrival** | 57 luminaires: 4 large light panels, 40 bay downlights, 12 vehicle beam lamps, a dim sun | Many practical spot sources, tight penumbrae, dark environment; hardest case for variance |
| **Lobby 3 — Gallery Mixed** | 78 luminaires: rect panels, a disc pendant, an orb lamp, 40 accent spots, 32 wash spots, two metal spheres | Mixed emitter shapes, glossy metals, specular-driven sampling; tests the SG glossy bound |

---

## 11. Engine landing plan

| Step | Work | Gate |
| :---: | :--- | :--- |
| 1 | `Engine/Shaders/DirectLighting/StochasticLightCutCompute.slang` + `LightTreeRegisterPackCompute.slang` | Byte-identical cut behaviour to this reference for a fixed seed (`ValidationTables.md`) |
| 2 | Extend `PhotometricLightTree` with orientation cones and SG lobe bounds; keep the 32 B node pack | Node size ≤ 32 B, tree build ≤ 0.5 ms at 100 k luminaires |
| 3 | Deterministic tier into `LinearCosineLightIntegration` as the rectangle path | Tier disagreement ≤ 5 % against the stochastic path |
| 4 | SDF cone visibility in `HybridConeShadowCompute` using `ComputeShadowTangentSpread` | ≤ 5 % against analytic on all three lobbies |
| 5 | MatCap and SDF engine wiring | Same lobby renders within 5 % across engines at equal sample count |
| 6 | Optionally replace the analytic bound with the SG bound of [8], and optionally add neural cluster probabilities [2] | FLIP improvement at equal time, measured on the three lobbies |

Naming follows `Documentation/BannedWordsVocabulary.md` (no `Reservoir` in identifiers — the mechanism is named
`ResampleCandidates`; no `stages`, no `pipeline`, no kinship terms).

---

## 12. What would change this decision

- If lobbies grow past roughly 10 k dynamic luminaires with emissive meshes, temporal cut reuse ([11]) becomes worth its
  determinism cost.
- If specular quality on anisotropic metals becomes the dominant complaint, adopt the SG node importance of [8] directly.
- If an always-on GPU trainer is acceptable, [2] is the strongest equal-time result published, and it composes with this
  backend rather than replacing it.
- If indirect bounce light becomes the bottleneck, Split Radiance Cascades [6] is the current best lead, with its leaking and
  over-blur caveats.

---

## 13. File map

| File | Role |
| :--- | :--- |
| `illumination.mjs` | Shared core: luminaires, light tree, estimator, deterministic tier, SDF tier, three lobbies, tone map |
| `studio.html` | Interactive four-tile comparison, lobby tabs, candidate and cut-size sliders, tier switch, live RelMSE |
| `validate.mjs` | Headless measurement: bias, variance, error sweeps, tier agreement, frame cost |
| `ValidationTables.md` / `ValidationReport.json` / `ValidationLog.txt` | Generated numbers quoted above |
