//============================================================================================================================================
//                                                     FORCEGENERATORCATALOG.JS
//                  Force Generator Type Registry — GLSL Type Codes, Parameter Schemas, Falloff Envelopes,
//                                 Emitter Shapes, Colour Ramps, and Scene Presets
//============================================================================================================================================

export const MaximumForceGenerators = 16;
export const MaximumEmitters = 6;
export const MaximumColliders = 4;

export const FalloffCurves = ['SmoothStep', 'Linear', 'InverseSquare', 'Constant', 'Gaussian', 'Plateau', 'Cosine'];
export const FalloffLabels = { SmoothStep: 'Smooth', Linear: 'Linear', InverseSquare: 'Inverse²', Constant: 'Infinite', Gaussian: 'Gaussian', Plateau: 'Plateau', Cosine: 'Interpolated' };
export const ForceCategories = ['Directional', 'Attraction', 'Rotational', 'Turbulence', 'Burst', 'Damping', 'Thermal'];
export const EmitterShapes = ['Sphere', 'Disc', 'Box'];
export const ColourRamps = ['Turbo', 'Engineering', 'Plasma', 'Diverging'];
export const SliceAxes = ['X', 'Y', 'Z'];
export const ScalarChannels = ['Speed', 'Pressure', 'Vorticity', 'Temperature', 'Density', 'Divergence', 'ForceMagnitude'];
export const AdvectionSchemes = ['SemiLagrangian', 'MacCormack'];
export const BoundaryModes = ['ClosedBox', 'OpenTop', 'OpenAll'];
export const VolumeFilters = ['Smooth', 'Voxel'];
export const VectorModes = ['Slice', 'Volume'];

//------------------------------------------------------------------------------------------------------------------------
//                                                  FORCE GENERATOR TYPES
//------------------------------------------------------------------------------------------------------------------------

