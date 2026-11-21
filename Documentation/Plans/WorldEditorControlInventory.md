# World Editor Control Inventory

📝 Source-of-truth inventory for the UI rewrite. Every row is a real backing field in `Engine/`; `🟢` means a control is already present in the current editor and `🚧` means the value exists in the model/codec but has no dedicated inspector control yet.

Legend: `🟢` live control · `🚧` model field awaiting UI · `[x]` multiplier · `·` grouped with the row above.

---

## 1. Controls already in the editor (exact current labels)

| Panel | UI label | Backing field | Type / range |
| :--- | :--- | :--- | :--- |
| Left inspector · Render Settings | Engine | `ViewportDisplaySettings::RenderEngineIndex` | enum 0..5 · MatCap, OpenPBR Direct, GTX SDF GI, HighEnd BVH4, Game Realtime, Scene Final |
| Left inspector · Render Settings | SDF Shadow | `SdfShadowPenumbraIndex` | enum 0..5 · Point SDF, Analytic Cone, Voxel Clipmap, Voxel + Cone GI, Hybrid Sobol, MC PT Ref |
| Left inspector · Render Settings | Quality | `QualityTier` | enum 0..4 · Low, Med, Std, High, Ultra |
| Left inspector · Render Settings | Raytracing | `RaytracingEnabled` | bool |
| Left inspector · Sky, Sun, Moon & Clouds | Solar Time | `SolarTimeOfDayHours` | hours 0..24, click steps +2 h |
| Left inspector · Sky, Sun, Moon & Clouds | Cloud Cover | `CloudFraction` / `VolumetricCloudsEnabled` | 0..1, click steps +0.20 |
| Viewport render sheet | Render Scale | `RenderScalePercent` | % 50..200 |
| Viewport render sheet | Shadow | `ShadowAlgorithmIndex` | enum 0..4 · Contact PCSS, PCSS + Filter, SMRT, PCF 3x3, BVH4 Ray |
| Viewport render sheet | SDF Penumbra | `SdfShadowPenumbraIndex` | enum 0..5 |
| Viewport render sheet | Debug View | `DebugChannelIndex` | enum 0..11 (extended channels listed in §11) |
| Viewport render sheet | Reflections | `ReflectionsEnabled` | bool |
| Viewport render sheet | Anti-aliasing | `AntiAliasingEnabled` | bool |
| Viewport render sheet | Raytracing | `RaytracingEnabled` | bool |
| Viewport render sheet | BVH4 Specular Trace | `ReflectionSourceIndex` | bool switch · BVH4 raytrace vs sky environment |
| Viewport render sheet · ATMOSPHERE & CLOUDS | Atmosphere | `AtmosphereSkyEnabled` | bool |
| Viewport render sheet · ATMOSPHERE & CLOUDS | Clouds | `VolumetricCloudsEnabled` | bool |
| Viewport render sheet · ATMOSPHERE & CLOUDS | Cloud Shadows | `CloudShadowsEnabled` | bool |
| Viewport render sheet · ATMOSPHERE & CLOUDS | Sun, Moons & Stars | `CelestialStarsEnabled` | bool |
| Viewport render sheet · ATMOSPHERE & CLOUDS | Day Cycle | `DayCyclePlaying` | bool |
| Viewport render sheet · ATMOSPHERE & CLOUDS | Solar Time | `SolarTimeOfDayHours` | hours 0..24 |
| Viewport render sheet · ATMOSPHERE & CLOUDS | Cloud Cover | `CloudFraction` | 0..1 |
| Viewport render sheet · ATMOSPHERE & CLOUDS | Wind Speed | `CloudWindSpeedMps` | m/s 0..40 |
| Viewport footer | Fly Speed | `CameraFlySpeedScalar` | 0.10x..16.00x; wheel adjusts, Shift = Sprint x3 |
| Project Browser | Open in Frontier | `HostWorkspaceTarget::FrontierWorld` | sole launch action |

---

## 2. Sun inspector · `SolarCelestialConfiguration`

