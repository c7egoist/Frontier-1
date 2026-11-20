//============================================================================================================================================
//                                                          FLUIDSOLVER.JS
//             GPU Eulerian Gas Solver Orchestration — Tiled 3D Half-Float Fields, Pass Sequencing (Inject → Advect →
//             Thermodynamics → Forces → Vorticity Confinement → Pressure Projection), Force / Emitter / Collider
//                                  Uniform Packing, and Flow Tracer Particle Integration
//============================================================================================================================================

import { computeVolumeLayout } from './GpuContext.js';
import * as Shaders from './FluidShaders.js';
import { ForceTypeById, FalloffCurves, EmitterShapes, BoundaryModes, MaximumForceGenerators, MaximumEmitters, MaximumColliders, timingEnvelope, DebrisKinds, emitterTransform } from './ForceGeneratorCatalog.js';

export class FluidSolver
{
    constructor(context)
    {
        this.context = context;
        this.gl = context.gl;
        this.time = 0;
        this.frame = 0;
        this.layout = null;
        this.fields = null;
        this.tracerSize = [128, 64];
        this.gridCentre = [0, 0, 0];
        this.relocations = 0;
        this.tracers = null;
        this.statistics = { stepMilliseconds: 0, passes: 0 };
        this.forceArrays = {
            a: new Float32Array(MaximumForceGenerators * 4),
            b: new Float32Array(MaximumForceGenerators * 4),
            c: new Float32Array(MaximumForceGenerators * 4),
            d: new Float32Array(MaximumForceGenerators * 4),
            count: 0,
        };
        this.emitterArrays = {
            a: new Float32Array(MaximumEmitters * 4),
            b: new Float32Array(MaximumEmitters * 4),
            c: new Float32Array(MaximumEmitters * 4),
            d: new Float32Array(MaximumEmitters * 4),
            e: new Float32Array(MaximumEmitters * 4),
            count: 0,
        };
        this.colliderArray = { data: new Float32Array(MaximumColliders * 4), count: 0 };
        this.compilePrograms();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  PROGRAMS + FIELDS
    //------------------------------------------------------------------------------------------------------------------------

    compilePrograms()
    {
        const create = (label, fragment) => this.context.createProgram(label, Shaders.FullscreenVertex, fragment);
        this.programs = {
            advect: create('Advect', Shaders.AdvectFragment),
            macCormack: create('MacCormack', Shaders.MacCormackFragment),
            inject: create('Inject', Shaders.InjectFragment),
            forces: create('Forces', Shaders.ForcesFragment),
            curl: create('Curl', Shaders.CurlFragment),
            vorticity: create('Vorticity', Shaders.VorticityFragment),
            divergence: create('Divergence', Shaders.DivergenceFragment),
            pressure: create('Pressure', Shaders.PressureFragment),
            gradient: create('Gradient', Shaders.GradientFragment),
            thermodynamics: create('Thermodynamics', Shaders.ThermodynamicsFragment),
            tracerUpdate: create('TracerUpdate', Shaders.TracerUpdateFragment),
            debrisUpdate: create('DebrisUpdate', Shaders.DebrisUpdateFragment),
            shift: create('Shift', Shaders.ShiftFragment),
        };
    }

    configure(resolution, cellSize, centre = [0, 0, 0])
    {
        const [width, height, depth] = resolution.map((value) => Math.max(8, Math.min(160, Math.round(value))));
        const layout = computeVolumeLayout(width, height, depth);
        const unchanged = this.layout && this.layout.width === width && this.layout.height === height && this.layout.depth === depth;
        this.cellSize = cellSize;
        this.gridCentre = [centre[0], centre[1], centre[2]];
        if (unchanged) return;
        this.destroyFields();
        this.layout = layout;
        const context = this.context;
        const w = layout.textureWidth, h = layout.textureHeight;
        this.fields = {
            velocity: context.createPingPong(w, h),
            state: context.createPingPong(w, h),
            pressure: context.createPingPong(w, h),
            divergence: context.createTarget(w, h),
            curl: context.createTarget(w, h),
            scratchA: context.createTarget(w, h),
            scratchB: context.createTarget(w, h),
        };
        this.tracers = context.createPingPong(this.tracerSize[0], this.tracerSize[1], { rgba32f: true, nearest: true });
        this.tracerPrevious = context.createTarget(this.tracerSize[0], this.tracerSize[1], { rgba32f: true, nearest: true });
        this.debrisSize = [64, 64];
        this.debrisPositions = context.createPingPong(this.debrisSize[0], this.debrisSize[1], { rgba32f: true, nearest: true });
        this.debrisVelocities = context.createPingPong(this.debrisSize[0], this.debrisSize[1], { rgba32f: true, nearest: true });
        this.debrisPrevious = context.createTarget(this.debrisSize[0], this.debrisSize[1], { rgba32f: true, nearest: true });
        this.reset();
    }

    destroyFields()
    {
        if (!this.fields) return;
        const context = this.context;
        context.destroyPingPong(this.fields.velocity);
        context.destroyPingPong(this.fields.state);
        context.destroyPingPong(this.fields.pressure);
        context.destroyTarget(this.fields.divergence);
        context.destroyTarget(this.fields.curl);
        context.destroyTarget(this.fields.scratchA);
        context.destroyTarget(this.fields.scratchB);
        context.destroyPingPong(this.tracers);
        context.destroyTarget(this.tracerPrevious);
        context.destroyPingPong(this.debrisPositions);
        context.destroyPingPong(this.debrisVelocities);
        context.destroyTarget(this.debrisPrevious);
        this.fields = null;
    }

    /**
     * World Grid relocation: the simulated window has stepped `shiftCells` cells through world space. Every field is
     * copied across by that integer offset (an exact texel move — no resampling blur) so the gas keeps its world position
     * while the window re-centres; the new centre feeds every world↔cell mapping through uGridCentre.
     */
    relocate(shiftCells, centre)
    {
        this.gridCentre = [centre[0], centre[1], centre[2]];
        if (!this.fields || !shiftCells.some((value) => value !== 0)) return;
        const gl = this.gl, context = this.context, layout = this.layout;
        const whole = Math.abs(shiftCells[0]) >= layout.width || Math.abs(shiftCells[1]) >= layout.height || Math.abs(shiftCells[2]) >= layout.depth;
        const uniforms = context.use(this.programs.shift);
        this.applyVolumeUniforms(uniforms);
        gl.uniform3i(uniforms.uShift, shiftCells[0], shiftCells[1], shiftCells[2]);
        for (const pair of [this.fields.velocity, this.fields.state, this.fields.pressure])
        {
            if (whole) { context.clearTarget(pair.read); continue; }
            context.bindTexture('uSource', pair.read.texture);
            context.dispatch(pair.write);
            pair.swap();
            this.statistics.passes += 1;
        }
        this.relocations = (this.relocations || 0) + 1;
    }

    reset()
    {
        const context = this.context;
        for (const pair of [this.fields.velocity, this.fields.state, this.fields.pressure])
        {
            context.clearTarget(pair.read);
            context.clearTarget(pair.write);
        }
        context.clearTarget(this.fields.divergence);
        context.clearTarget(this.fields.curl);
        this.time = 0;
        this.frame = 0;
        this.tracersNeedReset = true;
        this.debrisNeedReset = true;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  UNIFORM PACKING
    //------------------------------------------------------------------------------------------------------------------------

    packForces(forces)
    {
        const arrays = this.forceArrays;
        let count = 0;
        for (const force of forces)
        {
            if (!force.Enabled || count >= MaximumForceGenerators) continue;
            const type = ForceTypeById[force.Type];
            const envelope = timingEnvelope(force, this.time);
            const offset = count * 4;
            arrays.a.set([force.Position[0], force.Position[1], force.Position[2], force.Radius], offset);
            arrays.b.set([force.Direction[0], force.Direction[1], force.Direction[2], force.Strength * envelope], offset);
            arrays.c.set([type.Code, Math.max(0, FalloffCurves.indexOf(force.Falloff)), force.Frequency, force.Secondary], offset);
            arrays.d.set([force.Tertiary || 0, 0, 0, 0], offset);
            count += 1;
        }
        arrays.count = count;
    }

    packEmitters(emitters)
    {
        const arrays = this.emitterArrays;
        let count = 0;
        for (const emitter of emitters)
        {
            if (!emitter.Enabled || count >= MaximumEmitters) continue;
            const envelope = timingEnvelope(emitter, this.time);
            if (envelope <= 0 && !(emitter.Debris > 0)) continue;
            const offset = count * 4;
            const transform = emitterTransform(emitter, this.time);
            arrays.a.set([transform.Position[0], transform.Position[1], transform.Position[2], emitter.Radius], offset);
            arrays.b.set([transform.Velocity[0], transform.Velocity[1], transform.Velocity[2], emitter.Density * envelope], offset);
            arrays.c.set([emitter.Temperature * envelope, emitter.Fuel * envelope, emitter.Jitter, Math.max(0, EmitterShapes.indexOf(emitter.Shape))], offset);
            arrays.d.set([emitter.Orientation[0], emitter.Orientation[1], emitter.Orientation[2], emitter.Pulse], offset);
            arrays.e.set([emitter.Debris || 0, emitter.DebrisSpeed || 0, envelope, Math.max(0, DebrisKinds.indexOf(emitter.DebrisKind))], offset);
            count += 1;
        }
        arrays.count = count;
    }

    packColliders(colliders)
    {
        let count = 0;
        for (const collider of colliders)
        {
            if (!collider.Enabled || count >= MaximumColliders) continue;
            this.colliderArray.data.set([collider.Position[0], collider.Position[1], collider.Position[2], collider.Radius], count * 4);
            count += 1;
        }
        this.colliderArray.count = count;
    }

    applyVolumeUniforms(uniforms)
    {
        const gl = this.gl;
        const layout = this.layout;
        if (uniforms.uGrid) gl.uniform3i(uniforms.uGrid, layout.width, layout.height, layout.depth);
        if (uniforms.uTiles) gl.uniform2i(uniforms.uTiles, layout.tilesX, layout.tilesY);
        if (uniforms.uTexSize) gl.uniform2f(uniforms.uTexSize, layout.textureWidth, layout.textureHeight);
        if (uniforms.uCellSize) gl.uniform1f(uniforms.uCellSize, this.cellSize);
        if (uniforms.uGridCentre) gl.uniform3f(uniforms.uGridCentre, this.gridCentre[0], this.gridCentre[1], this.gridCentre[2]);
    }

    applyForceUniforms(uniforms)
    {
        const gl = this.gl;
        if (uniforms.uForceCount === undefined) return;
        gl.uniform1i(uniforms.uForceCount, this.forceArrays.count);
        gl.uniform4fv(uniforms.uForceA, this.forceArrays.a);
        gl.uniform4fv(uniforms.uForceB, this.forceArrays.b);
        gl.uniform4fv(uniforms.uForceC, this.forceArrays.c);
        gl.uniform4fv(uniforms.uForceD, this.forceArrays.d);
        gl.uniform1f(uniforms.uTime, this.time);
    }

    applyColliderUniforms(uniforms)
    {
        const gl = this.gl;
        if (uniforms.uColliderCount === undefined) return;
        gl.uniform1i(uniforms.uColliderCount, this.colliderArray.count);
        gl.uniform4fv(uniforms.uColliders, this.colliderArray.data);
    }

    applyBoundaryUniform(uniforms, domain)
    {
        if (uniforms.uBoundaryMode !== undefined) this.gl.uniform1i(uniforms.uBoundaryMode, Math.max(0, BoundaryModes.indexOf(domain.Boundary)));
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  PASSES
    //------------------------------------------------------------------------------------------------------------------------

    advectField(pair, velocityTexture, deltaTime, dissipation, scheme)
    {
        const gl = this.gl, context = this.context, programs = this.programs, fields = this.fields;
        if (scheme === 'MacCormack')
        {
            // Forward trace into scratchA.
            let uniforms = context.use(programs.advect);
            this.applyVolumeUniforms(uniforms);
            context.bindTexture('uVelocity', velocityTexture);
            context.bindTexture('uSource', pair.read.texture);
            gl.uniform1f(uniforms.uDeltaTime, deltaTime);
            gl.uniform1f(uniforms.uDissipation, dissipation);
            gl.uniform1f(uniforms.uDirection, 1.0);
            context.dispatch(fields.scratchA);
            // Reverse trace of the forward result into scratchB.
            context.bindTexture('uVelocity', velocityTexture);
            context.bindTexture('uSource', fields.scratchA.texture);
            gl.uniform1f(uniforms.uDirection, -1.0);
            gl.uniform1f(uniforms.uDissipation, 1.0);
            context.dispatch(fields.scratchB);
            // Error-compensated combine with monotone clamp.
            uniforms = context.use(programs.macCormack);
            this.applyVolumeUniforms(uniforms);
            context.bindTexture('uVelocity', velocityTexture);
            context.bindTexture('uSource', pair.read.texture);
            context.bindTexture('uForward', fields.scratchA.texture);
            context.bindTexture('uBackward', fields.scratchB.texture);
            gl.uniform1f(uniforms.uDeltaTime, deltaTime);
            gl.uniform1f(uniforms.uDissipation, dissipation);
            context.dispatch(pair.write);
            pair.swap();
            this.statistics.passes += 3;
            return;
        }
        const uniforms = context.use(programs.advect);
        this.applyVolumeUniforms(uniforms);
        context.bindTexture('uVelocity', velocityTexture);
        context.bindTexture('uSource', pair.read.texture);
        gl.uniform1f(uniforms.uDeltaTime, deltaTime);
        gl.uniform1f(uniforms.uDissipation, dissipation);
        gl.uniform1f(uniforms.uDirection, 1.0);
        context.dispatch(pair.write);
        pair.swap();
        this.statistics.passes += 1;
    }

    inject(mode, pair, deltaTime)
    {
        const gl = this.gl, context = this.context;
        const uniforms = context.use(this.programs.inject);
        this.applyVolumeUniforms(uniforms);
        context.bindTexture('uTarget', pair.read.texture);
        gl.uniform1i(uniforms.uMode, mode);
        gl.uniform1i(uniforms.uEmitterCount, this.emitterArrays.count);
        gl.uniform4fv(uniforms.uEmitterA, this.emitterArrays.a);
        gl.uniform4fv(uniforms.uEmitterB, this.emitterArrays.b);
        gl.uniform4fv(uniforms.uEmitterC, this.emitterArrays.c);
        gl.uniform4fv(uniforms.uEmitterD, this.emitterArrays.d);
        gl.uniform1f(uniforms.uDeltaTime, deltaTime);
        gl.uniform1f(uniforms.uTime, this.time);
        context.dispatch(pair.write);
        pair.swap();
        this.statistics.passes += 1;
    }

    step(scene, deltaTime)
    {
        if (!this.fields) return;
        const started = performance.now();
        const gl = this.gl, context = this.context, programs = this.programs, fields = this.fields;
        const domain = scene.Domain;
        this.statistics.passes = 0;
        this.packForces(scene.Forces);
        this.packEmitters(scene.Emitters);
        this.packColliders(scene.Colliders);
        gl.disable(gl.BLEND);
        gl.disable(gl.DEPTH_TEST);

        // 1. Emission into velocity + state.
        if (this.emitterArrays.count > 0)
        {
            this.inject(0, fields.velocity, deltaTime);
            this.inject(1, fields.state, deltaTime);
        }

        // 2. Self-advection of velocity and transport of the scalar state.
        this.advectField(fields.velocity, fields.velocity.read.texture, deltaTime, domain.VelocityDissipation, domain.Advection);
        this.advectField(fields.state, fields.velocity.read.texture, deltaTime, domain.Dissipation, domain.Advection);

        // 3. Combustion + cooling.
        let uniforms = context.use(programs.thermodynamics);
        this.applyVolumeUniforms(uniforms);
        context.bindTexture('uState', fields.state.read.texture);
        gl.uniform1f(uniforms.uDeltaTime, deltaTime);
        gl.uniform1f(uniforms.uCooling, domain.Cooling);
        gl.uniform1f(uniforms.uCombustion, domain.Combustion);
        gl.uniform1f(uniforms.uAmbientTemperature, domain.AmbientTemperature);
        context.dispatch(fields.state.write);
        fields.state.swap();

        // 4. Buoyancy, gravity, force generators, collider masking.
        uniforms = context.use(programs.forces);
        this.applyVolumeUniforms(uniforms);
        this.applyForceUniforms(uniforms);
        this.applyColliderUniforms(uniforms);
        context.bindTexture('uVelocity', fields.velocity.read.texture);
        context.bindTexture('uState', fields.state.read.texture);
        gl.uniform1f(uniforms.uDeltaTime, deltaTime);
        gl.uniform1f(uniforms.uBuoyancy, domain.Buoyancy);
        gl.uniform1f(uniforms.uGravity, domain.Gravity);
        gl.uniform1f(uniforms.uAmbientTemperature, domain.AmbientTemperature);
        context.dispatch(fields.velocity.write);
        fields.velocity.swap();

        // 5. Vorticity confinement (curl is retained for visualisation).
        uniforms = context.use(programs.curl);
        this.applyVolumeUniforms(uniforms);
        context.bindTexture('uVelocity', fields.velocity.read.texture);
        context.dispatch(fields.curl);
        if (domain.Vorticity > 0)
        {
            uniforms = context.use(programs.vorticity);
            this.applyVolumeUniforms(uniforms);
            context.bindTexture('uVelocity', fields.velocity.read.texture);
            context.bindTexture('uCurl', fields.curl.texture);
            gl.uniform1f(uniforms.uDeltaTime, deltaTime);
            gl.uniform1f(uniforms.uConfinement, domain.Vorticity);
            context.dispatch(fields.velocity.write);
            fields.velocity.swap();
        }

        // 6. Pressure projection.
        uniforms = context.use(programs.divergence);
        this.applyVolumeUniforms(uniforms);
        this.applyColliderUniforms(uniforms);
        this.applyBoundaryUniform(uniforms, domain);
        context.bindTexture('uVelocity', fields.velocity.read.texture);
        context.dispatch(fields.divergence);

        context.clearTarget(fields.pressure.read);
        uniforms = context.use(programs.pressure);
        this.applyVolumeUniforms(uniforms);
        this.applyColliderUniforms(uniforms);
        this.applyBoundaryUniform(uniforms, domain);
        for (let iteration = 0; iteration < domain.PressureIterations; iteration += 1)
        {
            context.textureUnit = 0;
            context.bindTexture('uPressure', fields.pressure.read.texture);
            context.bindTexture('uDivergence', fields.divergence.texture);
            context.dispatch(fields.pressure.write);
            fields.pressure.swap();
        }

        uniforms = context.use(programs.gradient);
        this.applyVolumeUniforms(uniforms);
        this.applyColliderUniforms(uniforms);
        this.applyBoundaryUniform(uniforms, domain);
        context.bindTexture('uPressure', fields.pressure.read.texture);
        context.bindTexture('uVelocity', fields.velocity.read.texture);
        context.dispatch(fields.velocity.write);
        fields.velocity.swap();

        this.statistics.passes += 7 + domain.PressureIterations;
        this.time += deltaTime;
        this.frame += 1;
        this.statistics.stepMilliseconds = performance.now() - started;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  TRACERS
    //------------------------------------------------------------------------------------------------------------------------

    updateTracers(deltaTime, lifetime)
    {
        if (!this.fields) return;
        const gl = this.gl, context = this.context;
        gl.disable(gl.BLEND);
        // Keep the previous frame for trail segments.
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.tracers.read.framebuffer);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, this.tracerPrevious.framebuffer);
        gl.blitFramebuffer(0, 0, this.tracerSize[0], this.tracerSize[1], 0, 0, this.tracerSize[0], this.tracerSize[1], gl.COLOR_BUFFER_BIT, gl.NEAREST);
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
        const uniforms = context.use(this.programs.tracerUpdate);
        this.applyVolumeUniforms(uniforms);
        context.bindTexture('uTracers', this.tracers.read.texture);
        context.bindTexture('uVelocity', this.fields.velocity.read.texture);
        gl.uniform1f(uniforms.uDeltaTime, deltaTime);
        gl.uniform1f(uniforms.uTime, this.time);
        gl.uniform1f(uniforms.uLifetime, lifetime);
        gl.uniform1i(uniforms.uReset, this.tracersNeedReset ? 1 : 0);
        context.dispatch(this.tracers.write);
        this.tracers.swap();
        if (this.tracersNeedReset)
        {
            this.tracersNeedReset = false;
            gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.tracers.read.framebuffer);
            gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, this.tracerPrevious.framebuffer);
            gl.blitFramebuffer(0, 0, this.tracerSize[0], this.tracerSize[1], 0, 0, this.tracerSize[0], this.tracerSize[1], gl.COLOR_BUFFER_BIT, gl.NEAREST);
            gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
            gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
        }
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  DEBRIS
    //------------------------------------------------------------------------------------------------------------------------

    updateDebris(deltaTime, settings)
    {
        if (!this.fields) return;
        const gl = this.gl, context = this.context;
        gl.disable(gl.BLEND);
        const copyPrevious = () =>
        {
            gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.debrisPositions.read.framebuffer);
            gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, this.debrisPrevious.framebuffer);
            gl.blitFramebuffer(0, 0, this.debrisSize[0], this.debrisSize[1], 0, 0, this.debrisSize[0], this.debrisSize[1], gl.COLOR_BUFFER_BIT, gl.NEAREST);
            gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
            gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
        };
        copyPrevious();
        const uniforms = context.use(this.programs.debrisUpdate);
        this.applyVolumeUniforms(uniforms);
        for (const output of [0, 1])
        {
            context.textureUnit = 0;
            context.bindTexture('uPositions', this.debrisPositions.read.texture);
            context.bindTexture('uVelocities', this.debrisVelocities.read.texture);
            context.bindTexture('uVelocity', this.fields.velocity.read.texture);
            gl.uniform1i(uniforms.uOutput, output);
            gl.uniform1i(uniforms.uReset, this.debrisNeedReset ? 1 : 0);
            gl.uniform1i(uniforms.uEmitterCount, this.emitterArrays.count);
            gl.uniform4fv(uniforms.uEmitterA, this.emitterArrays.a);
            gl.uniform4fv(uniforms.uEmitterB, this.emitterArrays.b);
            gl.uniform4fv(uniforms.uEmitterE, this.emitterArrays.e);
            gl.uniform1f(uniforms.uDeltaTime, deltaTime);
            gl.uniform1f(uniforms.uTime, this.time);
            gl.uniform1f(uniforms.uLifetime, settings.Lifetime);
            gl.uniform1f(uniforms.uGravity, settings.Gravity);
            gl.uniform1f(uniforms.uDrag, settings.Drag);
            gl.uniform1f(uniforms.uBounce, settings.Bounce);
            context.dispatch(output === 0 ? this.debrisPositions.write : this.debrisVelocities.write);
        }
        this.debrisPositions.swap();
        this.debrisVelocities.swap();
        if (this.debrisNeedReset)
        {
            this.debrisNeedReset = false;
            copyPrevious();
        }
    }

    get debrisCount()
    {
        return this.debrisSize[0] * this.debrisSize[1];
    }

    get tracerCount()
    {
        return this.tracerSize[0] * this.tracerSize[1];
    }

    get domainHalfExtent()
    {
        const layout = this.layout;
        return [layout.width * this.cellSize * 0.5, layout.height * this.cellSize * 0.5, layout.depth * this.cellSize * 0.5];
    }

    /** World-space centre of the simulated window (moves as the World Grid follows its target). */
    get domainCentre()
    {
        return this.gridCentre || [0, 0, 0];
    }
}