export const ForceGeneratorTypes = Object.freeze([
    // ---------------------------------------------------------------- Directional
    {
        Id: 'DirectionalWind', Code: 0, Category: 'Directional', Name: 'Directional Wind', Icon: 'wind', Tint: '#38bdf8',
        Summary: 'Uniform directional push across the whole domain or a bounded reach, with optional gusting.',
        Defaults: { Strength: 6, Radius: 2.0, Direction: [1, 0, 0], Falloff: 'Constant', Frequency: 0.6, Secondary: 0.35, Tertiary: 0 },
        Labels: { Frequency: 'Gust Rate', Secondary: 'Gust Depth' },
    },
    {
        Id: 'WindShear', Code: 17, Category: 'Directional', Name: 'Wind Shear', Icon: 'shear', Tint: '#60a5fa',
        Summary: 'Crosswind whose heading rotates with height along the axis — layered, twisting flow.',
        Defaults: { Strength: 5, Radius: 2.0, Direction: [0, 1, 0], Falloff: 'Constant', Frequency: 0, Secondary: 1.0, Tertiary: 0 },
        Labels: { Secondary: 'Twist (half-turns)' },
    },
    {
        Id: 'FlowStream', Code: 16, Category: 'Directional', Name: 'Flow Stream', Icon: 'stream', Tint: '#7dd3fc',
        Summary: 'Steers gas toward a target velocity along the axis — a soft conveyor rather than a push.',
        Defaults: { Strength: 3, Radius: 1.2, Direction: [1, 0, 0], Falloff: 'SmoothStep', Frequency: 2.5, Secondary: 0, Tertiary: 0 },
        Labels: { Frequency: 'Target Speed' },
    },
    {
        Id: 'DirectionalBurst', Code: 14, Category: 'Burst', Name: 'Directional Burst', Icon: 'dburst', Tint: '#fb923c',
        Summary: 'Periodic impulse along one axis — cannon shots, piston puffs, muzzle blasts.',
        Defaults: { Strength: 45, Radius: 0.9, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 1.5, Secondary: 0.1, Tertiary: 0 },
        Labels: { Frequency: 'Pulse Period', Secondary: 'Pulse Width', Tertiary: 'Start Delay' },
    },
    // ---------------------------------------------------------------- Attraction
    {
        Id: 'PointAttractor', Code: 1, Category: 'Attraction', Name: 'Point Attractor', Icon: 'attractor', Tint: '#a78bfa',
        Summary: 'Pulls fluid toward a centre; negative strength repels.',
        Defaults: { Strength: 14, Radius: 1.4, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0, Secondary: 0.1, Tertiary: 0 },
        Labels: { Secondary: 'Core Softness' },
    },
    {
        Id: 'PointRepulsor', Code: 2, Category: 'Attraction', Name: 'Point Repulsor', Icon: 'repulsor', Tint: '#f472b6',
        Summary: 'Pushes fluid away from a centre in all directions.',
        Defaults: { Strength: 14, Radius: 1.2, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0, Secondary: 0.1, Tertiary: 0 },
        Labels: { Secondary: 'Core Softness' },
    },
    {
        Id: 'AxisAttractor', Code: 8, Category: 'Attraction', Name: 'Axis Attractor', Icon: 'axis', Tint: '#22d3ee',
        Summary: 'Pulls fluid toward an infinite line through its centre.',
        Defaults: { Strength: 10, Radius: 1.8, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 0, Secondary: 0, Tertiary: 0 },
        Labels: {},
    },
    {
        Id: 'Orbit', Code: 10, Category: 'Attraction', Name: 'Orbit', Icon: 'orbit', Tint: '#c4b5fd',
        Summary: 'Tangential circulation about a point with centripetal pull — gas orbits the centre.',
        Defaults: { Strength: 10, Radius: 1.6, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 0, Secondary: 0.5, Tertiary: 0 },
        Labels: { Secondary: 'Centripetal Pull' },
    },
    {
        Id: 'Containment', Code: 11, Category: 'Attraction', Name: 'Containment', Icon: 'containment', Tint: '#2dd4bf',
        Summary: 'Spring that pushes gas back inside its radius — an invisible soft bottle.',
        Defaults: { Strength: 20, Radius: 1.2, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 0, Secondary: 0.4, Tertiary: 0 },
        Labels: { Secondary: 'Wall Softness' },
    },
    {
        Id: 'MagneticDipole', Code: 19, Category: 'Attraction', Name: 'Magnetic Dipole', Icon: 'dipole', Tint: '#e879f9',
        Summary: 'Dipole field lines looping pole to pole — gas follows the magnetic flux.',
        Defaults: { Strength: 10, Radius: 1.8, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0, Secondary: 0.25, Tertiary: 0 },
        Labels: { Secondary: 'Core Radius' },
    },
    // ---------------------------------------------------------------- Rotational
    {
        Id: 'VortexColumn', Code: 3, Category: 'Rotational', Name: 'Vortex Column', Icon: 'vortex', Tint: '#34d399',
        Summary: 'Swirls fluid around an axis with optional inward pull and axial lift.',
        Defaults: { Strength: 12, Radius: 1.6, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 0.4, Secondary: 2.5, Tertiary: 0 },
        Labels: { Frequency: 'Inward Pull', Secondary: 'Axial Lift' },
    },
    {
        Id: 'VortexRing', Code: 9, Category: 'Rotational', Name: 'Vortex Ring', Icon: 'ring', Tint: '#c084fc',
        Summary: 'Toroidal circulation around a ring — launches smoke rings.',
        Defaults: { Strength: 16, Radius: 0.9, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0.35, Secondary: 0, Tertiary: 0 },
        Labels: { Frequency: 'Tube Radius' },
    },
    {
        Id: 'Spin', Code: 18, Category: 'Rotational', Name: 'Rigid Spin', Icon: 'spin', Tint: '#4ade80',
        Summary: 'Solid-body rotation: tangential force grows with distance from the axis.',
        Defaults: { Strength: 6, Radius: 1.5, Direction: [0, 1, 0], Falloff: 'Plateau', Frequency: 0, Secondary: 0, Tertiary: 0 },
        Labels: {},
    },
    // ---------------------------------------------------------------- Turbulence
    {
        Id: 'CurlNoiseTurbulence', Code: 4, Category: 'Turbulence', Name: 'Curl Noise Turbulence', Icon: 'turbulence', Tint: '#fbbf24',
        Summary: 'Divergence-free animated curl noise that breaks up laminar flow.',
        Defaults: { Strength: 9, Radius: 2.4, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 1.8, Secondary: 0.7, Tertiary: 0 },
        Labels: { Frequency: 'Noise Scale', Secondary: 'Evolution Speed' },
    },
    {
        Id: 'BrownianJitter', Code: 15, Category: 'Turbulence', Name: 'Brownian Jitter', Icon: 'brownian', Tint: '#facc15',
        Summary: 'Per-cell random kicks re-rolled at a fixed rate — fine-grained, incoherent agitation.',
        Defaults: { Strength: 6, Radius: 1.5, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 6, Secondary: 12, Tertiary: 0 },
        Labels: { Frequency: 'Cell Scale', Secondary: 'Re-roll Rate (Hz)' },
    },
    // ---------------------------------------------------------------- Burst
    {
        Id: 'RadialBurst', Code: 6, Category: 'Burst', Name: 'Radial Burst', Icon: 'burst', Tint: '#fb7185',
        Summary: 'Pulsing outward blast filling the reach — detonation kick.',
        Defaults: { Strength: 40, Radius: 1.5, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 1.2, Secondary: 0.12, Tertiary: 0 },
        Labels: { Frequency: 'Pulse Period', Secondary: 'Pulse Width', Tertiary: 'Start Delay' },
    },
    {
        Id: 'ShockwaveShell', Code: 12, Category: 'Burst', Name: 'Shockwave Shell', Icon: 'shockwave', Tint: '#f87171',
        Summary: 'A thin spherical pressure front expanding from the centre to the reach radius each period.',
        Defaults: { Strength: 60, Radius: 2.0, Direction: [0, 1, 0], Falloff: 'Constant', Frequency: 1.6, Secondary: 0.12, Tertiary: 0 },
        Labels: { Frequency: 'Period', Secondary: 'Shell Thickness', Tertiary: 'Start Delay' },
    },
    {
        Id: 'Implosion', Code: 13, Category: 'Burst', Name: 'Implosion', Icon: 'implosion', Tint: '#fda4af',
        Summary: 'Periodic inward collapse toward the centre — the pre-blast suck of a fireball.',
        Defaults: { Strength: 35, Radius: 1.5, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 1.5, Secondary: 0.15, Tertiary: 0 },
        Labels: { Frequency: 'Pulse Period', Secondary: 'Pulse Width', Tertiary: 'Start Delay' },
    },
    // ---------------------------------------------------------------- Damping / Thermal
    {
        Id: 'DragBaffle', Code: 5, Category: 'Damping', Name: 'Drag Baffle', Icon: 'drag', Tint: '#94a3b8',
        Summary: 'Damps velocity inside its reach — a soft brake on the flow.',
        Defaults: { Strength: 4, Radius: 1.0, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0, Secondary: 0, Tertiary: 0 },
        Labels: {},
    },
    {
        Id: 'ThermalUpdraft', Code: 7, Category: 'Thermal', Name: 'Thermal Updraft', Icon: 'updraft', Tint: '#f97316',
        Summary: 'Cylindrical column of lift along its axis.',
        Defaults: { Strength: 8, Radius: 0.8, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0, Secondary: 0, Tertiary: 0 },
        Labels: {},
    },
]);