| Proposed control | Backing field | Type / range | Status |
| :--- | :--- | :--- | :--- |
| Sun Enabled | `SolarEnabled` | bool | 🚧 |
| Sun Diameter | `AngularDiameterDegrees` | degrees 0.1..5.0 | 🚧 |
| Tint Source | `TintSource` | enum · RGB Tint / Kelvin Temperature | 🚧 |
| Manual RGB Tint | `ManualRgbTint` | linear RGB | 🚧 |
| Colour Temperature | `ColorTemperatureKelvin` | K 1000..40000 | 🚧 |
| Solar Intensity | `SolarIntensityMultiplier` | x 0..60 | 🚧 |
| Direct Gain | `DirectGainMultiplier` | x 0..5 | 🚧 |
| Solar Time | `LocalHours` | hours 0..24 | 🟢 |
| Day Cycle | `CycleAnimationEnabled` | bool | 🟢 |
| Cycle Speed Preset | `CycleSpeedPreset` | enum · 1x / 8x / 30x / 100x / Custom | 🚧 |
| Day Duration | `DayDurationHours` | hours 0.01..168 | 🚧 |
| Site Latitude | `SiteLatitudeDegrees` | degrees -90..+90 | 🚧 |
| Site Longitude | `SiteLongitudeDegrees` | degrees -180..+180 | 🚧 |
| Day of Month | `DayOfMonth` | 1..31 | 🚧 |
| Month | `Month` | 1..12 | 🚧 |
| Year | `Year` | calendar year | 🚧 |
| UTC Offset | `UtcOffsetHours` | hours -12..+14 | 🚧 |
| Manual Direction Override | `ManualDirectionOverride` | bool | 🚧 |
| Sun Azimuth | `ManualAzimuthDegrees` | degrees 0..360 (compass from North / +X) | 🚧 |
| Sun Elevation | `ManualElevationDegrees` | degrees -90..+90 | 🚧 |
| Appearance Mode | `AppearanceMode` | enum · Muted / Radiant | 🚧 |
| Sun Glare Strength | `SunGlareStrength` | x 0..2 (Radiant mode) | 🚧 |
| Resolved Elevation | `SolarEphemerisOutcome::ElevationDegrees` | degrees (read-only telemetry) | 🚧 |
| Resolved Azimuth | `SolarEphemerisOutcome::AzimuthDegrees` | degrees (read-only telemetry) | 🚧 |
| Resolved Declination | `SolarEphemerisOutcome::DeclinationDegrees` | degrees (read-only telemetry) | 🚧 |
| Equation of Time | `SolarEphemerisOutcome::EquationOfTimeMinutes` | minutes (read-only telemetry) | 🚧 |
| Sidereal Time | `SolarEphemerisOutcome::SiderealHours` | hours (read-only telemetry) | 🚧 |

## 3. Sun flare inspector · `LensFlareConfiguration`

| Proposed control | Backing field | Type / range | Status |
| :--- | :--- | :--- | :--- |
| Lens Flare | `Enabled` | bool | 🚧 |
| Flare Style | `Style` | enum · Anamorphic / Streaks / Halos | 🚧 |
| Flare Intensity | `FlareIntensity` | x 0..3 | 🚧 |
| Flare Spread | `FlareSpread` | x 0.2..2.0 | 🚧 |
| Halo Intensity | `HaloIntensity` | x 0..2.5 | 🚧 |
| Ghost Intensity | `GhostIntensity` | x 0..2.5 | 🚧 |
| Ghost Spacing | `GhostSpacing` | x 0.2..1.8 | 🚧 |
| Ghost Count | `GhostCount` | count 0..16 | 🚧 |
| Ghost Sides | `GhostSides` | count 0 round, 6 hex, 8 oct | 🚧 |
| Streak Rotation | `StreakRotationDegrees` | degrees 0..360 | 🚧 |

## 4. Sky / atmosphere inspector · `AtmosphericMediumConfiguration`

