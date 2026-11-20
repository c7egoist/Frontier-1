//------------------------------------------------------------------------------------------------------------------------
//                                                  PARTICLE SHADERS
//          GLSL ES 3.00 — Shape Sampling, Emit / Cloner Integration against the Force Field + Gas, Sprite Rendering
//------------------------------------------------------------------------------------------------------------------------

import { VolumeChunk, NoiseChunk, ForceFieldChunk, RampChunk } from './FluidShaders.js';

/** Deterministic shape sampling shared by spawning (random salt) and cloner homes (lattice salt = -1). */
export const ParticleShapeChunk = `
uniform int   uShape;        // 0 Point, 1 Sphere, 2 Box, 3 Disc, 4 Ring, 5 Line, 6 Grid
uniform vec3  uOrigin;
uniform vec3  uAxis;
uniform float uRadius;
uniform int   uCount;

void ShapeBasis(out vec3 t, out vec3 b)
{
    vec3 n = uAxis;
    vec3 helper = abs(n.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    t = normalize(cross(helper, n));
    b = cross(n, t);
}

// index/count drive lattice placement for cloners; rnd (3 uniform randoms) drives emission spawns.
vec3 ShapePoint(int index, vec3 rnd, bool lattice)
{
    vec3 t, b; ShapeBasis(t, b);
    float count = float(max(uCount, 1));
    float i = float(index);
    if (uShape == 1)
    {
        if (lattice)
        {
            float golden = 2.39996323;
            float y = 1.0 - (i + 0.5) / count * 2.0;
            float r = sqrt(max(0.0, 1.0 - y * y));
            float a = golden * i;
            return uOrigin + vec3(cos(a) * r, y, sin(a) * r) * uRadius;
        }
        vec3 d = normalize(rnd * 2.0 - 1.0 + 1e-4);
        return uOrigin + d * uRadius * pow(Hash31(rnd.zxy + 2.7), 1.0 / 3.0);
    }
    if (uShape == 2 || uShape == 6)
    {
        if (lattice || uShape == 6)
        {
            float side = ceil(pow(count, 1.0 / 3.0));
            float x = mod(i, side), y = mod(floor(i / side), side), z = floor(i / (side * side));
            vec3 cell = (vec3(x, y, z) + 0.5) / side * 2.0 - 1.0;
            return uOrigin + (t * cell.x + uAxis * cell.y + b * cell.z) * uRadius;
        }
        vec3 cell = rnd * 2.0 - 1.0;
        return uOrigin + (t * cell.x + uAxis * cell.y + b * cell.z) * uRadius;
    }
    if (uShape == 3)
    {
        float a, r;
        if (lattice) { a = 2.39996323 * i; r = sqrt((i + 0.5) / count); }
        else { a = rnd.x * 6.2831853; r = sqrt(rnd.y); }
        return uOrigin + (t * cos(a) + b * sin(a)) * r * uRadius;
    }
    if (uShape == 4)
    {
        float a = lattice ? i / count * 6.2831853 : rnd.x * 6.2831853;
        return uOrigin + (t * cos(a) + b * sin(a)) * uRadius;
    }
    if (uShape == 5)
    {
        float s = lattice ? (i + 0.5) / count * 2.0 - 1.0 : rnd.x * 2.0 - 1.0;
        return uOrigin + uAxis * s * uRadius;
    }
    return uOrigin;
}
`;

