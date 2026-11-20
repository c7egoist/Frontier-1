# 🌪️ Fluid Studio (`Experimental/FluidEditor`)

> **TL;DR**: Project-Zero HTML reference studio for the ground-up **Fluid · ForceField** redo (`CLAUDE.md` §1.2). A real-time
> WebGL2 3D Eulerian gas solver (tiled half-float volumes, MacCormack advection, buoyancy, combustion, vorticity confinement,
> Jacobi pressure projection, sphere colliders) with **10 analytic force generators**, GPU flow tracers, velocity-vector and
> scalar heat-map slices, and inspector gizmos — all framed in the exact Project-Zero chrome (trapezoidal dock tabs, top Notch,
> Outliner entity cards, hero / metric / status-tile Inspector, bottom Asset Browser drawer).

---

## 🏷️ 1. Layout & Surfaces

| Surface | Mechanics |
| :--- | :--- |
| **Outliner** (left dock) | `Visible` / `Hidden` counters, search + filter pills, collapsible collections `Domain · Emitters · Force Generators · Colliders · Visualizers`, per-node enable dot + eye toggle, inline `+` constructors. |
| **Viewport** (centre dock) | `Scene · PREVIEW` header, `Edit / Sim / Play` segmented mode, `Add Force` primary pill, camera preset select, five overlay toggles, transport (`Play · Step · Reset` + sim clock), colour-ramp legends, bottom `Asset Browser` trapezoid drawer. |
| **Inspector** (right dock) | Breadcrumb, renameable title, tinted hero banner with type glyph, 2×2 metric cards, status tiles (`Enabled`, `Gizmo`, `MacCormack`, `Pulsed`…), `PROPERTIES` cards with pill sliders + numeric fields, XYZ vector rows, choice chips, `Duplicate / Delete`. |
| **Notch** (`Project-Zero · Fluid Studio`) | Workspace switcher placeholder, `.FluidScene` JSON export / import / reload, shortcut legend. |
| **Status bar** | Realtime fps, solver ms + GPU pass count, grid dims + cell count, active forces/emitters, honest `HTML preview · no engine connection` text. |

---

## 🧩 2. Solver Pipeline (`Engine/FluidSolver.js` + `Engine/FluidShaders.js`)

Fields are 3D grids stored as 2D tile atlases (`RGBA16F`, hardware bilinear + manual slice lerp = trilinear). Per step:

| # | Pass | Kernel |
| :---: | :--- | :--- |
| 1 | Emission | `InjectFragment` — sphere / disc / box masks, noise jitter, pulse envelopes → velocity + `(density, temperature, fuel)` |
| 2 | Advection | `AdvectFragment` (RK2 semi-Lagrangian) or `AdvectFragment ×2 + MacCormackFragment` (error-compensated, monotone-clamped) for velocity and state |
| 3 | Thermodynamics | `ThermodynamicsFragment` — fuel ignition above threshold, heat + soot release, Newtonian + radiative cooling |
| 4 | Forces | `ForcesFragment` — thermal buoyancy, smoke weight, **force generator sum**, collider masking |
| 5 | Vorticity | `CurlFragment` (kept for visualisation) + `VorticityFragment` confinement `ε h (N × ω)` |
| 6 | Projection | `DivergenceFragment` → `PressureFragment` × N Jacobi → `GradientFragment` with closed / open-top / open boundaries and solid Neumann cells |
| 7 | Tracers | `TracerUpdateFragment` — RK2 particle integration, lifetime respawn, previous-frame copy for trails |

### Force generators (`Engine/ForceGeneratorCatalog.js` ↔ `ForceFieldChunk`)

| Code | Type | Field |
| :---: | :--- | :--- |
| 0 | Directional Wind | `axis · strength · (1 + gustDepth · noise(t))`, infinite or spherical reach |
| 1 / 2 | Point Attractor / Repulsor | radial pull / push with soft core |
| 3 | Vortex Column | tangential swirl + inward pull + axial lift about an axis |
| 4 | Curl Noise Turbulence | ∇ × ψ of animated value-noise potential (divergence-free) |
| 5 | Drag Baffle | `−v · strength` inside reach |
| 6 | Radial Burst | periodic outward impulse with pulse width |
| 7 | Thermal Updraft | cylindrical axial lift |
| 8 | Axis Attractor | pull toward an infinite line |
| 9 | Vortex Ring | toroidal circulation around a ring core (launches smoke rings) |

Falloff envelopes: `SmoothStep · Linear · InverseSquare · Infinite`. Up to **12** simultaneous generators, **6** emitters, **4** sphere colliders.

---

## 🔍 3. Flow Visualisation (`Engine/VolumeRenderer.js`)

- **Volume Render** — single-pass ray-march with jittered steps, directional light shadow march, smoke albedo, blackbody fire emission, filmic tone map; analytic ground grid + sphere colliders share the same ray.
- **Velocity Vectors** — GPU-generated arrow lines on an axis-aligned slice (stride, scale, Turbo / Engineering / Plasma / Diverging ramps).
- **Scalar Slice** — heat-map of `Speed · Pressure · Vorticity · Temperature · Density · Divergence · Force |F|` (force magnitude is evaluated live from the generator uniforms).
- **Flow Tracers** — 8 192 GPU particles with speed-coloured points + trails.
- **Force Gizmos** — per-type wireframes (wind arrows, attractor cages, vortex helix, ring tubes…); click to select, drag to move (`Shift` locks height). Gizmos never alter solver state.

---

## 🧪 4. Run & Validate

```bash
python3 Experimental/FluidEditor/Serve.py 8098        # http://0.0.0.0:8098/
node Experimental/FluidEditor/FluidValidation.mjs     # catalog · GLSL (glslangValidator if present) · mock-WebGL2 pass/uniform audit · JSON · DOM · UI smoke
```

Requires WebGL2 with `EXT_color_buffer_float`. Fonts: DM Sans (OFL, bundled in `Fonts/`). No CDN or hot-linked assets.

Shortcuts — `Space` play/pause · `.` step · `R` reset · `F` frame · `Delete` remove · LMB orbit · RMB/MMB pan · wheel dolly.