| Proposed control | Backing field | Type / range | Status |
| :--- | :--- | :--- | :--- |
| Atmosphere | `AtmosphereEnabled` | bool | 🟢 |
| Rayleigh Strength | `RayleighStrength` | x 0..4 | 🚧 |
| Mie Strength | `MieStrength` | x 0..6 | 🚧 |
| Mie Anisotropy | `MieAnisotropy` | g 0..0.99 | 🚧 |
| Ozone Strength | `OzoneStrength` | x 0..4 | 🚧 |
| Rayleigh Scale Height | `RayleighScaleHeightMeters` | m | 🚧 |
| Mie Scale Height | `MieScaleHeightMeters` | m | 🚧 |
| Atmosphere Thickness | `AtmosphereThicknessMeters` | m | 🚧 |
| Planet Radius | `PlanetRadiusMeters` | m | 🚧 |
| Limb Glow | `LimbGlowIntensity` | x 0..3 | 🚧 |
| Sky Brightness | `SkyBrightnessMultiplier` | x 0..3 | 🚧 |
| Sky Tint | `SkyTint` | linear RGB | 🚧 |
| Ground Albedo | `GroundAlbedo` | linear RGB | 🚧 |
| Orbital View | `OrbitalViewEnabled` | bool | 🚧 |
| Observer Altitude | `ObserverAltitudeMeters` | m | 🚧 |
| Exposure Stops | `RenderSpaceDocumentModel::ExposureStopsEV` | EV (RenderSpace) | 🚧 |
| Tone Transform | `RenderSpaceDocumentModel::ToneTransformOperator` | enum · ACESFilmicSrgb | 🚧 |

## 5. Cloud deck inspector · `CloudDeckConfiguration`

| Proposed control | Backing field | Type / range | Status |
| :--- | :--- | :--- | :--- |
| Clouds | `CloudsEnabled` | bool | 🟢 |
| Cloud Type | `Category` | enum 0..11 · Stratus, Stratocumulus, Cumulus, Cumulonimbus, Altostratus, Cirrus, Lenticular, Mammatus, KelvinHelmholtz, Asperitas, Nacreous, Pyrocumulus | 🚧 |
| Follow Wind | `FollowWindEnabled` | bool | 🚧 |
| Wind Source | `WindSource` | enum · CloudWind / LocalCloudWind / etc. | 🚧 |
| Cloud Cover | `CloudFraction` | 0..1 | 🟢 |
| Extinction Multiplier | `ExtinctionMultiplier` | x 0..4 | 🚧 |
| Feature Scale | `FeatureScaleMultiplier` | x 0.2..3.0 | 🚧 |
| Cloud Floor | `CloudFloorMeters` | m 100..15000 | 🚧 |
| Layer Thickness | `LayerThicknessMeters` | m 100..6500 | 🚧 |
| Deck Ceiling | `DeckCeilingMeters` | m 4000..22000 | 🚧 |
| Anvil Top Spread | `AnvilTopSpread` | 0..1 | 🚧 |
| Forward Scatter | `ForwardScatterAnisotropy` | g | 🚧 |
| Wind Speed | `WindSpeedMetersPerSecond` | m/s 0..40 | 🟢 |
| Wind Bearing | `WindBearingDegrees` | degrees 0..360 | 🚧 |
| Wind Shear | `WindShearPerKilometer` | /km | 🚧 |
| Wind Veer | `WindVeerDegreesPerKm` | deg/km -60..60 | 🚧 |
| Wind Turbulence | `WindTurbulence` | 0..1 | 🚧 |
| Cloud Render Scale | `CloudRenderScale` | 0.25..1.0 | 🚧 |
| Cloud March Steps | `CloudMarchSteps` | count 16..128 | 🚧 |
| Cloud Shadows | `CloudShadow.ShadowEnabled` | bool | 🟢 |
| Shadow Cloud Type | `CloudShadow.ShadowCategory` | enum 0..11 | 🚧 |
| Shadow Cover | `CloudShadow.ShadowFraction` | 0..1 | 🚧 |
| Shadow Density | `CloudShadow.ShadowDensityMultiplier` | x | 🚧 |
| Shadow Scale | `CloudShadow.ShadowScale` | x | 🚧 |
| Shadow Anvil | `CloudShadow.ShadowAnvil` | x | 🚧 |
| Shadow Floor | `CloudShadow.ShadowFloorMeters` | m | 🚧 |
| Shadow Thickness | `CloudShadow.ShadowThicknessMeters` | m | 🚧 |

## 6. Local cloud volumes · `BoundedCloudVolume` (`LocalCloud` + `LocalClouds[0..7]`)