export const ForceTypeById = Object.fromEntries(ForceGeneratorTypes.map((type) => [type.Id, type]));

//------------------------------------------------------------------------------------------------------------------------
//                                                  COLOUR RAMPS (Overlay Legend Mirror)
//------------------------------------------------------------------------------------------------------------------------

export const RampStops = Object.freeze({
    Turbo: ['#30123b', '#4662d7', '#36aaf9', '#1ae4b6', '#72fe5e', '#c8ef34', '#faba39', '#f66b19', '#ca2a04', '#7a0403'],
    Engineering: ['#0b1f3a', '#12518a', '#1aa0c8', '#5fd0a0', '#e5e36a', '#f28b2d', '#d9281c'],
    Plasma: ['#0d0887', '#5b02a3', '#9a179b', '#cb4679', '#ed7953', '#fdb32f', '#f0f921'],
    Diverging: ['#2166ac', '#67a9cf', '#d1e5f0', '#f7f7f7', '#fddbc7', '#ef8a62', '#b2182b'],
});

//------------------------------------------------------------------------------------------------------------------------
//                                                  SCENE PRESETS
//------------------------------------------------------------------------------------------------------------------------

export const SmokeLooks = Object.freeze({
    Soot: { Albedo: '#4b4b4f', Absorption: 4.0, Anisotropy: 0.4 },
    Grey: { Albedo: '#8e8f94', Absorption: 3.2, Anisotropy: 0.45 },
    Steam: { Albedo: '#d9dde3', Absorption: 2.2, Anisotropy: 0.6 },
    Dust: { Albedo: '#b89c74', Absorption: 2.6, Anisotropy: 0.3 },
});

