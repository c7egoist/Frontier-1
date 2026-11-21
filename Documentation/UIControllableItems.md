# UI-Controllable Items — Complete Inventory

Everything the Frontier editor can control today, plus the full backend parameter space that is
available to expose. Use this to adjust your UI prototype before handing it over.

Legend: **[UI]** = wired to a live control in the current editor UI · **[Backend]** = parameter exists
in the engine and takes effect when changed, but has no dedicated control yet (prototype candidate).

---

## 1. Viewport (toolbar, header & footer)

Source: `ViewportDisplaySettings` (`Engine/Components/Panes/ViewportPanel.h`).

| Item | Type | Values / Range | Status |
|---|---|---|---|
| Render Engine | cycle | MatCap · OpenPBR Direct · GTX SDF GI · HighEnd BVH4 · Game Realtime (PT) · Scene Final (PT) | [UI] |
| Quality Tier | segmented | Low · Med · Std · High · Ultra | [UI] |
| Debug Channel | cycle | Off · Depth · Albedo · Shading Nrm · Roughness · Metalness · Cluster ID · Geom Nrm · Sun Shadow · Ambient Occ · Direct Lit · Specular | [UI] |
| Shadow Algorithm | cycle | Contact PCSS · PCSS Plane-Aware · Shadow Ray March · PCF Bilinear · WideTree RayQuery | [UI] |
| SDF Shadow / Penumbra Mode | cycle | 0 Point SDF · 1 Analytic Cone · 2 Voxel Clipmap · 3 Voxel+Cone GI · 4 Hybrid Sobol · 5 MC PT Ref | [UI] |
| Reflection Source | cycle | WideTree Raytrace · Sky Environment | [UI] |
| Trace Resolution | cycle | Full · ½ · ¼ | [UI] |
| Render Scale | slider | 50–200 % | [UI] |
| Reflections Enabled | toggle | on/off | [UI] |
| Anti-Aliasing Enabled | toggle | on/off | [UI] |
| Raytracing Enabled | toggle | on/off | [UI] |
| Atmosphere Sky Enabled | toggle | on/off | [UI] |
| Volumetric Clouds Enabled | toggle | on/off | [UI] |
| Cloud Shadows Enabled | toggle | on/off | [UI] |
| Celestial Stars Enabled | toggle | on/off | [UI] |
| Day Cycle Playing | toggle | play/pause | [UI] |
| Solar Time of Day | slider/step | 0–24 h | [UI] |
| Cloud Cover (Fraction) | slider/step | 0–100 % | [UI] |
| Cloud Wind Speed | slider | m/s | [UI] |
| Camera Fly Speed | mouse wheel | ×0.10 – ×16.0 (footer readout `Fly Speed x.xx`) | [UI] |
| Camera movement | keys | WASD/QE fly, Shift = sprint ×3, Alt+RMB orbit-drag, F = frame selected | [UI] |

## 2. Outliner — World folder rows

Rows: **Sun · Moon · Stars · Volumetric Clouds · Local Cloud** (plus **Ceiling Softbox** under
Lighting when the scene has area lights, and every scene object). Source:
`RegisterShowcaseGalleryInOutliner` / `SynchronizeShowcaseWorldFromOutliner`.

| Row | Eye toggle drives | Shadow toggle drives |
|---|---|---|
| Sun | `Solar.SolarEnabled` | sun cast-shadows |
| Moon | `Moons[0].Visible` (slot 0) | — |
| Stars | `Stars.StarfieldEnabled` | — |
| Volumetric Clouds | `Clouds.CloudsEnabled` | `Clouds.CloudShadow.ShadowEnabled` |
| Local Cloud | `LocalCloud.LocalEnabled` | — |
| Ceiling Softbox (area light) | area-light visibility | — |

## 3. Inspector — generic (every Outliner entry)

Source: `InspectorPanel`.

- **Enabled** (visibility) and **Cast shadows** capability tiles [UI]
- **Transform**: Position (m), Rotation (deg), Scale (×) — numeric fields with per-row reset
  buttons and precision hint [UI]
- Identity readout: title, category label, subtitle, author notes, position summary [UI]

## 4. Inspector — Editor Camera custom cards

Source: `EditorLifecycle` camera Inspector section (mirrors the `Inspectors.jsx` prototype cards).

**Card 1 · Render Settings** [UI]: Engine (cycle, 6) · SDF Shadow (cycle, 6) · Quality
(Low/Med/Std/High/Ultra segmented) · Raytracing (ON/OFF).

**Card 2 · Sky, Sun, Moon & Clouds** [UI]: Solar Time (steps +2 h, wraps 22→6) · Cloud Cover
(steps +20 %, wraps 80→0, also toggles Volumetric Clouds).