| Proposed control | Backing field | Type / range | Status |
| :--- | :--- | :--- | :--- |
| Local Cloud | `LocalEnabled` | bool | 🚧 |
| Follow Wind | `FollowWindEnabled` | bool | 🚧 |
| Wind Source | `WindSource` | enum | 🚧 |
| Shape | `Shape` | enum · Box / Sphere / Cylinder / Prism / Wedge / Cone / Pyramid | 🚧 |
| Prism Sides | `PrismSides` | count 3..16 | 🚧 |
| Volume Center | `VolumeCenterMeters` | XYZ metres | 🚧 |
| Half Extents | `HalfExtentMeters` | XYZ metres | 🚧 |
| Yaw | `YawDegrees` | degrees | 🚧 |
| Edge Softness | `EdgeSoftness` | 0..1 | 🚧 |
| Extinction | `ExtinctionMultiplier` | x | 🚧 |
| Cover | `CloudFraction` | 0..1 | 🚧 |
| Feature Scale | `FeatureScaleMeters` | m | 🚧 |
| Forward Scatter | `ForwardScatterAnisotropy` | g | 🚧 |

## 7. Wind inspector · `AtmosphericWindParameters`

| Proposed control | Backing field | Type / range | Status |
| :--- | :--- | :--- | :--- |
| Wind | `Enabled` | bool | 🚧 |
| Speed | `SpeedMetersPerSecond` | m/s 0..40 | 🟢 (cloud speed row) |
| Bearing | `BearingDegrees` | degrees 0..360 | 🚧 |
| Shear | `ShearPerKilometer` | /km 0..2 | 🚧 |
| Veer | `VeerDegreesPerKm` | deg/km -60..60 | 🚧 |
| Air Shear Advection | `AirShearAdvection` | bool | 🚧 |
| Gust Strength | `GustStrength` | 0..1 | 🚧 |
| Gust Frequency | `GustFrequencyHz` | Hz | 🚧 |
| Turbulence | `Turbulence` | 0..1 | 🚧 |
| Steadiness | `Steadiness` | 0..1 | 🚧 |
| Simulation Seconds | `WorldAtmosphereConfiguration::SimulationSeconds` | s | 🚧 |

## 8. Fog and god rays · `AtmosphericFogConfiguration`

| Proposed control | Backing field | Type / range | Status |
| :--- | :--- | :--- | :--- |
| Height Fog | `HeightFog.Enabled` | bool | 🚧 |
| Height Fog Density | `HeightFog.Density` | 1/m 0..0.2 | 🚧 |
| Fog Falloff Height | `HeightFog.FalloffHeightMeters` | m 10..3000 | 🚧 |
| Fog Base Altitude | `HeightFog.BaseAltitudeMeters` | m | 🚧 |
| Fog Sun Scatter | `HeightFog.SunScatterIntensity` | x 0..2 | 🚧 |
| Fog Colour | `HeightFog.HeightColour` | linear RGB | 🚧 |
| Fog Follow Wind | `HeightFog.FollowWind` | bool | 🚧 |
| Fog Wind Source | `HeightFog.WindSource` | enum | 🚧 |
| Aerial Perspective | `AerialFog.Enabled` | bool | 🚧 |
| Aerial Density | `AerialFog.DensityMultiplier` | x 0..4 | 🚧 |
| Aerial Start Distance | `AerialFog.StartDistanceMeters` | m 0..2000 | 🚧 |
| Aerial Mie Blend | `AerialFog.MieBlend` | 0..1 | 🚧 |
| Aerial Scale Height | `AerialFog.ScaleHeightMeters` | m | 🚧 |
| Local Fog Volumes | `LocalFogs[0..7]` | 8 volumes · enabled, shape, prism sides, center, half extents, yaw, softness, density, coverage, feature scale, anisotropy, tint, wind coupling | 🚧 |
| God Rays | `GodRays.Enabled` | bool | 🚧 |
| God Ray Intensity | `GodRays.Intensity` | x 0..4 | 🚧 |
| God Ray Anisotropy | `GodRays.AnisotropyG` | g 0..0.95 | 🚧 |
| God Ray Decay | `GodRays.Decay` | 0..1 | 🚧 |
| God Ray Density | `GodRays.Density` | x 0..3 | 🚧 |
| God Ray Max Distance | `GodRays.MaxDistanceMeters` | m 10..2000 | 🚧 |
| God Ray Steps | `GodRays.SampleSteps` | count 8..48 | 🚧 |
| God Rays × Clouds | `GodRays.InteractWithClouds` | bool | 🚧 |
| God Rays × Fog | `GodRays.InteractWithFog` | bool | 🚧 |
| God Ray Tint | `GodRays.ShaftTint` | linear RGB | 🚧 |

