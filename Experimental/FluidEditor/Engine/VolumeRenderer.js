//============================================================================================================================================
//                                                        VOLUMERENDERER.JS
//               Viewport Composition — Analytic Ground / Collider / Volume Ray-March Pass, Velocity Vector Slice,
//                Scalar Heat-Map Slice, Flow Tracer Points + Trails, and Force / Emitter / Domain Gizmo Line Batches
//============================================================================================================================================

import * as Shaders from './FluidShaders.js';
import { Vec3 } from './OrbitCamera.js';
import { ForceTypeById, ColourRamps, ScalarChannels, SliceAxes, VolumeFilters } from './ForceGeneratorCatalog.js';

function hexToRgb(hex)
{
    const value = parseInt(hex.replace('#', ''), 16);
    return [((value >> 16) & 255) / 255, ((value >> 8) & 255) / 255, (value & 255) / 255];
}

export class VolumeRenderer
{
    constructor(context, solver)
    {
        this.context = context;
        this.solver = solver;
        this.gl = context.gl;
        const gl = this.gl;
        this.programs = {
            scene: context.createProgram('Scene', Shaders.FullscreenVertex, Shaders.SceneFragment),
            vectors: context.createProgram('Vectors', Shaders.VectorVertex, Shaders.ColourFragment),
            slice: context.createProgram('Slice', Shaders.SliceVertex, Shaders.SliceFragment),
            tracers: context.createProgram('Tracers', Shaders.TracerRenderVertex, Shaders.ColourFragment),
            lines: context.createProgram('Lines', Shaders.LineVertex, Shaders.ColourFragment),
        };
        this.emptyVao = gl.createVertexArray();
        this.lineVao = gl.createVertexArray();
        this.lineBuffer = gl.createBuffer();
        gl.bindVertexArray(this.lineVao);
        gl.bindBuffer(gl.ARRAY_BUFFER, this.lineBuffer);
        gl.enableVertexAttribArray(0);
        gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 28, 0);
        gl.enableVertexAttribArray(1);
        gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 28, 12);
        gl.bindVertexArray(null);
        this.lineScratch = new Float32Array(7 * 4096);
        this.lineVertexCount = 0;
        this.lightDirection = Vec3.normalize([0.45, 0.8, 0.35]);
        this.lightColour = [1.35, 1.22, 1.05];
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  FRAME
    //------------------------------------------------------------------------------------------------------------------------

    render(scene, camera, selection, deltaTime)
    {
        const gl = this.gl, context = this.context, solver = this.solver;
        if (!solver.fields) return;
        const matrices = camera.matrices();
        const canvas = context.canvas;
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, canvas.width, canvas.height);
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.BLEND);

        this.renderScene(scene, camera, matrices, selection);

        gl.enable(gl.BLEND);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        const visualizers = scene.Visualizers;
        if (visualizers.ScalarSlice.Visible) this.renderSlice(visualizers.ScalarSlice, matrices);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
        if (visualizers.VelocityVectors.Visible) this.renderVectors(visualizers.VelocityVectors, matrices);
        if (visualizers.Tracers.Visible) this.renderTracers(visualizers.Tracers, matrices, deltaTime);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        this.renderGizmos(scene, matrices, selection);
        gl.disable(gl.BLEND);
        return matrices;
    }

    renderScene(scene, camera, matrices, selection)
    {
        const gl = this.gl, context = this.context, solver = this.solver;
        const volume = scene.Visualizers.Volume;
        const uniforms = context.use(this.programs.scene);
        solver.applyVolumeUniforms(uniforms);
        solver.applyColliderUniforms(uniforms);
        context.bindTexture('uState', solver.fields.state.read.texture);
        const position = camera.position;
        gl.uniform3f(uniforms.uCameraPosition, position[0], position[1], position[2]);
        gl.uniformMatrix4fv(uniforms.uInverseViewProjection, false, matrices.inverseViewProjection);
        gl.uniform2f(uniforms.uViewport, context.canvas.width, context.canvas.height);
        gl.uniform3fv(uniforms.uLightDirection, this.lightDirection);
        gl.uniform3fv(uniforms.uLightColour, this.lightColour);
        gl.uniform3fv(uniforms.uAlbedo, hexToRgb(volume.Albedo));
        gl.uniform1f(uniforms.uDensityScale, volume.Density);
        gl.uniform1f(uniforms.uAbsorption, volume.Absorption);
        gl.uniform1f(uniforms.uFireIntensity, volume.FireIntensity);
        gl.uniform3fv(uniforms.uFireEmber, hexToRgb(volume.FireEmber));
        gl.uniform3fv(uniforms.uFireFlame, hexToRgb(volume.FireFlame));
        gl.uniform3fv(uniforms.uFireCore, hexToRgb(volume.FireCore));
        gl.uniform1f(uniforms.uFireThreshold, volume.FireThreshold);
        gl.uniform1f(uniforms.uFireSoftness, volume.FireSoftness);
        gl.uniform1f(uniforms.uFireOpacity, volume.FireOpacity);
        gl.uniform1f(uniforms.uExposure, volume.Exposure);
        gl.uniform1f(uniforms.uAnisotropy, Math.min(0.95, volume.Anisotropy));
        gl.uniform1f(uniforms.uLightIntensity, volume.LightIntensity);
        gl.uniform3fv(uniforms.uAmbientColour, hexToRgb(volume.Ambient));
        gl.uniform1i(uniforms.uSteps, Math.round(volume.Steps));
        gl.uniform1i(uniforms.uShadowSteps, Math.round(volume.ShadowSteps));
        gl.uniform1f(uniforms.uGroundHeight, -solver.domainHalfExtent[1] - 0.02);
        gl.uniform1i(uniforms.uShowVolume, volume.Visible ? 1 : 0);
        gl.uniform1i(uniforms.uVoxelPreview, VolumeFilters.indexOf(volume.Filter) === 1 ? 1 : 0);
        gl.uniform1i(uniforms.uFrame, solver.frame & 1023);
        const selectedCollider = selection ? scene.Colliders.filter((c) => c.Enabled).findIndex((c) => c.Id === selection) : -1;
        gl.uniform1i(uniforms.uSelectedCollider, selectedCollider);
        context.dispatch(null);
    }

    renderVectors(settings, matrices)
    {
        const gl = this.gl, context = this.context, solver = this.solver, layout = solver.layout;
        const axis = Math.max(0, SliceAxes.indexOf(settings.Axis));
        const axisCells = [layout.width, layout.height, layout.depth][axis];
        const slice = Math.min(axisCells - 1, Math.max(0, Math.floor(settings.Offset * axisCells)));
        const stride = Math.max(1, Math.round(settings.Stride));
        const plane = axis === 0 ? [layout.height, layout.depth] : axis === 1 ? [layout.width, layout.depth] : [layout.width, layout.height];
        const columns = Math.ceil(plane[0] / stride), rows = Math.ceil(plane[1] / stride);
        const arrows = columns * rows;
        const uniforms = context.use(this.programs.vectors);
        solver.applyVolumeUniforms(uniforms);
        context.bindTexture('uVelocity', solver.fields.velocity.read.texture);
        gl.uniformMatrix4fv(uniforms.uViewProjection, false, matrices.viewProjection);
        gl.uniform1i(uniforms.uAxis, axis);
        gl.uniform1i(uniforms.uSlice, slice);
        gl.uniform1i(uniforms.uStride, stride);
        gl.uniform1f(uniforms.uScale, settings.Scale);
        gl.uniform1f(uniforms.uMaxSpeed, settings.MaxSpeed);
        gl.uniform1i(uniforms.uRampMode, Math.max(0, ColourRamps.indexOf(settings.Ramp)));
        gl.bindVertexArray(this.emptyVao);
        gl.uniform1i(uniforms.uHeads, 0);
        gl.drawArrays(gl.LINES, 0, arrows * 2);
        gl.uniform1i(uniforms.uHeads, 1);
        gl.drawArrays(gl.POINTS, 0, arrows);
        gl.bindVertexArray(null);
    }

    renderSlice(settings, matrices)
    {
        const gl = this.gl, context = this.context, solver = this.solver;
        const half = solver.domainHalfExtent;
        const axis = Math.max(0, SliceAxes.indexOf(settings.Axis));
        const offset = (settings.Offset * 2 - 1) * half[axis];
        let corner, edgeU, edgeV;
        if (axis === 0) { corner = [offset, -half[1], -half[2]]; edgeU = [0, 2 * half[1], 0]; edgeV = [0, 0, 2 * half[2]]; }
        else if (axis === 1) { corner = [-half[0], offset, -half[2]]; edgeU = [2 * half[0], 0, 0]; edgeV = [0, 0, 2 * half[2]]; }
        else { corner = [-half[0], -half[1], offset]; edgeU = [2 * half[0], 0, 0]; edgeV = [0, 2 * half[1], 0]; }
        const uniforms = context.use(this.programs.slice);
        solver.applyVolumeUniforms(uniforms);
        solver.applyForceUniforms(uniforms);
        const fields = solver.fields;
        context.bindTexture('uVelocity', fields.velocity.read.texture);
        context.bindTexture('uState', fields.state.read.texture);
        context.bindTexture('uPressure', fields.pressure.read.texture);
        context.bindTexture('uCurl', fields.curl.texture);
        context.bindTexture('uDivergence', fields.divergence.texture);
        gl.uniformMatrix4fv(uniforms.uViewProjection, false, matrices.viewProjection);
        gl.uniform3fv(uniforms.uCorner, corner);
        gl.uniform3fv(uniforms.uEdgeU, edgeU);
        gl.uniform3fv(uniforms.uEdgeV, edgeV);
        gl.uniform1i(uniforms.uChannel, Math.max(0, ScalarChannels.indexOf(settings.Channel)));
        gl.uniform1f(uniforms.uRange, Math.max(1e-3, settings.Range));
        gl.uniform1f(uniforms.uOpacity, settings.Opacity);
        gl.uniform1i(uniforms.uRampMode, Math.max(0, ColourRamps.indexOf(settings.Ramp)));
        gl.bindVertexArray(this.emptyVao);
        gl.drawArrays(gl.TRIANGLES, 0, 6);
        gl.bindVertexArray(null);
    }

    renderTracers(settings, matrices, deltaTime)
    {
        const gl = this.gl, context = this.context, solver = this.solver;
        const count = Math.min(solver.tracerCount, Math.max(0, Math.round(settings.Count)));
        if (count === 0) return;
        const uniforms = context.use(this.programs.tracers);
        context.bindTexture('uTracers', solver.tracers.read.texture);
        context.bindTexture('uPrevious', solver.tracerPrevious.texture);
        gl.uniformMatrix4fv(uniforms.uViewProjection, false, matrices.viewProjection);
        gl.uniform2i(uniforms.uTracerSize, solver.tracerSize[0], solver.tracerSize[1]);
        gl.uniform1f(uniforms.uMaxSpeed, settings.MaxSpeed);
        gl.uniform1f(uniforms.uPointSize, settings.Size * (typeof window !== "undefined" ? Math.min(window.devicePixelRatio || 1, 1.5) : 1));
        gl.uniform1f(uniforms.uDeltaTime, Math.max(deltaTime, 1e-4));
        gl.uniform1f(uniforms.uRespawnDistance, solver.cellSize * 10);
        gl.uniform1f(uniforms.uLifetime, settings.Lifetime);
        gl.uniform1i(uniforms.uRampMode, Math.max(0, ColourRamps.indexOf(settings.Ramp)));
        gl.bindVertexArray(this.emptyVao);
        gl.uniform1i(uniforms.uTrail, 1);
        gl.drawArrays(gl.LINES, 0, count * 2);
        gl.uniform1i(uniforms.uTrail, 0);
        gl.drawArrays(gl.POINTS, 0, count);
        gl.bindVertexArray(null);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  GIZMO LINE BATCH
    //------------------------------------------------------------------------------------------------------------------------

    beginLines()
    {
        this.lineVertexCount = 0;
    }

    pushVertex(p, colour)
    {
        if ((this.lineVertexCount + 1) * 7 > this.lineScratch.length)
        {
            const grown = new Float32Array(this.lineScratch.length * 2);
            grown.set(this.lineScratch);
            this.lineScratch = grown;
        }
        const o = this.lineVertexCount * 7;
        this.lineScratch[o] = p[0]; this.lineScratch[o + 1] = p[1]; this.lineScratch[o + 2] = p[2];
        this.lineScratch[o + 3] = colour[0]; this.lineScratch[o + 4] = colour[1]; this.lineScratch[o + 5] = colour[2]; this.lineScratch[o + 6] = colour[3];
        this.lineVertexCount += 1;
    }

    line(a, b, colour) { this.pushVertex(a, colour); this.pushVertex(b, colour); }

    circle(centre, axis, radius, colour, segments = 40)
    {
        const u = Vec3.anyPerpendicular(axis);
        const v = Vec3.normalize(Vec3.cross(Vec3.normalize(axis), u));
        let previous = null;
        for (let i = 0; i <= segments; i += 1)
        {
            const angle = (i / segments) * Math.PI * 2;
            const point = Vec3.add(centre, Vec3.add(Vec3.scale(u, Math.cos(angle) * radius), Vec3.scale(v, Math.sin(angle) * radius)));
            if (previous) this.line(previous, point, colour);
            previous = point;
        }
    }

    arrow(origin, direction, length, colour)
    {
        const d = Vec3.normalize(direction);
        const tip = Vec3.add(origin, Vec3.scale(d, length));
        this.line(origin, tip, colour);
        const side = Vec3.anyPerpendicular(d);
        const side2 = Vec3.cross(d, side);
        const back = Vec3.sub(tip, Vec3.scale(d, length * 0.18));
        const wing = length * 0.07;
        for (const s of [side, Vec3.scale(side, -1), side2, Vec3.scale(side2, -1)]) this.line(tip, Vec3.add(back, Vec3.scale(s, wing)), colour);
    }

    box(centre, half, colour)
    {
        const corners = [];
        for (let i = 0; i < 8; i += 1) corners.push([centre[0] + (i & 1 ? half[0] : -half[0]), centre[1] + (i & 2 ? half[1] : -half[1]), centre[2] + (i & 4 ? half[2] : -half[2])]);
        const edges = [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]];
        for (const [a, b] of edges) this.line(corners[a], corners[b], colour);
    }

    sphereWire(centre, radius, colour)
    {
        this.circle(centre, [1, 0, 0], radius, colour, 32);
        this.circle(centre, [0, 1, 0], radius, colour, 32);
        this.circle(centre, [0, 0, 1], radius, colour, 32);
    }

    forceGizmo(force, selected, opacity)
    {
        const type = ForceTypeById[force.Type];
        const tint = hexToRgb(type.Tint);
        const alpha = (force.Enabled ? 1 : 0.35) * opacity;
        const colour = selected ? [1, 1, 1, alpha] : [tint[0], tint[1], tint[2], alpha * 0.85];
        const faint = [colour[0], colour[1], colour[2], alpha * 0.35];
        const c = force.Position, r = force.Radius, axis = Vec3.normalize(force.Direction);
        const infinite = force.Falloff === 'Constant';
        switch (type.Id)
        {
            case 'DirectionalWind':
            {
                const length = Math.min(1.6, 0.4 + Math.abs(force.Strength) * 0.05);
                for (let i = 0; i < 5; i += 1)
                {
                    const side = Vec3.anyPerpendicular(axis);
                    const side2 = Vec3.cross(axis, side);
                    const angle = (i / 5) * Math.PI * 2;
                    const spread = i === 0 ? 0 : 0.25;
                    const origin = Vec3.add(c, Vec3.add(Vec3.scale(side, Math.cos(angle) * spread), Vec3.scale(side2, Math.sin(angle) * spread)));
                    this.arrow(Vec3.sub(origin, Vec3.scale(axis, length * 0.5)), axis, length, i === 0 ? colour : faint);
                }
                if (!infinite) this.sphereWire(c, r, faint);
                break;
            }
            case 'PointAttractor':
            case 'PointRepulsor':
            {
                this.sphereWire(c, r, colour);
                const inward = type.Id === 'PointAttractor';
                for (const d of [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]])
                {
                    const start = inward ? Vec3.add(c, Vec3.scale(d, r * 0.95)) : Vec3.add(c, Vec3.scale(d, r * 0.25));
                    this.arrow(start, inward ? Vec3.scale(d, -1) : d, r * 0.55, faint);
                }
                break;
            }
            case 'VortexColumn':
            case 'AxisAttractor':
            case 'ThermalUpdraft':
            {
                const height = r * 1.6;
                for (const k of [-1, -0.33, 0.33, 1]) this.circle(Vec3.add(c, Vec3.scale(axis, k * height * 0.5)), axis, r, k === -1 || k === 1 ? colour : faint);
                this.arrow(Vec3.sub(c, Vec3.scale(axis, height * 0.5)), axis, height, colour);
                if (type.Id === 'VortexColumn')
                {
                    const u = Vec3.anyPerpendicular(axis), v = Vec3.cross(axis, u);
                    let previous = null;
                    for (let i = 0; i <= 48; i += 1)
                    {
                        const t = i / 48;
                        const angle = t * Math.PI * 4;
                        const point = Vec3.add(c, Vec3.add(Vec3.scale(axis, (t - 0.5) * height), Vec3.add(Vec3.scale(u, Math.cos(angle) * r * 0.7), Vec3.scale(v, Math.sin(angle) * r * 0.7))));
                        if (previous) this.line(previous, point, faint);
                        previous = point;
                    }
                }
                break;
            }
            case 'CurlNoiseTurbulence':
            {
                this.sphereWire(c, r, faint);
                for (let i = 0; i < 6; i += 1)
                {
                    const seed = i * 1.7;
                    const start = Vec3.add(c, [Math.sin(seed) * r * 0.5, Math.cos(seed * 1.3) * r * 0.5, Math.sin(seed * 0.7) * r * 0.5]);
                    this.arrow(start, [Math.cos(seed * 2.1), Math.sin(seed * 1.1), Math.cos(seed * 0.5)], r * 0.35, colour);
                }
                break;
            }
            case 'DragBaffle':
            {
                this.sphereWire(c, r, colour);
                this.box(c, [r * 0.5, r * 0.5, r * 0.5], faint);
                break;
            }
            case 'RadialBurst':
            {
                this.sphereWire(c, r, faint);
                this.sphereWire(c, r * 0.35, colour);
                for (const d of [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1], [0.7, 0.7, 0], [-0.7, 0, 0.7]]) this.arrow(Vec3.add(c, Vec3.scale(d, r * 0.4)), d, r * 0.55, colour);
                break;
            }
            case 'VortexRing':
            {
                this.circle(c, axis, r, colour, 48);
                const tube = Math.max(0.05, force.Frequency);
                const u = Vec3.anyPerpendicular(axis), v = Vec3.cross(axis, u);
                for (let i = 0; i < 8; i += 1)
                {
                    const angle = (i / 8) * Math.PI * 2;
                    const radialDirection = Vec3.add(Vec3.scale(u, Math.cos(angle)), Vec3.scale(v, Math.sin(angle)));
                    const ringPoint = Vec3.add(c, Vec3.scale(radialDirection, r));
                    const ringTangent = Vec3.cross(axis, radialDirection);
                    this.circle(ringPoint, ringTangent, tube, faint, 16);
                }
                this.arrow(c, axis, r * 0.9, colour);
                break;
            }
            default:
                this.sphereWire(c, r, colour);
        }
    }

    emitterGizmo(emitter, selected, opacity)
    {
        const alpha = (emitter.Enabled ? 1 : 0.35) * opacity;
        const colour = selected ? [1, 1, 1, alpha] : [1.0, 0.62, 0.25, alpha * 0.9];
        const faint = [colour[0], colour[1], colour[2], alpha * 0.35];
        const c = emitter.Position, r = emitter.Radius;
        if (emitter.Shape === 'Sphere') this.sphereWire(c, r, colour);
        else if (emitter.Shape === 'Disc')
        {
            this.circle(c, emitter.Orientation, r, colour, 40);
            this.circle(c, emitter.Orientation, r * 0.5, faint, 24);
        }
        else this.box(c, [r, r, r], colour);
        const speed = Vec3.length(emitter.Velocity);
        if (speed > 1e-3) this.arrow(c, emitter.Velocity, Math.min(1.2, 0.25 + speed * 0.2), colour);
    }

    renderGizmos(scene, matrices, selection)
    {
        const gl = this.gl, context = this.context, solver = this.solver;
        const gizmos = scene.Visualizers.ForceGizmos;
        this.beginLines();
        const half = solver.domainHalfExtent;
        this.box([0, 0, 0], half, [0.62, 0.66, 0.74, selection === 'domain' ? 0.95 : 0.45]);
        if (gizmos.Visible)
        {
            for (const force of scene.Forces) if (force.Visible) this.forceGizmo(force, force.Id === selection, gizmos.Opacity);
            for (const emitter of scene.Emitters) if (emitter.Visible) this.emitterGizmo(emitter, emitter.Id === selection, gizmos.Opacity);
            for (const collider of scene.Colliders) if (collider.Visible && collider.Id === selection) this.sphereWire(collider.Position, collider.Radius * 1.04, [1, 1, 1, 0.8]);
        }
        if (this.lineVertexCount === 0) return;
        const uniforms = context.use(this.programs.lines);
        gl.uniformMatrix4fv(uniforms.uViewProjection, false, matrices.viewProjection);
        gl.bindVertexArray(this.lineVao);
        gl.bindBuffer(gl.ARRAY_BUFFER, this.lineBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, this.lineScratch.subarray(0, this.lineVertexCount * 7), gl.DYNAMIC_DRAW);
        gl.drawArrays(gl.LINES, 0, this.lineVertexCount);
        gl.bindVertexArray(null);
    }
}