export const FireLooks = Object.freeze({
    Wood: { FireEmber: '#ff2a05', FireFlame: '#ff8a16', FireCore: '#fff3cf' },
    Gas: { FireEmber: '#1f4bff', FireFlame: '#58a6ff', FireCore: '#e6f2ff' },
    Fuel: { FireEmber: '#b3120a', FireFlame: '#ff6a00', FireCore: '#ffd27a' },
    Magic: { FireEmber: '#5b0bb5', FireFlame: '#c84bff', FireCore: '#f6e3ff' },
});

export const ScenePresets = Object.freeze([
    {
        Id: 'preset-campfire', Name: 'Campfire', Icon: 'flame', Summary: 'Fuel-burning flame with soot plume and gentle crosswind.',
        Domain: { Resolution: [48, 96, 48], Size: [3, 6, 3], Buoyancy: 1.6, AmbientTemperature: 0, Cooling: 0.8, Vorticity: 0.35, Dissipation: 0.995, Combustion: 1.2 },
        Look: { Albedo: '#4b4b4f', Absorption: 4.0, FireIntensity: 4.5 },
        Emitters: [
            { Name: 'Fire Source', Shape: 'Disc', Position: [0, -2.7, 0], Radius: 0.42, Density: 0.3, Temperature: 4.5, Fuel: 1.0, Velocity: [0, 1.2, 0], Jitter: 0.6 },
        ],
        Forces: [
            { Type: 'DirectionalWind', Position: [0, 0, 0], Strength: 1.2, Direction: [1, 0, 0.2], Falloff: 'Constant', Frequency: 0.5, Secondary: 0.6 },
            { Type: 'CurlNoiseTurbulence', Position: [0, 0.3, 0], Strength: 2.5, Radius: 2.0, Frequency: 2.4, Secondary: 0.9 },
        ],
    },
    {
        Id: 'preset-smoke-plume', Name: 'Smoke Plume', Icon: 'smoke', Summary: 'Dense cold smoke rising through a vortex column.',
        Domain: { Resolution: [48, 96, 48], Size: [3, 6, 3], Buoyancy: 0.9, AmbientTemperature: 0, Cooling: 0.4, Vorticity: 0.5, Dissipation: 0.998 },
        Look: { Albedo: '#8e8f94', Absorption: 3.2 },
        Emitters: [
            { Name: 'Smoke Stack', Shape: 'Sphere', Position: [0, -2.5, 0], Radius: 0.35, Density: 1.4, Temperature: 1.1, Fuel: 0, Velocity: [0, 1.5, 0], Jitter: 0.3 },
        ],
        Forces: [
            { Type: 'VortexColumn', Position: [0, 0, 0], Strength: 7, Radius: 1.6, Frequency: 0.6, Secondary: 1.2 },
        ],
    },
    {
        Id: 'preset-explosion', Name: 'Explosion', Icon: 'burst', Summary: 'Radial burst detonation with high-temperature fireball.',
        Domain: { Resolution: [64, 96, 64], Size: [4, 6, 4], Buoyancy: 1.0, AmbientTemperature: 0, Cooling: 1.1, Vorticity: 0.6, Dissipation: 0.993, Combustion: 1.8 },
        Look: { Albedo: '#3f3d3c', Absorption: 4.5, FireIntensity: 6.0, FireOpacity: 2.0, FireSoftness: 4.5, FireEmber: '#b3120a', FireFlame: '#ff6a00', FireCore: '#ffd27a', Exposure: 1.0 },
        Emitters: [
            { Name: 'Detonation Core', Shape: 'Sphere', Position: [0, -2.2, 0], Radius: 0.55, Density: 0.6, Temperature: 7.5, Fuel: 1.6, Velocity: [0, 0, 0], Jitter: 1.0, Pulse: 2.6 },
        ],
        Forces: [
            { Type: 'RadialBurst', Position: [0, -2.2, 0], Strength: 55, Radius: 1.8, Frequency: 2.6, Secondary: 0.1 },
            { Type: 'CurlNoiseTurbulence', Position: [0, 0, 0], Strength: 6, Radius: 2.6, Frequency: 1.6, Secondary: 1.2 },
        ],
    },
    {
        Id: 'preset-tornado', Name: 'Tornado Funnel', Icon: 'vortex', Summary: 'Strong vortex with axial lift and inward pull lofting dust.',
        Domain: { Resolution: [48, 96, 48], Size: [3, 6, 3], Buoyancy: 0.3, AmbientTemperature: 0, Cooling: 0.2, Vorticity: 0.25, Dissipation: 0.998 },
        Look: { Albedo: '#b89c74', Absorption: 2.6, Anisotropy: 0.3 },
        Emitters: [
            { Name: 'Dust Ring', Shape: 'Disc', Position: [0, -2.8, 0], Radius: 1.1, Density: 0.8, Temperature: 0.2, Fuel: 0, Velocity: [0, 0.3, 0], Jitter: 0.5 },
        ],
        Forces: [
            { Type: 'VortexColumn', Position: [0, 0, 0], Strength: 18, Radius: 1.4, Falloff: 'Linear', Frequency: 1.4, Secondary: 4.0 },
            { Type: 'DragBaffle', Position: [0, 2.0, 0], Strength: 2.5, Radius: 1.3 },
        ],
    },
    {
        Id: 'preset-wind-tunnel', Name: 'Wind Tunnel', Icon: 'wind', Summary: 'Crosswind jet over a spherical collider with a von Kármán wake.',
        Domain: { Resolution: [96, 48, 48], Size: [6, 3, 3], Buoyancy: 0.0, AmbientTemperature: 0, Cooling: 0.0, Vorticity: 0.45, Dissipation: 0.999 },
        Boundary: 'OpenAll',
        Look: { Albedo: '#d9dde3', Absorption: 2.2, Anisotropy: 0.6 },
        Emitters: [
            { Name: 'Tracer Inlet', Shape: 'Disc', Position: [-2.6, 0, 0], Radius: 0.6, Density: 0.7, Temperature: 0, Fuel: 0, Velocity: [4.5, 0, 0], Jitter: 0.05, Orientation: [1, 0, 0] },
        ],
        Forces: [
            { Type: 'DirectionalWind', Position: [0, 0, 0], Strength: 3.5, Direction: [1, 0, 0], Falloff: 'Constant', Frequency: 0.0, Secondary: 0.0 },
        ],
        Colliders: [
            { Name: 'Bluff Sphere', Position: [-0.8, 0, 0], Radius: 0.45 },
        ],
    },
    {
        Id: 'preset-smoke-ring', Name: 'Smoke Ring Cannon', Icon: 'ring', Summary: 'Vortex ring generator launching toroidal puffs.',
        Domain: { Resolution: [64, 64, 96], Size: [3, 3, 4.5], Buoyancy: 0.15, AmbientTemperature: 0, Cooling: 0.1, Vorticity: 0.2, Dissipation: 0.998 },
        Boundary: 'OpenAll',
        Look: { Albedo: '#d9dde3', Absorption: 2.4, Anisotropy: 0.6 },
        Emitters: [
            { Name: 'Ring Muzzle', Shape: 'Disc', Position: [0, 0, -1.8], Radius: 0.45, Density: 1.2, Temperature: 0.3, Fuel: 0, Velocity: [0, 0, 3.0], Jitter: 0.1, Orientation: [0, 0, 1], Pulse: 1.8 },
        ],
        Forces: [
            { Type: 'VortexRing', Position: [0, 0, -1.3], Strength: 14, Radius: 0.5, Direction: [0, 0, 1], Frequency: 0.3 },
        ],
    },
]);

