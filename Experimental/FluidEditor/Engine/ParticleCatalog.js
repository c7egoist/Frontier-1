//------------------------------------------------------------------------------------------------------------------------
//                                                  PARTICLE CATALOG
//                     Mograph-style Particle / VFX System Data Model: Shapes, Modes, Colour Rules, Templates
//
//  This file is the portable specification of the particle module. It contains no WebGL — only enumerations, numeric
//  codes (which the GLSL and a future C++ port share) and factory helpers. ParticleShaders.js implements the behaviour
//  on the GPU and ParticleSimulation.js owns the per-system state. Adding a shape, colour rule or template means adding
//  one entry here plus its branch in the shader; nothing else in the studio needs to change.
//------------------------------------------------------------------------------------------------------------------------

import { allocateNodeId } from './ForceGeneratorCatalog.js';

export const MaximumParticleSystems = 4;
export const MaximumParticlesPerSystem = 16384;

/** Spawn / cloner shapes. Code = index. Direction acts as the shape axis for Disc, Ring, Line and Grid. */
export const ParticleShapes = Object.freeze(['Point', 'Sphere', 'Box', 'Disc', 'Ring', 'Line', 'Grid']);

/** Emit = particles are born, age and die. Cloner = a fixed formation displaced by forces and pulled home by a spring. */
export const ParticleModes = Object.freeze(['Emit', 'Cloner']);

/** Colour rules. Code = index. */
export const ParticleColourModes = Object.freeze(['Life', 'Speed', 'Displacement', 'Height', 'Seed', 'Swatch', 'Cycle']);
export const ParticleColourLabels = Object.freeze({ Life: 'Gradient over life', Speed: 'Speed', Displacement: 'Distance from home', Height: 'Height', Seed: 'Random hue', Swatch: 'Random palette swatch', Cycle: 'Palette cycling in time' });
/** Where Speed / Displacement / Height take their gradient from. */
export const ParticleRampSources = Object.freeze(['Palette', 'Ramp']);
export const MaximumPaletteStops = 8;
export const PalettePresets = Object.freeze({
    Fire: ['#fff3c4', '#ffb454', '#ff5a1f', '#b3120a'],
    Ice: ['#ffffff', '#bfe9ff', '#38bdf8', '#1d4ed8'],
    Candy: ['#ff5f7e', '#ffb454', '#fef08a', '#34c759', '#38bdf8', '#a78bfa'],
    Neon: ['#f472b6', '#a78bfa', '#38bdf8', '#34c759'],
    Mono: ['#ffffff', '#71717a'],
    Earth: ['#d6c7ad', '#b89c74', '#8a7a5c', '#4b4038'],
});

export const ParticleBlendModes = Object.freeze(['Alpha', 'Additive']);
export const ParticleRenderStyles = Object.freeze(['Point', 'Streak']);

export const ParticleCategories = Object.freeze(['Emitters', 'Cloners', 'Environment']);

