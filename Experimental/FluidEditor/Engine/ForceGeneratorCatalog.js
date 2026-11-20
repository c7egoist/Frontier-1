//============================================================================================================================================
//                                                     FORCEGENERATORCATALOG.JS
//                  Force Generator Type Registry — GLSL Type Codes, Parameter Schemas, Falloff Envelopes,
//                                 Emitter Shapes, Colour Ramps, and Scene Presets
//============================================================================================================================================

export const MaximumForceGenerators = 12;
export const MaximumEmitters = 6;
export const MaximumColliders = 4;

export const FalloffCurves = ['SmoothStep', 'Linear', 'InverseSquare', 'Constant'];
export const EmitterShapes = ['Sphere', 'Disc', 'Box'];
export const ColourRamps = ['Turbo', 'Engineering', 'Plasma', 'Diverging'];
export const SliceAxes = ['X', 'Y', 'Z'];
export const ScalarChannels = ['Speed', 'Pressure', 'Vorticity', 'Temperature', 'Density', 'Divergence', 'ForceMagnitude'];
export const AdvectionSchemes = ['SemiLagrangian', 'MacCormack'];
export const BoundaryModes = ['ClosedBox', 'OpenTop', 'OpenAll'];

//------------------------------------------------------------------------------------------------------------------------
//                                                  FORCE GENERATOR TYPES
//------------------------------------------------------------------------------------------------------------------------