//------------------------------------------------------------------------------------------------------------------------
//                                                  FACTORY HELPERS
//------------------------------------------------------------------------------------------------------------------------

let NextNodeIndex = 1;

export function allocateNodeId(prefix)
{
    NextNodeIndex += 1;
    return `${prefix}-${NextNodeIndex.toString(36)}`;
}

export function createForceGenerator(typeId, overrides = {})
{
    const type = ForceTypeById[typeId];
    if (!type) throw new Error(`Unknown force generator type "${typeId}".`);
    return {
        Id: allocateNodeId('force'),
        Kind: 'Force',
        Type: type.Id,
        Name: overrides.Name || type.Name,
        Enabled: true,
        Visible: true,
        Position: [...(overrides.Position || [0, 0, 0])],
        Direction: [...(overrides.Direction || type.Defaults.Direction)],
        Strength: overrides.Strength ?? type.Defaults.Strength,
        Radius: overrides.Radius ?? type.Defaults.Radius,
        Falloff: overrides.Falloff || type.Defaults.Falloff,
        Frequency: overrides.Frequency ?? type.Defaults.Frequency,
        Secondary: overrides.Secondary ?? type.Defaults.Secondary,
        Tertiary: overrides.Tertiary ?? type.Defaults.Tertiary ?? 0,
    };
}