## 9. Stars and moons · `StarfieldConfiguration`, `LunarBodyConfiguration[0..5]`

| Proposed control | Backing field | Type / range | Status |
| :--- | :--- | :--- | :--- |
| Starfield | `Stars.StarfieldEnabled` | bool | 🟢 (Sun, Moons & Stars) |
| Scintillation | `Stars.ScintillationEnabled` | bool | 🚧 |
| Limiting Magnitude | `Stars.LimitingMagnitude` | mag 0..8 | 🚧 |
| Star Brightness | `Stars.BrightnessMultiplier` | x 0..4 | 🚧 |
| Star Point Size | `Stars.PointSizeScale` | x 0.4..3.0 | 🚧 |
| Twinkle Depth | `Stars.ScintillationDepthPercent` | % 0..100 | 🚧 |
| Twinkle Rate | `Stars.ScintillationRateHertz` | Hz 0.2..3.0 | 🚧 |
| Celestial Rotation | `Stars.CelestialRotationDegrees` | degrees 0..360 | 🚧 |
| Bright Star Catalog | `Stars.EnableBrightStarCatalog` | bool | 🚧 |
| Galactic Band | `Stars.EnableGalacticBand` | bool | 🚧 |
| Zodiacal Light | `Stars.ZodiacalLightEnabled` | bool | 🚧 |
| Zodiacal Radiance | `Stars.ZodiacalRadianceScale` | x 0..4 | 🚧 |
| Moon Visible (6 slots) | `Moons[i].Visible` | bool | 🚧 |
| Moon Anti-Solar Lock | `Moons[i].FollowSkyOppositeSun` | bool | 🚧 |
| Moon Surface Preset | `Moons[i].SurfacePreset` | enum · Luna / Ember / Glacier / Sulfur / Shroud / Shard | 🚧 |
| Moon Phase | `Moons[i].SynodicPhaseFraction` | 0..1 | 🚧 |
| Moon Brightness | `Moons[i].BrightnessMultiplier` | x 0..6 | 🚧 |
| Moon Halo | `Moons[i].HaloGlowMultiplier` | x 0..3 | 🚧 |
| Moon Diameter | `Moons[i].AngularDiameterDegrees` | degrees 0.1..180 | 🚧 |
| Moon Azimuth | `Moons[i].AzimuthDegrees` | degrees 0..360 | 🚧 |
| Moon Elevation | `Moons[i].ElevationDegrees` | degrees -90..90 | 🚧 |
| Moon Surface Roll / Pitch | `Moons[i].SurfaceRollDegrees`, `SurfacePitchDegrees` | degrees | 🚧 |
| Moon Atmosphere | `Moons[i].HasAtmosphere` | bool | 🚧 |
| Moon Atmosphere Intensity | `Moons[i].AtmosphereIntensity` | x 0..3 | 🚧 |
| Moon Atmosphere Thickness | `Moons[i].AtmosphereThicknessScale` | x | 🚧 |
| Moon Atmosphere Tint | `Moons[i].AtmosphereTint` | linear RGB | 🚧 |

## 10. Render settings and shadow lab · `WorldRenderConfiguration` / `RenderSpace`