/** Asset-browser templates. Each is a full set of overrides for createParticleSystem. */
export const ParticleTemplates = Object.freeze([
    {
        Id: 'sparks', Name: 'Sparks', Category: 'Emitters', Icon: 'sparks', Tint: '#ffb454',
        Summary: 'Hot ballistic sparks that streak, cool and fall.',
        Defaults: { Mode: 'Emit', Shape: 'Point', Radius: 0.1, Count: 4096, Rate: 18, Lifetime: 1.4, Speed: 4.5, Spread: 0.55, Direction: [0, 1, 0], Gravity: 6, Drag: 0.6, Noise: 0.4, Bounce: 0.2, SizeStart: 3.5, SizeEnd: 1.0, ColourMode: 'Life', Palette: ['#fff3c4', '#ffb454', '#ff5a1f', '#b3120a'], BrightnessJitter: 0.3, Blend: 'Additive', Style: 'Streak' },
    },
    {
        Id: 'confetti', Name: 'Confetti Cannon', Category: 'Emitters', Icon: 'confetti', Tint: '#f472b6',
        Summary: 'Rainbow confetti blasted from a nozzle, fluttering down and settling.',
        Defaults: { Mode: 'Emit', Shape: 'Disc', Radius: 0.25, Count: 4096, Rate: 10, Lifetime: 5, Speed: 9, Spread: 0.3, Direction: [0.3, 1, 0], Gravity: 5, Drag: 1.6, Noise: 1.8, Bounce: 0.25, SizeStart: 4.5, SizeEnd: 4.0, ColourMode: 'Swatch', Palette: ['#ff5f7e', '#ffb454', '#fef08a', '#34c759', '#38bdf8', '#a78bfa'], HueJitter: 0.08, Blend: 'Alpha', Style: 'Point' },
    },
    {
        Id: 'fireflies', Name: 'Fireflies', Category: 'Emitters', Icon: 'fireflies', Tint: '#d9ff7a',
        Summary: 'Slow pulsing motes wandering on curl noise.',
        Defaults: { Mode: 'Emit', Shape: 'Sphere', Radius: 1.4, Count: 1024, Rate: 1.5, Lifetime: 7, Speed: 0.25, Spread: 1.0, Gravity: 0, Drag: 1.2, Noise: 1.3, Bounce: 0, SizeStart: 3.5, SizeEnd: 2.5, ColourMode: 'Life', Palette: ['#f7ffb0', '#d9ff7a', '#5c8a00'], HueJitter: 0.06, Blend: 'Additive', Style: 'Point', Flicker: 1.0 },
    },
    {
        Id: 'fountain', Name: 'Fountain', Category: 'Emitters', Icon: 'fountain', Tint: '#38bdf8',
        Summary: 'Water-like jet that arcs under gravity and splashes on the floor.',
        Defaults: { Mode: 'Emit', Shape: 'Disc', Radius: 0.12, Count: 8192, Rate: 25, Lifetime: 3, Speed: 6.5, Spread: 0.12, Direction: [0, 1, 0], Gravity: 9.8, Drag: 0.2, Noise: 0.2, Bounce: 0.15, SizeStart: 2.5, SizeEnd: 2.0, ColourMode: 'Speed', RampSource: 'Palette', Palette: ['#1d6fa8', '#38bdf8', '#bfe9ff', '#ffffff'], Blend: 'Alpha', Style: 'Streak' },
    },
    {
        Id: 'grid-cloner', Name: 'Grid Cloner', Category: 'Cloners', Icon: 'gridcloner', Tint: '#a78bfa',
        Summary: 'Lattice of points held by springs — forces push it, colour shows the displacement.',
        Defaults: { Mode: 'Cloner', Shape: 'Grid', Radius: 1.6, Count: 4096, Spring: 7, Damping: 3.5, FieldInfluence: 1.0, GasInfluence: 0.0, Gravity: 0, Drag: 0, Noise: 0, SizeStart: 4.0, SizeEnd: 4.0, ColourMode: 'Displacement', RampSource: 'Palette', Palette: ['#8b7cf6', '#38bdf8', '#34c759', '#ffb454', '#f472b6'], DisplacementRange: 0.8, Blend: 'Alpha', Style: 'Point' },
    },
    {
        Id: 'ring-cloner', Name: 'Ring Cloner', Category: 'Cloners', Icon: 'ringcloner', Tint: '#34c759',
        Summary: 'Circle of clones that ripples around the ring under any field.',
        Defaults: { Mode: 'Cloner', Shape: 'Ring', Radius: 1.5, Count: 512, Spring: 9, Damping: 4, FieldInfluence: 1.0, GasInfluence: 0.0, Gravity: 0, Drag: 0, Noise: 0, SizeStart: 6.0, SizeEnd: 6.0, ColourMode: 'Displacement', RampSource: 'Palette', Palette: ['#34c759', '#fef08a', '#ffb454', '#ff5f7e'], DisplacementRange: 0.6, Blend: 'Alpha', Style: 'Point' },
    },
    {
        Id: 'sphere-swarm', Name: 'Sphere Swarm', Category: 'Cloners', Icon: 'swarm', Tint: '#f59e0b',
        Summary: 'Fibonacci sphere shell with a loose spring and gentle noise — a breathing swarm.',
        Defaults: { Mode: 'Cloner', Shape: 'Sphere', Radius: 1.3, Count: 2048, Spring: 3, Damping: 1.5, FieldInfluence: 1.0, GasInfluence: 0.0, Gravity: 0, Drag: 0, Noise: 0.6, SizeStart: 3.5, SizeEnd: 3.5, ColourMode: 'Displacement', RampSource: 'Palette', Palette: ['#ffb454', '#f97316', '#ef4444', '#f472b6'], DisplacementRange: 0.7, BrightnessJitter: 0.2, Blend: 'Additive', Style: 'Point' },
    },
    {
        Id: 'line-cloner', Name: 'Line Cloner', Category: 'Cloners', Icon: 'linecloner', Tint: '#38bdf8',
        Summary: 'Row of clones along the axis — a wave-table that reacts to every gizmo you drag through it.',
        Defaults: { Mode: 'Cloner', Shape: 'Line', Radius: 2.2, Count: 256, Spring: 10, Damping: 3, FieldInfluence: 1.0, GasInfluence: 0.0, Gravity: 0, Drag: 0, Noise: 0, SizeStart: 7.0, SizeEnd: 7.0, ColourMode: 'Displacement', RampSource: 'Palette', Palette: ['#38bdf8', '#a78bfa', '#f472b6'], DisplacementRange: 0.5, Blend: 'Alpha', Style: 'Point' },
    },
    {
        Id: 'snowfall', Name: 'Snowfall', Category: 'Environment', Icon: 'snow', Tint: '#e2e8f0',
        Summary: 'Slow flakes drifting down through the gas flow.',
        Defaults: { Mode: 'Emit', Shape: 'Box', Radius: 2.4, Count: 4096, Rate: 4, Lifetime: 9, Speed: 0.4, Spread: 0.4, Direction: [0, -1, 0], Gravity: 0.5, Drag: 1.8, Noise: 0.7, GasInfluence: 1.5, Bounce: 0, SizeStart: 2.5, SizeEnd: 2.5, ColourMode: 'Life', Palette: ['#ffffff', '#cbd5e1'], BrightnessJitter: 0.25, Blend: 'Alpha', Style: 'Point' },
    },
    {
        Id: 'ground-dust', Name: 'Ground Dust', Category: 'Environment', Icon: 'dust', Tint: '#b89c74',
        Summary: 'Floor-hugging motes picked up by whatever the gas is doing.',
        Defaults: { Mode: 'Emit', Shape: 'Disc', Radius: 2.4, Count: 4096, Rate: 6, Lifetime: 6, Speed: 0.1, Spread: 1.0, Gravity: 0.3, Drag: 1.0, Noise: 0.3, GasInfluence: 3.0, Bounce: 0, SizeStart: 2.0, SizeEnd: 1.5, ColourMode: 'Speed', RampSource: 'Palette', Palette: ['#8a7a5c', '#b89c74', '#d6c7ad', '#fff3c4'], BrightnessJitter: 0.3, Blend: 'Alpha', Style: 'Point' },
    },
    {
        Id: 'rain', Name: 'Rain', Category: 'Environment', Icon: 'rain', Tint: '#60a5fa',
        Summary: 'Fast streaks falling straight through the domain.',
        Defaults: { Mode: 'Emit', Shape: 'Box', Radius: 2.4, Count: 8192, Rate: 12, Lifetime: 2, Speed: 7, Spread: 0.02, Direction: [0, -1, 0], Gravity: 4, Drag: 0.1, Noise: 0.05, GasInfluence: 0.4, Bounce: 0.1, SizeStart: 1.5, SizeEnd: 1.5, ColourMode: 'Life', Palette: ['#bfdbfe', '#60a5fa'], Blend: 'Alpha', Style: 'Streak' },
    },
]);