## 5. Sun / Solar ephemeris (backend space)

Source: `SolarCelestialConfiguration` (`CelestialEphemerisSolver.h`).

| Parameter | Range / Options | Status |
|---|---|---|
| Solar Enabled | on/off | [UI] via Outliner |
| Local Hours (time of day) | 0–24 h | [UI] |
| Cycle Animation Enabled + speed preset (×1/×8/×30/×100/Custom) + Day Duration | 0.01–168 h | [Backend] (play toggle is [UI]) |
| Angular Diameter | 0.1–5.0° | [Backend] |
| Tint Source (RGB tint / Kelvin) + Manual RGB Tint | — | [Backend] |
| Color Temperature | 1000–40000 K | [Backend] |
| Solar Intensity Multiplier | 0–60× | [Backend] |
| Direct Gain Multiplier | 0–5× | [Backend] |
| Site Latitude / Longitude | −90–90° / −180–180° (default Johannesburg) | [Backend] |
| Date (Day / Month / Year) + UTC Offset | — | [Backend] |
| Manual Direction Override + Azimuth / Elevation | 0–360° / −90–90° | [Backend] |
| Appearance Mode (Muted / Radiant) + Sun Glare Strength | 0–2 | [Backend] |

## 6. Moon — 6 slots (backend space)

Source: `LunarBodyConfiguration` (`LunarStarfieldSequence.h`). Outliner eye controls slot 0 only.

| Parameter | Range / Options |
|---|---|
| Visible | on/off |
| Follow Sky Opposite Sun | on/off |
| Surface Preset | Luna · Ember · Glacier · Sulfur · Shroud · Shard |
| Synodic Phase | 0..1 (0/1 = new, 0.5 = full) |
| Brightness | 0..6 x |
| Halo Glow | 0..3 x |
| Angular Diameter | 0.1..180 deg |
| Azimuth / Elevation | 0..360 deg / -90..90 deg |
| Surface Roll / Pitch | 0..360 deg / -180..180 deg |
| Has Atmosphere | on/off |
| Atmosphere Intensity / Thickness / Tint | 0..3 x / relative radius / LinearRGB |

## 7. Stars (backend space)

Source: `StarfieldConfiguration` (`LunarStarfieldSequence.h`).

| Parameter | Range / Options |
|---|---|
| Starfield Enabled | on/off (also Outliner eye) |
| Scintillation Enabled / Depth / Rate | on/off / 0..100 % / 0.2..3.0 Hz |
| Limiting Magnitude | 0..8 mag |
| Brightness / Point Size Scale | 0..4 x / 0.4..3.0 x |
| Celestial Rotation | 0..360 deg |
| Bright Star Catalog / Galactic Band / Zodiacal Light | on/off each |
| Zodiacal Radiance Scale | 0..4 x |
| Preset | Galactic Bulge · Zodiacal Light Dusk · Southern Cross Coalsack · Winter Hunter Betelgeuse · Magellanic Clouds |

## 8. Atmosphere & sky (backend space)

Source: `AtmosphericMediumConfiguration` (`AtmosphericSkySequence.h`).

| Parameter | Range / Options | Status |
|---|---|---|
| Atmosphere Enabled | on/off | [UI] via Viewport toggle |
| Rayleigh Strength | 0..4 x | [Backend] |
| Mie Strength / Anisotropy | 0..6 x / 0..0.99 | [Backend] |
| Ozone Strength | 0..4 x | [Backend] |
| Rayleigh / Mie Scale Height | m | [Backend] |
| Atmosphere Thickness / Planet Radius | m | [Backend] |
| Limb Glow / Sky Brightness | 0..3 x / 0..3 x | [Backend] |
| Sky Tint / Ground Albedo | LinearRGB | [Backend] |
| Orbital View / Observer Altitude | on/off / m | [Backend] |

## 9. Clouds (backend space)

Source: `CloudDeckConfiguration`, `BoundedCloudVolume`, `CloudShadowConfiguration`, `LensFlareConfiguration`
(`VolumetricCloudSequence.h`).

