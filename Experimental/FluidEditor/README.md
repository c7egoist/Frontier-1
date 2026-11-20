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

Domain resolution follows the Unreal convention: world **Size** stays fixed, **Voxel resolution** sets the cell count on the longest axis (`Draft 32 … Extreme 160` chips), and the per-axis grid + cell size are derived. Open faces (`OpenTop` default) carry an outflow sponge band so plumes exit instead of flattening on the lid; the `Voxel` volume filter shows the raw cells.

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

### Force generators (`Engine/ForceGeneratorCatalog.js` ↔ `ForceFieldChunk`) — the merged ForceField editor

| Category | Types |
| :--- | :--- |
| **Directional** | Directional Wind (gusting) · Wind Shear (heading twists with height) · Flow Stream (steer to target velocity) |
| **Attraction** | Point Attractor · Point Repulsor · Axis Attractor · Orbit (tangential + centripetal) · Containment (soft spring bottle) · Magnetic Dipole (flux-line field) |
| **Rotational** | Vortex Column · Vortex Ring · Rigid Spin |
| **Turbulence** | Curl Noise Turbulence (divergence-free gradient-noise curl) · Brownian Jitter (re-rolled per-cell kicks) |
| **Burst** | Radial Burst · Directional Burst · Shockwave Shell (expanding thin front) · Implosion — all with period, width and start delay |
| **Damping / Thermal** | Drag Baffle · Thermal Updraft |

Falloff envelopes: `Smooth · Linear · Inverse² · Infinite · Gaussian · Plateau · Interpolated (cosine)`. Up to **16** simultaneous generators, **6** emitters, **4** sphere colliders. Each generator exposes strength, reach, falloff, position, axis and up to three type-specific modulation parameters; negative strength inverts the field.

---

## 🔍 3. Flow Visualisation (`Engine/VolumeRenderer.js`)

- **Volume Render** — single-pass ray-march with jittered steps, directional shadow march, Henyey-Greenstein forward scattering, editable **fire incandescence ramp** (ember → flame → core colours, ignition temperature, span, flame opacity, emission), soot lit by its own flame, editable smoke albedo / extinction / ambient / sun / exposure with `Wood · Gas · Fuel · Magic` fire and `Soot · Grey · Steam · Dust` smoke look chips, ACES tone map; analytic ground grid + sphere colliders share the same ray.
- **Velocity Vectors** — GPU-generated arrow lines on an axis-aligned slice (stride, scale, Turbo / Engineering / Plasma / Diverging ramps).
- **Force Vectors** — GPU-evaluated Σ-force arrows on a slice plane or as a sparse 3D lattice, coloured by |F|.
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