export const ParticleTemplateById = Object.freeze(Object.fromEntries(ParticleTemplates.map((template) => [template.Id, template])));

export function createParticleSystem(templateId, overrides = {})
{
    const template = ParticleTemplateById[templateId] || ParticleTemplates[0];
    const base = { ...template.Defaults, ...overrides };
    return {
        Id: allocateNodeId('particles'),
        Kind: 'Particles',
        Template: template.Id,
        Name: overrides.Name || template.Name,
        Enabled: true,
        Visible: true,
        Mode: base.Mode || 'Emit',
        Shape: base.Shape || 'Point',
        Position: [...(base.Position || [0, 0, 0])],
        Direction: [...(base.Direction || [0, 1, 0])],
        Radius: base.Radius ?? 0.5,
        Count: Math.min(MaximumParticlesPerSystem, Math.max(16, Math.round(base.Count ?? 2048))),
        Rate: base.Rate ?? 8,                      // respawn probability per second for dead particles
        Lifetime: base.Lifetime ?? 3,
        Speed: base.Speed ?? 2,
        Spread: base.Spread ?? 0.5,                // 0 = all along Direction, 1 = fully random
        Gravity: base.Gravity ?? 0,
        Drag: base.Drag ?? 0.5,
        Noise: base.Noise ?? 0,                    // curl-noise wander strength
        FieldInfluence: base.FieldInfluence ?? 1,  // how much scene force generators accelerate the particles
        GasInfluence: base.GasInfluence ?? 0,      // coupling to the fluid velocity field
        Spring: base.Spring ?? 6,                  // cloner return stiffness
        Damping: base.Damping ?? 3,
        Bounce: base.Bounce ?? 0.2,
        SizeStart: base.SizeStart ?? 3,
        SizeEnd: base.SizeEnd ?? 1,
        ColourMode: base.ColourMode || 'Life',
        Palette: [...(base.Palette && base.Palette.length ? base.Palette : [base.ColourStart || '#ffffff', base.ColourEnd || '#808080'])].slice(0, MaximumPaletteStops),
        RampSource: base.RampSource || 'Palette',
        HueJitter: base.HueJitter ?? 0,
        BrightnessJitter: base.BrightnessJitter ?? 0,
        CycleSpeed: base.CycleSpeed ?? 0.5,
        Brightness: base.Brightness ?? 1,
        Ramp: base.Ramp || 'Turbo',
        DisplacementRange: base.DisplacementRange ?? 1,
        MaxSpeed: base.MaxSpeed ?? 6,
        Flicker: base.Flicker ?? 0,
        Blend: base.Blend || 'Alpha',
        Style: base.Style || 'Point',
        StartTime: base.StartTime ?? 0,
        Duration: base.Duration ?? 0,
        Fade: base.Fade ?? 0.15,
        Repeat: base.Repeat ?? 0,
    };
}

/** Square texture side that holds `count` particles. */
export function particleTextureSide(count)
{
    const side = Math.ceil(Math.sqrt(Math.max(16, Math.min(MaximumParticlesPerSystem, count))));
    return Math.min(128, Math.max(4, Math.ceil(side / 4) * 4));
}