export function createEmitter(overrides = {})
{
    return {
        Id: allocateNodeId('emitter'),
        Kind: 'Emitter',
        Name: overrides.Name || 'Emitter',
        Enabled: true,
        Visible: true,
        Shape: overrides.Shape || 'Sphere',
        Position: [...(overrides.Position || [0, -1.5, 0])],
        Orientation: [...(overrides.Orientation || [0, 1, 0])],
        Radius: overrides.Radius ?? 0.4,
        Density: overrides.Density ?? 1.0,
        Temperature: overrides.Temperature ?? 2.0,
        Fuel: overrides.Fuel ?? 0.0,
        Velocity: [...(overrides.Velocity || [0, 1, 0])],
        Jitter: overrides.Jitter ?? 0.4,
        Pulse: overrides.Pulse ?? 0.0,
    };
}

export function createCollider(overrides = {})
{
    return {
        Id: allocateNodeId('collider'),
        Kind: 'Collider',
        Name: overrides.Name || 'Sphere Collider',
        Enabled: true,
        Visible: true,
        Position: [...(overrides.Position || [0, 0, 0])],
        Radius: overrides.Radius ?? 0.5,
    };
}

export const ResolutionPresets = Object.freeze({ Draft: 32, Low: 48, Medium: 64, High: 96, Ultra: 128, Extreme: 160 });