export const ForceGeneratorTypes = Object.freeze([
    {
        Id: 'DirectionalWind', Code: 0, Name: 'Directional Wind', Icon: 'wind', Tint: '#38bdf8',
        Summary: 'Uniform directional push across the whole domain or a spherical reach.',
        Defaults: { Strength: 6, Radius: 2.0, Direction: [1, 0, 0], Falloff: 'Constant', Frequency: 0.6, Secondary: 0.35 },
        Labels: { Secondary: 'Gust Depth', Frequency: 'Gust Rate' },
    },
    {
        Id: 'PointAttractor', Code: 1, Name: 'Point Attractor', Icon: 'attractor', Tint: '#a78bfa',
        Summary: 'Pulls fluid toward a centre; negative strength repels.',
        Defaults: { Strength: 14, Radius: 1.4, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0, Secondary: 0 },
        Labels: { Secondary: 'Core Softness' },
    },
    {
        Id: 'PointRepulsor', Code: 2, Name: 'Point Repulsor', Icon: 'repulsor', Tint: '#f472b6',
        Summary: 'Pushes fluid away from a centre in all directions.',
        Defaults: { Strength: 14, Radius: 1.2, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0, Secondary: 0 },
        Labels: { Secondary: 'Core Softness' },
    },
    {
        Id: 'VortexColumn', Code: 3, Name: 'Vortex Column', Icon: 'vortex', Tint: '#34d399',
        Summary: 'Swirls fluid around an axis with optional inward pull and axial lift.',
        Defaults: { Strength: 12, Radius: 1.6, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 0.4, Secondary: 2.5 },
        Labels: { Secondary: 'Axial Lift', Frequency: 'Inward Pull' },
    },
    {
        Id: 'CurlNoiseTurbulence', Code: 4, Name: 'Curl Noise Turbulence', Icon: 'turbulence', Tint: '#fbbf24',
        Summary: 'Divergence-free animated curl noise that breaks up laminar flow.',
        Defaults: { Strength: 9, Radius: 2.4, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 1.8, Secondary: 0.7 },
        Labels: { Secondary: 'Evolution Speed', Frequency: 'Noise Scale' },
    },
    {
        Id: 'DragBaffle', Code: 5, Name: 'Drag Baffle', Icon: 'drag', Tint: '#94a3b8',
        Summary: 'Damps velocity inside its reach — a soft brake on the flow.',
        Defaults: { Strength: 4, Radius: 1.0, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0, Secondary: 0 },
        Labels: {},
    },
    {
        Id: 'RadialBurst', Code: 6, Name: 'Radial Burst', Icon: 'burst', Tint: '#fb7185',
        Summary: 'Pulsing outward shockwave with a tunable period.',
        Defaults: { Strength: 40, Radius: 1.5, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 1.2, Secondary: 0.12 },
        Labels: { Secondary: 'Pulse Width', Frequency: 'Pulse Period' },
    },
    {
        Id: 'ThermalUpdraft', Code: 7, Name: 'Thermal Updraft', Icon: 'updraft', Tint: '#f97316',
        Summary: 'Cylindrical column of lift along its axis.',
        Defaults: { Strength: 8, Radius: 0.8, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0, Secondary: 0 },
        Labels: {},
    },
    {
        Id: 'AxisAttractor', Code: 8, Name: 'Axis Attractor', Icon: 'axis', Tint: '#22d3ee',
        Summary: 'Pulls fluid toward an infinite line through its centre.',
        Defaults: { Strength: 10, Radius: 1.8, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 0, Secondary: 0 },
        Labels: {},
    },
    {
        Id: 'VortexRing', Code: 9, Name: 'Vortex Ring', Icon: 'ring', Tint: '#c084fc',
        Summary: 'Toroidal circulation around a ring — launches smoke rings.',
        Defaults: { Strength: 16, Radius: 0.9, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0.35, Secondary: 0 },
        Labels: { Frequency: 'Tube Radius' },
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
        Domain: { Resolution: [64, 96, 64], Size: [3, 4.5, 3], Buoyancy: 1.6, AmbientTemperature: 0, Cooling: 0.8, Vorticity: 0.35, Dissipation: 0.995, Combustion: 1.2 },
        Look: { Albedo: '#4b4b4f', Absorption: 4.0, FireIntensity: 4.5 },
        Emitters: [
            { Name: 'Fire Source', Shape: 'Disc', Position: [0, -2.0, 0], Radius: 0.42, Density: 0.3, Temperature: 4.5, Fuel: 1.0, Velocity: [0, 1.2, 0], Jitter: 0.6 },
        ],
        Forces: [
            { Type: 'DirectionalWind', Position: [0, 0, 0], Strength: 1.2, Direction: [1, 0, 0.2], Falloff: 'Constant', Frequency: 0.5, Secondary: 0.6 },
            { Type: 'CurlNoiseTurbulence', Position: [0, 0.3, 0], Strength: 2.5, Radius: 2.0, Frequency: 2.4, Secondary: 0.9 },
        ],
    },
    {
        Id: 'preset-smoke-plume', Name: 'Smoke Plume', Icon: 'smoke', Summary: 'Dense cold smoke rising through a vortex column.',
        Domain: { Resolution: [64, 96, 64], Size: [3, 4.5, 3], Buoyancy: 0.9, AmbientTemperature: 0, Cooling: 0.4, Vorticity: 0.5, Dissipation: 0.998 },
        Look: { Albedo: '#8e8f94', Absorption: 3.2 },
        Emitters: [
            { Name: 'Smoke Stack', Shape: 'Sphere', Position: [0, -1.8, 0], Radius: 0.35, Density: 1.4, Temperature: 1.1, Fuel: 0, Velocity: [0, 1.5, 0], Jitter: 0.3 },
        ],
        Forces: [
            { Type: 'VortexColumn', Position: [0, 0, 0], Strength: 7, Radius: 1.6, Frequency: 0.6, Secondary: 1.2 },
        ],
    },
    {
        Id: 'preset-explosion', Name: 'Explosion', Icon: 'burst', Summary: 'Radial burst detonation with high-temperature fireball.',
        Domain: { Resolution: [72, 72, 72], Size: [4, 4, 4], Buoyancy: 1.0, AmbientTemperature: 0, Cooling: 1.1, Vorticity: 0.6, Dissipation: 0.993, Combustion: 1.8 },
        Look: { Albedo: '#3f3d3c', Absorption: 4.5, FireIntensity: 6.0, FireOpacity: 2.0, FireSoftness: 4.5, FireEmber: '#b3120a', FireFlame: '#ff6a00', FireCore: '#ffd27a', Exposure: 1.0 },
        Emitters: [
            { Name: 'Detonation Core', Shape: 'Sphere', Position: [0, -0.6, 0], Radius: 0.55, Density: 0.6, Temperature: 7.5, Fuel: 1.6, Velocity: [0, 0, 0], Jitter: 1.0, Pulse: 2.6 },
        ],
        Forces: [
            { Type: 'RadialBurst', Position: [0, -0.6, 0], Strength: 55, Radius: 1.8, Frequency: 2.6, Secondary: 0.1 },
            { Type: 'CurlNoiseTurbulence', Position: [0, 0, 0], Strength: 6, Radius: 2.6, Frequency: 1.6, Secondary: 1.2 },
        ],
    },
    {
        Id: 'preset-tornado', Name: 'Tornado Funnel', Icon: 'vortex', Summary: 'Strong vortex with axial lift and inward pull lofting dust.',
        Domain: { Resolution: [64, 96, 64], Size: [3, 4.5, 3], Buoyancy: 0.3, AmbientTemperature: 0, Cooling: 0.2, Vorticity: 0.25, Dissipation: 0.998 },
        Look: { Albedo: '#b89c74', Absorption: 2.6, Anisotropy: 0.3 },
        Emitters: [
            { Name: 'Dust Ring', Shape: 'Disc', Position: [0, -2.1, 0], Radius: 1.1, Density: 0.8, Temperature: 0.2, Fuel: 0, Velocity: [0, 0.3, 0], Jitter: 0.5 },
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

export function createDomain(overrides = {})
{
    return {
        Id: 'domain',
        Kind: 'Domain',
        Name: 'Gas Domain',
        Resolution: [...(overrides.Resolution || [64, 96, 64])],
        Size: [...(overrides.Size || [3, 4.5, 3])],
        Buoyancy: overrides.Buoyancy ?? 1.2,
        AmbientTemperature: overrides.AmbientTemperature ?? 0,
        Cooling: overrides.Cooling ?? 0.6,
        Vorticity: overrides.Vorticity ?? 0.35,
        Dissipation: overrides.Dissipation ?? 0.996,
        VelocityDissipation: overrides.VelocityDissipation ?? 0.999,
        Gravity: overrides.Gravity ?? 0.25,
        PressureIterations: overrides.PressureIterations ?? 24,
        Advection: overrides.Advection || 'MacCormack',
        Boundary: overrides.Boundary || 'ClosedBox',
        TimeScale: overrides.TimeScale ?? 1.0,
        Combustion: overrides.Combustion ?? 1.0,
    };
}

export function createVisualizerSet()
{
    return {
        Volume: {
            Id: 'vis-volume', Kind: 'Visualizer', Name: 'Volume Render', Visible: true,
            Density: 1.0, Albedo: '#8e8f94', Absorption: 3.2, Anisotropy: 0.45, Ambient: '#3a414c', LightIntensity: 1.4, Exposure: 1.15,
            FireIntensity: 4.0, FireThreshold: 1.0, FireSoftness: 3.5, FireOpacity: 1.4, FireEmber: '#ff2a05', FireFlame: '#ff8a16', FireCore: '#fff3cf',
            Steps: 80, ShadowSteps: 4,
        },
        VelocityVectors: { Id: 'vis-vectors', Kind: 'Visualizer', Name: 'Velocity Vectors', Visible: true, Axis: 'Z', Offset: 0.5, Stride: 2, Scale: 0.35, Ramp: 'Turbo', MaxSpeed: 6 },
        ScalarSlice: { Id: 'vis-slice', Kind: 'Visualizer', Name: 'Scalar Slice', Visible: false, Axis: 'Y', Offset: 0.35, Channel: 'Speed', Ramp: 'Turbo', Range: 6, Opacity: 0.85 },
        Tracers: { Id: 'vis-tracers', Kind: 'Visualizer', Name: 'Flow Tracers', Visible: true, Count: 8192, Lifetime: 4.0, Ramp: 'Turbo', MaxSpeed: 6, Size: 2.0 },
        ForceGizmos: { Id: 'vis-gizmos', Kind: 'Visualizer', Name: 'Force Gizmos', Visible: true, Opacity: 0.9 },
    };
}