export const ParticleUpdateFragment = `#version 300 es
precision highp float; precision highp int; precision highp sampler2D;
${VolumeChunk}
${NoiseChunk}
${ForceFieldChunk}
${ParticleShapeChunk}
uniform sampler2D uPositions;    // xyz, age
uniform sampler2D uVelocities;   // xyz, seed
uniform sampler2D uVelocity;     // gas velocity volume
uniform int   uOutput;           // 0 positions, 1 velocities
uniform int   uReset;
uniform int   uMode;             // 0 emit, 1 cloner
uniform ivec2 uParticleSize;
uniform float uDeltaTime;
uniform float uEnvelope;         // timing-key envelope
uniform float uRate;
uniform float uLifetime;
uniform float uSpeed;
uniform float uSpread;
uniform float uGravity;
uniform float uDrag;
uniform float uNoise;
uniform float uFieldInfluence;
uniform float uGasInfluence;
uniform float uSpring;
uniform float uDamping;
uniform float uBounce;
out vec4 fragColour;

vec3 RandomDirection(vec3 seed)
{
    float z = Hash31(seed) * 2.0 - 1.0;
    float a = Hash31(seed.zxy + 5.1) * 6.2831853;
    float r = sqrt(max(0.0, 1.0 - z * z));
    return vec3(r * cos(a), z, r * sin(a));
}

void main()
{
    vec2 pixel = floor(gl_FragCoord.xy);
    int index = int(pixel.x) + int(pixel.y) * uParticleSize.x;
    vec4 p = texelFetch(uPositions, ivec2(pixel), 0);
    vec4 v = texelFetch(uVelocities, ivec2(pixel), 0);
    float seed = Hash31(vec3(pixel, 4.2));
    vec3 home = ShapePoint(index, vec3(0.0), true);
    if (uReset == 1)
    {
        if (uMode == 1) fragColour = uOutput == 0 ? vec4(home, seed * uLifetime) : vec4(0.0, 0.0, 0.0, seed);
        else fragColour = uOutput == 0 ? vec4(0.0, -1e4, 0.0, uLifetime + 1.0) : vec4(0.0, 0.0, 0.0, seed);
        return;
    }
    vec3 position = p.xyz;
    vec3 velocity = v.xyz;
    float age = p.w;
    vec3 half_ = vec3(uGrid) * uCellSize * 0.5;
    bool alive = uMode == 1 || age < uLifetime;
    if (index >= uCount) { fragColour = uOutput == 0 ? vec4(0.0, -1e4, 0.0, uLifetime + 1.0) : vec4(0.0, 0.0, 0.0, seed); return; }

    if (!alive)
    {
        float chance = uRate * uEnvelope * uDeltaTime;
        if (Hash31(vec3(pixel, uTime)) < chance)
        {
            vec3 rnd = vec3(Hash31(vec3(pixel, uTime + 1.3)), Hash31(vec3(pixel.yx, uTime + 2.9)), Hash31(vec3(pixel + 7.0, uTime + 4.1)));
            position = ShapePoint(index, rnd, false);
            vec3 random = RandomDirection(vec3(pixel, uTime + 8.8));
            velocity = normalize(mix(uAxis, random, uSpread) + 1e-4) * uSpeed * (0.75 + 0.5 * Hash31(vec3(pixel, uTime + 6.6)));
            age = 0.0;
            alive = true;
        }
    }
    if (alive)
    {
        age += uDeltaTime;
        vec3 acceleration = vec3(0.0, -uGravity, 0.0);
        if (uFieldInfluence != 0.0) acceleration += EvaluateForceField(position, velocity) * uFieldInfluence;
        if (uNoise != 0.0) acceleration += CurlNoise(position * 1.3 + vec3(uTime * 0.5, v.w * 7.0, uTime * 0.2)) * uNoise * 3.0;
        if (uGasInfluence != 0.0)
        {
            vec3 gas = SampleVolume(uVelocity, CellFromWorld(position)).xyz;
            acceleration += (gas - velocity) * uGasInfluence;
        }
        if (uMode == 1) acceleration += (home - position) * uSpring - velocity * uDamping;
        velocity += acceleration * uDeltaTime;
        velocity *= exp(-uDrag * uDeltaTime);
        position += velocity * uDeltaTime;
        float floorY = -half_.y + 0.01;
        if (position.y < floorY && uBounce >= 0.0)
        {
            position.y = floorY;
            velocity.y = abs(velocity.y) * uBounce;
            velocity.xz *= 0.7;
        }
        if (uMode == 0 && (any(greaterThan(abs(position.xz), half_.xz * 1.05)) || position.y > half_.y * 1.2 || position.y < -half_.y * 1.5)) age = uLifetime;
    }
    fragColour = uOutput == 0 ? vec4(position, age) : vec4(velocity, v.w);
}
`;

