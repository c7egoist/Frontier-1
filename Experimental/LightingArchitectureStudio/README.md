# 🧩 `Experimental/LightingArchitectureStudio/`

> **TL;DR**: Reference implementation and measured demonstration for the AutoSpace lighting architecture: a binary photometric
> light tree with orientation cones, a hierarchical stochastic cut estimator, a deterministic closed-form polygon irradiance
> tier, and a signed-distance cone visibility tier. Three lobby scenes are authored here. No UI for the lobbies is implemented.

---

## Files

| File | Purpose |
| :--- | :--- |
| `LightingArchitecturePlan.md` | Research ledger, technique decisions, measured results, engine landing plan |
| `illumination.mjs` | Shared illumination core: luminaire model, light tree, estimator, deterministic tier, SDF tier, lobby scenes |
| `studio.html` | Four-tile interactive comparison (uniform pick, hierarchical cut, shared cut, deterministic) with live RelMSE |
| `validate.mjs` | Headless validation: bias, variance, error sweeps against the exact tier, tier agreement, frame cost |
| `ValidationTables.md` | Generated measurement tables quoted by the plan |
| `ValidationReport.json` | Generated machine-readable measurements |
| `ValidationLog.txt` | Generated console transcript of the validation run |

## Run

```bash
node Experimental/LightingArchitectureStudio/validate.mjs      # writes the tables above
python3 -m http.server 8123 --bind 0.0.0.0                     # then open studio.html
```

## Measured headline

- Median single-point estimate **101.2 % of exact** at 4 candidates, unclamped.
- Variance per frame **12× lower than uniform picking** on the 57-luminaire night lobby.
- Signed-distance and analytic visibility tiers agree within **4.33 %** on the hardest scene.
- Light tree build **0.62 ms** for 57 luminaires in JavaScript.

Full tables, research citations, and the engine landing plan live in
[LightingArchitecturePlan.md](LightingArchitecturePlan.md).