| Parameter | Range / Options | Status |
|---|---|---|
| Clouds Enabled / Follow Wind | on/off | [UI] toggle / [Backend] |
| Cloud Type | Stratus · Stratocumulus · Cumulus · Cumulonimbus · Altostratus · Cirrus · Lenticular · Mammatus · KelvinHelmholtz · Asperitas · Nacreous · Pyrocumulus | [Backend] |
| Cloud Fraction | 0..1 | [UI] via Cloud Cover |
| Extinction / Feature Scale | 0..4 x / 0.2..3.0 x | [Backend] |
| Cloud Floor / Layer Thickness / Deck Ceiling | 100..15000 m / 100..6500 m / 4000..22000 m | [Backend] |
| Anvil Top Spread / Forward Scatter g | 0..1 / -0.9..0.9 | [Backend] |
| Cloud March Steps | 16..128 | [Backend] |
| Cloud Render Scale | 0.25..1 | [Backend] |
| Wind Speed / Bearing / Shear / Veer / Turbulence | 0..40 m/s / 0..360 deg / per-km / d/km / 0..1 | [UI] speed / [Backend] rest |
| Cloud Shadow: Enabled / Category / Fraction / Density / Scale / Anvil / Floor / Thickness | on/off + per-shadow values | [UI] enabled / [Backend] rest |
| Local Cloud: Enabled / Shape / Prism Sides / Center / Half Extent / Yaw / Edge Softness / Extinction / Fraction / Feature Scale / Scatter | 8 GPU volumes maximum | [UI] slot 0 eye / [Backend] rest |
| Lens Flare: Enabled / Style / Intensity / Spread / Halo / Ghost / Spacing / Count / Sides / Streak Rotation | Anamorphic · Streaks · Halos | [Backend] |

## 10. Wind & fog (backend space)

Sources: `AtmosphericWindParameters` (`AtmosphericWindSolver.h`), `AtmosphericFogConfiguration` (`VolumetricFogSequence.h`).

- **Wind**: Enabled · Speed (0..40 m/s) · Bearing (0..360 deg) · Shear (/km) · Veer (-60..60 d/km) · Air Shear Advection · Gust Strength / Frequency · Turbulence · Steadiness.
- **Height Fog**: Enabled · Density (0..0.2 /m) · Falloff Height (10..3000 m) · Base Altitude · Sun Scatter (0..2 x) · Height Colour · Follow Wind.
- **Aerial Fog**: Enabled · Density Multiplier (0..4 x) · Start Distance (0..2000 m) · Mie Blend · Scale Height · Follow Wind.
- **Local Fog Volumes**: Enabled · Follow Wind · Shape · Prism Sides · Center · Half Extent · Yaw · Edge Softness · Density · Coverage · Feature Scale · Anisotropy · Tint.
- **God Rays**: Enabled · Intensity (0..4 x) · Anisotropy · Decay · Density · Max Distance · Sample Steps · Cloud / Fog Interaction · Shaft Tint.

## 11. Camera & gizmo (editor)

| Item | Control | Status |
|---|---|---|
| RMB Look + WASD / Q / E fly | mouse + keys | [UI] |
| Shift Sprint (x3) / Ctrl precision (x0.3) | keys | [UI] |
| Mouse wheel Fly Speed | 0.10..16.0 x | [UI] |
| MMB pan, Alt+LMB orbit, Alt+RMB dolly, Alt+MMB pan | mouse modifiers | [UI] |
| LMB pick / gizmo drag (no camera movement) | mouse | [UI] |
| 1 Translate / 2 Rotate / 3 Scale / 4 Universal gizmo, F frame selection | keys | [UI] |
| Camera Lag Enabled / Translation Lag / Rotation Lag / Teleport Distance | reusable `CameraLagComponent` | [Backend] via `EditorLifecycle::ConfigureEditorCameraLag` |
| Camera FOV, near/far, sensor gate, focal length | `CameraConfiguration` | [Backend] |
| Thin-lens DOF, motion blur, vignette, chromatic aberration, lens flare | `CameraConfiguration` GPU effects | [Backend] |

## 12. Rendering, lighting & debug spaces (backend space)

- **Solar shadows**: algorithm (Contact PCSS · PCSS Plane-Aware · Shadow Ray March · PCF Bilinear · WideTree RayQuery) [UI]; cascade count, penumbra filter, bias, Vogel sample count [Backend].
- **SDF / GDF**: GDF clipmap resolution / voxel size / fine center-span, bake cadence, shadow cone aperture, SDF penumbra mode [UI mode / Backend parameters]. See `Documentation/Plans/SdfDebugAndAoAssessment.md` for the AO assessment.
- **AO**: current GDF 4-tap AO and 3-ray BVH fallback [Backend]; XGTAO radius / bias / sample count / denoiser are proposed, not implemented yet.
- **Direct lighting**: sun intensity / tint, area lights (4 GPU slots), photometric IES lamps, specular AO, reflection source [UI source / Backend rest].
- **Materials**: albedo, roughness, metalness, normal, emissive, transmission, clearcoat, LTC area response — see `MaterialSpace` and the Albedo / Roughness / Metalness debug channels [UI channels / Backend values].
- **Debug channels (12)**: Off · Depth · Albedo · Shading Nrm · Roughness · Metalness · Cluster ID · Geom Nrm · Sun Shadow · Ambient Occ · Direct Lit · Specular [UI].