export const ParticleRenderVertex = `#version 300 es
precision highp float; precision highp int; precision highp sampler2D;
${NoiseChunk}
${RampChunk}
${ParticleShapeChunk}
uniform sampler2D uPositions;
uniform sampler2D uVelocities;
uniform sampler2D uPrevious;
uniform mat4  uViewProjection;
uniform ivec2 uParticleSize;
uniform int   uMode;
uniform int   uTrail;
uniform int   uColourMode;    // 0 life, 1 speed, 2 displacement, 3 height, 4 seed
uniform int   uRampMode;
uniform vec3  uColourStart;
uniform vec3  uColourEnd;
uniform float uLifetime;
uniform float uSizeStart;
uniform float uSizeEnd;
uniform float uMaxSpeed;
uniform float uDisplacementRange;
uniform float uFlicker;
uniform float uTime;
uniform float uDomainHeight;
uniform float uPixelRatio;
out vec4 vColour;
out float vSoft;
vec3 HueToRgb(float h)
{
    vec3 k = mod(vec3(5.0, 3.0, 1.0) + h * 6.0, 6.0);
    return 1.0 - clamp(min(k, 4.0 - k), 0.0, 1.0) * 0.85;
}
void main()
{
    int particle = uTrail == 1 ? gl_VertexID / 2 : gl_VertexID;
    int end = uTrail == 1 ? gl_VertexID - particle * 2 : 0;
    ivec2 pixel = ivec2(particle - (particle / uParticleSize.x) * uParticleSize.x, particle / uParticleSize.x);
    vec4 current = texelFetch(uPositions, pixel, 0);
    vec4 previous = texelFetch(uPrevious, pixel, 0);
    vec4 velocity = texelFetch(uVelocities, pixel, 0);
    bool respawned = current.w < previous.w || distance(current.xyz, previous.xyz) > 1.0;
    vec3 position = (end == 1 && !respawned) ? previous.xyz : current.xyz;
    float alive = (uMode == 1 || current.w < uLifetime) && particle < uCount ? 1.0 : 0.0;
    float life = uMode == 1 ? 0.5 : clamp(current.w / max(uLifetime, 1e-3), 0.0, 1.0);
    float speed = length(velocity.xyz);
    float t;
    vec3 colour;
    if (uColourMode == 1) { t = clamp(speed / max(uMaxSpeed, 1e-3), 0.0, 1.0); colour = Ramp(uRampMode, t); }
    else if (uColourMode == 2) { vec3 home = ShapePoint(particle, vec3(0.0), true); t = clamp(distance(current.xyz, home) / max(uDisplacementRange, 1e-3), 0.0, 1.0); colour = Ramp(uRampMode, t); }
    else if (uColourMode == 3) { t = clamp(current.y / max(uDomainHeight, 1e-3) + 0.5, 0.0, 1.0); colour = Ramp(uRampMode, t); }
    else if (uColourMode == 4) { colour = HueToRgb(velocity.w); t = life; }
    else { t = life; colour = mix(uColourStart, uColourEnd, life); }
    float flicker = uFlicker > 0.0 ? mix(1.0, 0.35 + 0.65 * (0.5 + 0.5 * sin(uTime * (3.0 + 5.0 * velocity.w) + velocity.w * 50.0)), uFlicker) : 1.0;
    float fade = uMode == 1 ? 1.0 : smoothstep(0.0, 0.05, life) * (1.0 - smoothstep(0.75, 1.0, life));
    float alpha = alive * fade * flicker * (end == 1 ? 0.3 * smoothstep(0.2, 2.5, speed) : 1.0);
    vColour = vec4(colour * (uColourMode == 4 ? 1.0 : (0.85 + 0.3 * flicker)), alpha);
    vSoft = uTrail == 1 ? 0.0 : 1.0;
    gl_Position = uViewProjection * vec4(position, 1.0);
    gl_PointSize = mix(uSizeStart, uSizeEnd, life) * (0.8 + 0.4 * velocity.w) * uPixelRatio;
}
`;

export const ParticleFragment = `#version 300 es
precision highp float;
in vec4 vColour;
in float vSoft;
out vec4 fragColour;
void main()
{
    if (vColour.a <= 0.0) discard;
    float alpha = vColour.a;
    if (vSoft > 0.5)
    {
        float d = length(gl_PointCoord - 0.5) * 2.0;
        if (d > 1.0) discard;
        alpha *= 1.0 - smoothstep(0.55, 1.0, d);
    }
    fragColour = vec4(vColour.rgb, alpha);
}
`;