// Resolution follows the Unreal convention: the domain keeps its world size, `MaxCells` sets the cell count along the
// longest axis, and `CellSize` + per-axis `Resolution` are derived so shrinking the voxel count makes the gas coarser.
export function resolveDomainGrid(domain)
{
    const longest = Math.max(domain.Size[0], domain.Size[1], domain.Size[2]);
    const maxCells = Math.max(8, Math.min(160, Math.round(domain.MaxCells)));
    const cellSize = longest / maxCells;
    domain.CellSize = cellSize;
    domain.Resolution = domain.Size.map((extent) => Math.max(8, Math.min(160, Math.round(extent / cellSize))));
    return domain;
}

export function createDomain(overrides = {})
{
    const size = [...(overrides.Size || [3, 6, 3])];
    const maxCells = overrides.MaxCells || (overrides.Resolution ? Math.max(...overrides.Resolution) : 96);
    return resolveDomainGrid({
        Id: 'domain',
        Kind: 'Domain',
        Name: 'Gas Domain',
        Size: size,
        MaxCells: maxCells,
        Buoyancy: overrides.Buoyancy ?? 1.2,
        AmbientTemperature: overrides.AmbientTemperature ?? 0,
        Cooling: overrides.Cooling ?? 0.6,
        Vorticity: overrides.Vorticity ?? 0.35,
        Dissipation: overrides.Dissipation ?? 0.996,
        VelocityDissipation: overrides.VelocityDissipation ?? 0.999,
        Gravity: overrides.Gravity ?? 0.25,
        PressureIterations: overrides.PressureIterations ?? 36,
        Advection: overrides.Advection || 'MacCormack',
        Boundary: overrides.Boundary || 'OpenTop',
        TimeScale: overrides.TimeScale ?? 1.0,
        Combustion: overrides.Combustion ?? 1.0,
    });
}

export function createVisualizerSet()
{
    return {
        Volume: {
            Id: 'vis-volume', Kind: 'Visualizer', Name: 'Volume Render', Visible: true,
            Density: 1.0, Albedo: '#8e8f94', Absorption: 3.2, Anisotropy: 0.45, Ambient: '#3a414c', LightIntensity: 1.4, Exposure: 1.15,
            FireIntensity: 4.0, FireThreshold: 1.0, FireSoftness: 3.5, FireOpacity: 1.4, FireEmber: '#ff2a05', FireFlame: '#ff8a16', FireCore: '#fff3cf',
            Steps: 80, ShadowSteps: 4, Filter: 'Smooth',
        },
        VelocityVectors: { Id: 'vis-vectors', Kind: 'Visualizer', Name: 'Velocity Vectors', Visible: true, Axis: 'Z', Offset: 0.5, Stride: 2, Scale: 0.35, Ramp: 'Turbo', MaxSpeed: 6 },
        ScalarSlice: { Id: 'vis-slice', Kind: 'Visualizer', Name: 'Scalar Slice', Visible: false, Axis: 'Y', Offset: 0.35, Channel: 'Speed', Ramp: 'Turbo', Range: 6, Opacity: 0.85 },
        Tracers: { Id: 'vis-tracers', Kind: 'Visualizer', Name: 'Flow Tracers', Visible: true, Count: 8192, Lifetime: 4.0, Ramp: 'Turbo', MaxSpeed: 6, Size: 2.0 },
        ForceVectors: { Id: 'vis-forces', Kind: 'Visualizer', Name: 'Force Vectors', Visible: false, Mode: 'Slice', Axis: 'Z', Offset: 0.5, Stride: 3, Scale: 0.04, Ramp: 'Plasma', MaxForce: 20 },
        ForceGizmos: { Id: 'vis-gizmos', Kind: 'Visualizer', Name: 'Force Gizmos', Visible: true, Opacity: 0.9 },
    };
}