| Proposed control | Backing field | Type / range | Status |
| :--- | :--- | :--- | :--- |
| Engine | `Engine` | enum 0..5 | 🟢 |
| Shadow Algorithm | `ShadowAlgorithm` | enum 0..4 | 🟢 |
| Reflection Source | `ReflectionSource` | enum · WideTreeRaytrace / SkyEnvironment | 🟢 |
| GI Candidate | `GiCandidate` | enum · StokesContour / SurfelRadiosity | 🚧 |
| SDF Penumbra | `SdfPenumbraMode` | enum 0..5 | 🟢 |
| Path Trace Samples | `PathTraceSamples` | spp | 🚧 |
| Quality Tier | `QualityTier` | enum 0..4 | 🟢 |
| Render Scale | `ViewportDisplaySettings::RenderScalePercent` | % 50..200 | 🟢 |
| Reflections | `ReflectionsEnabled` | bool | 🟢 |
| Raytracing | `RaytracingEnabled` | bool | 🟢 |
| Anti-aliasing | `AntiAliasingEnabled` | bool | 🟢 |
| Cheap MatCap 2x2 Shadow | `RenderSpace` · `CheapMatCapTwoByTwoShadow` | bool | 🚧 |
| Static Cloud Render Scale | `RenderSpace` · `StaticCloudRenderScale` | 0.25..1.0 | 🚧 |
| Fine Cascade Resolution | `RenderSpace` · `CascadeResolutionTexels` | texels | 🚧 |
| Fine Cascade Half-Span | `RenderSpace` · `FineCascadeHalfSpanMeters` | m | 🚧 |
| Fine GDF Half-Span | `RenderSpace` · `FineDistanceFieldHalfMeters` | m | 🚧 |
| Coarse Cascade Half-Span | `RenderSpace` · `CoarseCascadeHalfSpanMeters` | m | 🚧 |
| Coarse GDF Half-Span | `RenderSpace` · `CoarseDistanceFieldHalfMeters` | m | 🚧 |
| Debug Channel | `DebugChannel` | enum 0..15 (§11) | 🟢 |

## 11. Debug views (after this branch's changes)

| Index | View | Source |
| ---: | :--- | :--- |
| 0 | Lit (off) | final composite |
| 1 | Depth | primary hit distance |
| 2 | Albedo | material diffuse albedo |
| 3 | Shading normal | shading normal |
| 4 | Roughness | material specular roughness |
| 5 | Metalness | material metalness |
| 6 | Material / cluster ID | hashed material slot |
| 7 | Geometric normal | triangle normal |
| 8 | Solar shadow | filtered sun visibility |
| 9 | Ambient occlusion | combined AO · SDF + optional XGTAO |
| 10 | Direct lighting | direct base + indirect irradiance |
| 11 | Specular & reflections | specular AO |
| 12 | SDF raw distance | signed distance field sample at first hit |
| 13 | SDF with materials | SDF visibility × material albedo |
| 14 | SDF normals | stored G-buffer normals |
| 15 | XGTAO | screen-space ground-truth-ish AO only |

## 12. Camera, gizmo and browser controls

| Control | Backing field | Type / range | Status |
| :--- | :--- | :--- | :--- |
| Fly Speed | `EditorLifecycle::CameraFlySpeedScalar` | 0.10x..16.00x, wheel | 🟢 |
| Sprint | Shift with RMB fly | x3 | 🟢 |
| Camera Eye Lag | `CameraLagComponent::EyeLagSeconds` | s 0..1 (editor default 0.09) | 🟢 |
| Camera Target Lag | `CameraLagComponent::TargetLagSeconds` | s 0..1 (editor default 0.07) | 🟢 |
| Gizmo Translate / Rotate / Scale / Universal | `TransformGizmo::ConfigureHandleFilter` | enum | 🟢 |
| Gizmo snap | Ctrl drag | bool | 🟢 |
| Open in Frontier | `WorkspaceCapabilityRegister::FrontierWorld` | sole browser action | 🟢 |
| New project | `ProjectGenerator::ProjectScaffoldSpecification` | blank plane + clear sky | 🟢 |

---

## 13. Quick prototype checklist (sun + cloud)

```text
Sun inspector
  Enabled · Diameter · Azimuth · Elevation · Manual override · Local hours
  Day cycle · Cycle speed · Day duration · Latitude · Longitude · Date · UTC offset
  Tint source · RGB tint · Kelvin · Intensity · Direct gain
  Appearance mode · Glare strength
  Flare: enabled · style · intensity · spread · halo · ghost intensity · ghost spacing · count · sides · streak rotation
  Resolved: elevation · azimuth · declination · equation of time · sidereal time

Cloud inspector
  Enabled · Cloud type (12) · Cover · Extinction · Feature scale
  Floor · Thickness · Ceiling · Anvil spread · Forward scatter
  Wind speed · bearing · shear · veer · turbulence · follow wind · source
  Render scale · March steps (16..128)
  Cloud shadows: enabled · type · cover · density · scale · anvil · floor · thickness
```
