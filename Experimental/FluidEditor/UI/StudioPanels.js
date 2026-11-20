//============================================================================================================================================
//                                                          STUDIOPANELS.JS
//            Project-Zero Fluid Studio Chrome — Outliner Tree, Property Inspector (Hero · Metrics · Status Tiles · Pill
//           Controls), Top Control-Centre Notch, Viewport Toolbar + Transport + Legend, Asset Browser Drawer, Status Bar
//============================================================================================================================================

import { icon } from './StudioIcons.js';
import {
    ForceGeneratorTypes, ForceTypeById, FalloffCurves, EmitterShapes, ColourRamps, SliceAxes, ScalarChannels, AdvectionSchemes, BoundaryModes,
    ScenePresets, RampStops, MaximumForceGenerators, SmokeLooks, FireLooks, ResolutionPresets, VolumeFilters,
} from '../Engine/ForceGeneratorCatalog.js';

const FalloffLabels = { SmoothStep: 'Smooth', Linear: 'Linear', InverseSquare: 'Inverse²', Constant: 'Infinite' };
const BoundaryLabels = { ClosedBox: 'Closed Box', OpenTop: 'Open Top', OpenAll: 'Open All' };

function element(tag, className, html)
{
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (html !== undefined) node.innerHTML = html;
    return node;
}

function formatNumber(value, digits = 2)
{
    return Number(value).toFixed(digits).replace(/\.?0+$/, (match) => (match.startsWith('.') ? '' : match));
}

function formatCells(count)
{
    return count >= 1e6 ? (count / 1e6).toFixed(2) + ' M' : count >= 1e3 ? (count / 1e3).toFixed(0) + ' k' : String(count);
}

function rampGradient(name)
{
    return `linear-gradient(90deg, ${(RampStops[name] || RampStops.Turbo).join(', ')})`;
}

function nodeIcon(node)
{
    if (node.Kind === 'Force') return ForceTypeById[node.Type].Icon;
    if (node.Kind === 'Emitter') return node.Fuel > 0 ? 'flame' : 'smoke';
    if (node.Kind === 'Collider') return 'collider';
    if (node.Kind === 'Domain') return 'domain';
    return { 'vis-volume': 'volume', 'vis-vectors': 'vectors', 'vis-slice': 'slice', 'vis-tracers': 'tracers', 'vis-gizmos': 'gizmo' }[node.Id] || 'cube';
}

function nodeTint(node)
{
    if (node.Kind === 'Force') return ForceTypeById[node.Type].Tint;
    if (node.Kind === 'Emitter') return node.Fuel > 0 ? '#f97316' : '#cbd5e1';
    if (node.Kind === 'Collider') return '#94a3b8';
    if (node.Kind === 'Domain') return '#e2e8f0';
    return '#38bdf8';
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  STUDIO UI
//------------------------------------------------------------------------------------------------------------------------

export function mountStudioUI({ app, refs })
{
    const state = { searchTerm: '', collapsed: new Set(), drawerOpen: false, drawerTab: 'presets', notchOpen: false, mode: 'sim', snapshot: null, metrics: null };

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  OUTLINER
    //------------------------------------------------------------------------------------------------------------------------

    function renderOutliner(snapshot)
    {
        const { scene, selection } = snapshot;
        const host = refs.outliner;
        host.innerHTML = '';
        const nodes = [scene.Domain, ...scene.Emitters, ...scene.Forces, ...scene.Colliders, ...Object.values(scene.Visualizers)];
        const visible = nodes.filter((node) => node.Visible !== false).length;
        const hidden = nodes.length - visible;

        const heading = element('div', 'panel-heading', `<h1>Outliner</h1><span class="subtitle">${scene.Name} · ${nodes.length} nodes</span>`);
        const headingButton = element('button', 'icon-button', icon('sliders', 14));
        headingButton.title = 'Outliner options';
        headingButton.addEventListener('click', () => toggleDrawer(true, 'forces'));
        heading.appendChild(headingButton);
        host.appendChild(heading);

        host.appendChild(element('div', 'counter-row',
            `<div class="counter-card"><div class="label"><span class="status-dot">${icon('check', 9, 3)}</span>Visible</div><div class="value">${visible}</div></div>
             <div class="counter-card"><div class="label"><span class="status-dot off">${icon('eyeOff', 9)}</span>Hidden</div><div class="value">${hidden}</div></div>`));

        const searchRow = element('div', 'search-row');
        const searchPill = element('div', 'search-pill', icon('search', 13));
        const searchInput = document.createElement('input');
        searchInput.placeholder = 'Search  Ctrl+F';
        searchInput.value = state.searchTerm;
        searchInput.addEventListener('input', () => { state.searchTerm = searchInput.value.toLowerCase(); renderTree(treeHost, snapshot); });
        searchPill.appendChild(searchInput);
        searchRow.appendChild(searchPill);
        const filterButton = element('button', 'filter-pill', `${icon('filter', 12)} Filter ${icon('chevron', 10)}`);
        filterButton.addEventListener('click', () => { state.searchTerm = ''; searchInput.value = ''; renderTree(treeHost, snapshot); });
        searchRow.appendChild(filterButton);
        host.appendChild(searchRow);

        const treeHost = element('div', 'tree');
        host.appendChild(treeHost);
        renderTree(treeHost, snapshot);
    }

    function renderTree(treeHost, snapshot)
    {
        const { scene, selection } = snapshot;
        treeHost.innerHTML = '';
        const collections = [
            { key: 'domain', title: 'Domain', icon: 'domain', items: [scene.Domain], subtitle: `${scene.Domain.Resolution.join(' × ')} cells` },
            { key: 'emitters', title: 'Emitters', icon: 'flame', items: scene.Emitters, subtitle: `${scene.Emitters.length} items`, add: () => app.addEmitter('Sphere') },
            { key: 'forces', title: 'Force Generators', icon: 'wind', items: scene.Forces, subtitle: `${scene.Forces.length} of ${MaximumForceGenerators} slots`, add: () => toggleDrawer(true, 'forces') },
            { key: 'colliders', title: 'Colliders', icon: 'collider', items: scene.Colliders, subtitle: `${scene.Colliders.length} items`, add: () => app.addCollider() },
            { key: 'visualizers', title: 'Visualizers', icon: 'vectors', items: Object.values(scene.Visualizers), subtitle: 'Flow diagnostics' },
        ];
        for (const collection of collections)
        {
            const items = collection.items.filter((node) => !state.searchTerm || node.Name.toLowerCase().includes(state.searchTerm) || (node.Type || '').toLowerCase().includes(state.searchTerm));
            if (state.searchTerm && items.length === 0) continue;
            const row = element('div', 'tree-collection' + (state.collapsed.has(collection.key) ? ' collapsed' : ''),
                `${icon('chevron', 10)}<span class="collection-icon">${icon(collection.icon, 14)}</span><span class="meta"><strong>${collection.title}</strong><span>${collection.subtitle}</span></span>`);
            row.querySelector('svg').classList.add('chevron');
            row.addEventListener('click', () => { if (state.collapsed.has(collection.key)) state.collapsed.delete(collection.key); else state.collapsed.add(collection.key); renderTree(treeHost, snapshot); });
            if (collection.add)
            {
                const add = element('button', 'add-inline', icon('plus', 12));
                add.title = `Add to ${collection.title}`;
                add.addEventListener('click', (event) => { event.stopPropagation(); collection.add(); });
                row.appendChild(add);
            }
            treeHost.appendChild(row);
            const list = element('div', 'tree-items');
            for (const node of items) list.appendChild(renderTreeItem(node, selection));
            treeHost.appendChild(list);
        }
    }

    function describeNode(node)
    {
        switch (node.Kind)
        {
            case 'Domain': return `${node.Size.map((extent) => extent.toFixed(1)).join(' × ')} m · ${(node.CellSize * 100).toFixed(1)} cm voxels · ${BoundaryLabels[node.Boundary]}`;
            case 'Force': return `${ForceTypeById[node.Type].Name} · ${formatNumber(node.Strength, 1)} · ${FalloffLabels[node.Falloff]}`;
            case 'Emitter': return `${node.Shape} · ρ ${formatNumber(node.Density, 1)} · T ${formatNumber(node.Temperature, 1)}${node.Fuel > 0 ? ' · fuel' : ''}`;
            case 'Collider': return `Sphere · r ${formatNumber(node.Radius, 2)} m`;
            default:
                if (node.Id === 'vis-vectors') return `${node.Axis} slice · stride ${node.Stride}`;
                if (node.Id === 'vis-slice') return `${node.Channel} · ${node.Axis} ${Math.round(node.Offset * 100)}%`;
                if (node.Id === 'vis-tracers') return `${node.Count} tracers · ${formatNumber(node.Lifetime, 1)} s`;
                if (node.Id === 'vis-volume') return `${node.Steps} steps · ${node.ShadowSteps} shadow`;
                return 'Force + emitter overlays';
        }
    }

    function renderTreeItem(node, selection)
    {
        const enabled = node.Enabled !== false;
        const item = element('div', 'tree-item' + (node.Id === selection ? ' selected' : '') + (enabled ? '' : ' disabled'));
        item.innerHTML = `<span class="item-icon" style="color:${nodeTint(node)}">${icon(nodeIcon(node), 15)}</span><span class="item-meta"><strong>${node.Name}</strong><span>${describeNode(node)}</span></span>`;
        item.addEventListener('click', () => app.select(node.Id));
        if (node.Kind !== 'Domain' && node.Kind !== 'Visualizer')
        {
            const status = element('button', 'status-dot' + (enabled ? '' : ' off'), icon(enabled ? 'check' : 'power', 9, 3));
            status.title = enabled ? 'Enabled — click to disable' : 'Disabled — click to enable';
            status.addEventListener('click', (event) => { event.stopPropagation(); app.updateNode(node.Id, 'Enabled', !enabled); });
            item.appendChild(status);
        }
        if (node.Kind !== 'Domain')
        {
            const eye = element('button', 'eye-button' + (node.Visible ? ' on' : ''), icon(node.Visible ? 'eye' : 'eyeOff', 13));
            eye.title = node.Visible ? 'Visible in viewport' : 'Hidden in viewport';
            eye.addEventListener('click', (event) => { event.stopPropagation(); app.updateNode(node.Id, 'Visible', !node.Visible); });
            item.appendChild(eye);
        }
        return item;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  INSPECTOR CONTROLS
    //------------------------------------------------------------------------------------------------------------------------

    function slider(node, key, label, min, max, step, unit = '', digits = 2)
    {
        const control = element('div', 'control');
        const head = element('div', 'control-head', `<span>${label}<span class="unit">${unit}</span></span>`);
        const number = document.createElement('input');
        number.type = 'number'; number.step = String(step); number.value = formatNumber(node[key], digits);
        head.appendChild(number);
        const range = document.createElement('input');
        range.type = 'range'; range.min = String(min); range.max = String(max); range.step = String(step); range.value = String(node[key]);
        const paint = () => range.style.setProperty('--fill', `${((Number(range.value) - min) / (max - min)) * 100}%`);
        paint();
        range.addEventListener('input', () => { number.value = formatNumber(range.value, digits); paint(); app.updateNode(node.Id, key, Number(range.value)); });
        number.addEventListener('change', () => { const value = Number(number.value); if (Number.isFinite(value)) { range.value = String(Math.max(min, Math.min(max, value))); paint(); app.updateNode(node.Id, key, value); } });
        control.appendChild(head);
        control.appendChild(range);
        return control;
    }

    function vector(node, key, label, step = 0.05, integer = false)
    {
        const control = element('div', 'control');
        control.appendChild(element('div', 'control-head', `<span>${label}</span>`));
        const row = element('div', 'vector-row');
        ['x', 'y', 'z'].forEach((axis, index) =>
        {
            const wrap = element('label', axis, `<span>${axis.toUpperCase()}</span>`);
            const input = document.createElement('input');
            input.type = 'number'; input.step = String(step); input.value = integer ? String(node[key][index]) : formatNumber(node[key][index], 2);
            input.addEventListener('change', () =>
            {
                const value = Number(input.value);
                if (!Number.isFinite(value)) return;
                const next = [...node[key]];
                next[index] = integer ? Math.round(value) : value;
                app.updateNode(node.Id, key, next);
            });
            wrap.appendChild(input);
            row.appendChild(wrap);
        });
        control.appendChild(row);
        return control;
    }

    function choice(node, key, label, options, labels = {})
    {
        const control = element('div', 'control');
        control.appendChild(element('div', 'control-head', `<span>${label}</span>`));
        const row = element('div', 'choice-row');
        for (const option of options)
        {
            const button = element('button', node[key] === option ? 'active' : '', labels[option] || option);
            button.addEventListener('click', () => app.updateNode(node.Id, key, option));
            row.appendChild(button);
        }
        control.appendChild(row);
        return control;
    }

    function colour(node, key, label)
    {
        const row = element('div', 'colour-row', `<span>${label}</span>`);
        const input = document.createElement('input');
        input.type = 'color'; input.value = node[key];
        input.addEventListener('input', () => app.updateNode(node.Id, key, input.value));
        row.appendChild(input);
        return row;
    }

    function lookChips(node, label, looks, isActive)
    {
        const control = element('div', 'control');
        control.appendChild(element('div', 'control-head', `<span>${label}</span>`));
        const row = element('div', 'choice-row');
        for (const [name, look] of Object.entries(looks))
        {
            const button = element('button', isActive(look) ? 'active' : '', name);
            button.addEventListener('click', () => app.applyLook(node.Id, look));
            row.appendChild(button);
        }
        control.appendChild(row);
        return control;
    }

    function card(title, hint, controls)
    {
        const wrap = element('div', 'property-card', `<h3>${title}${hint ? `<span>${hint}</span>` : ''}</h3>`);
        for (const control of controls) if (control) wrap.appendChild(control);
        return wrap;
    }

    function statusTile(label, stateText, iconName, on, onClick, warn = false)
    {
        const tile = element('button', 'status-tile' + (on ? ' on' : '') + (warn ? ' warn' : ''), `<span class="tile-icon">${icon(iconName, 16)}</span><span class="tile-label">${label}</span><span class="tile-state">${stateText}</span>`);
        if (onClick) tile.addEventListener('click', onClick); else tile.disabled = true;
        return tile;
    }

    function metric(value, unit, label)
    {
        return `<div class="metric-card"><div class="value">${value}${unit ? `<small>${unit}</small>` : ''}</div><div class="label">${label}</div></div>`;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  INSPECTOR
    //------------------------------------------------------------------------------------------------------------------------

    function renderInspector(snapshot)
    {
        const host = refs.inspector;
        host.innerHTML = '';
        const node = snapshot.node || snapshot.scene.Domain;
        const kindLabel = { Domain: 'Gas Domain', Force: 'Force Generator', Emitter: 'Emitter', Collider: 'Collider', Visualizer: 'Visualizer' }[node.Kind];

        const breadcrumb = element('div', 'inspector-breadcrumb', `<span>Inspector / ${kindLabel}</span>`);
        const notes = element('button', 'pill', `${icon('plus', 11)} Add notes`);
        notes.addEventListener('click', () => toggleDrawer(true, 'presets'));
        breadcrumb.appendChild(notes);
        host.appendChild(breadcrumb);

        const title = element('div', 'inspector-title');
        if (node.Kind === 'Domain' || node.Kind === 'Visualizer') title.innerHTML = `<h1>${node.Name}</h1>`;
        else
        {
            const rename = document.createElement('input');
            rename.className = 'rename'; rename.value = node.Name; rename.title = 'Rename';
            rename.addEventListener('change', () => app.updateNode(node.Id, 'Name', rename.value.trim() || node.Name));
            title.appendChild(rename);
        }
        title.appendChild(element('div', 'kind', node.Kind === 'Force' ? ForceTypeById[node.Type].Summary : kindLabel));
        host.appendChild(title);

        if (node.Kind === 'Domain') renderDomainInspector(host, node, snapshot);
        else if (node.Kind === 'Force') renderForceInspector(host, node, snapshot);
        else if (node.Kind === 'Emitter') renderEmitterInspector(host, node, snapshot);
        else if (node.Kind === 'Collider') renderColliderInspector(host, node, snapshot);
        else renderVisualizerInspector(host, node, snapshot);
    }

    function hero(tint, caption, sub, corner, glyph)
    {
        return element('div', 'hero',
            `<div style="position:absolute;inset:0;background:linear-gradient(135deg, ${tint}66 0%, ${tint}22 45%, #101010 100%)"></div>
             <div style="position:absolute;inset:0;background:radial-gradient(circle at 80% 50%, ${tint}55, transparent 55%)"></div>
             <span class="hero-glyph">${icon(glyph, 56, 1)}</span>
             <div class="hero-caption"><strong>${caption}</strong><span>${sub}</span></div>
             <div class="hero-corner">${corner}</div>`);
    }

    function renderDomainInspector(host, domain, snapshot)
    {
        const metrics = state.metrics || {};
        const size = domain.Resolution.map((cells) => (cells * domain.CellSize).toFixed(1));
        host.appendChild(hero('#64748b', `${domain.Resolution.join(' × ')}`, `${size.join(' × ')} m · ${BoundaryLabels[domain.Boundary]}`, `${formatCells(domain.Resolution[0] * domain.Resolution[1] * domain.Resolution[2])} cells`, 'domain'));
        host.appendChild(element('div', 'metric-grid',
            metric(formatCells(metrics.cells || 0), '', 'Cells') +
            metric((domain.CellSize * 100).toFixed(1), 'cm', 'Cell size') +
            metric((metrics.stepMilliseconds || 0).toFixed(1), 'ms', 'Solver step') +
            metric(metrics.passes || 0, '', 'GPU passes')));
        const status = element('div', 'status-grid');
        status.appendChild(statusTile(snapshot.playing ? 'Simulating' : 'Paused', snapshot.playing ? 'LIVE' : 'HELD', snapshot.playing ? 'pause' : 'play', snapshot.playing, () => app.togglePlay()));
        status.appendChild(statusTile('MacCormack', domain.Advection === 'MacCormack' ? '2ND ORDER' : 'SEMI-LAGRANGIAN', 'vectors', domain.Advection === 'MacCormack', () => app.updateNode(domain.Id, 'Advection', domain.Advection === 'MacCormack' ? AdvectionSchemes[0] : AdvectionSchemes[1])));
        host.appendChild(status);
        host.appendChild(element('div', 'section-head', '<span>Properties</span><span>Grid, dynamics & solver</span>'));
        const resolutionChips = element('div', 'control');
        resolutionChips.appendChild(element('div', 'control-head', '<span>Quality preset</span>'));
        const chipRow = element('div', 'choice-row');
        for (const [name, cells] of Object.entries(ResolutionPresets))
        {
            const button = element('button', domain.MaxCells === cells ? 'active' : '', `${name} <span style="opacity:.55;margin-left:4px">${cells}</span>`);
            button.addEventListener('click', () => app.updateNode(domain.Id, 'MaxCells', cells));
            chipRow.appendChild(button);
        }
        resolutionChips.appendChild(chipRow);
        host.appendChild(card('Grid', `${domain.Resolution.join(' × ')} = ${formatCells(domain.Resolution[0] * domain.Resolution[1] * domain.Resolution[2])}`, [
            vector(domain, 'Size', 'World size (m)', 0.5),
            slider(domain, 'MaxCells', 'Voxel resolution (longest axis)', 16, 160, 8, 'cells', 0),
            resolutionChips,
            element('p', 'note', `Voxel ${(domain.CellSize * 100).toFixed(1)} cm — lower resolution = coarser, blockier gas and a faster solve; higher resolution = finer detail at a higher GPU cost. World size stays fixed while you change it.`),
            choice(domain, 'Boundary', 'Boundary', BoundaryModes, BoundaryLabels),
            element('p', 'note', 'Open faces let gas flow out of the domain (a sponge band fades it near the edge); closed walls contain it.'),
        ]));
        host.appendChild(card('Dynamics', 'Per second', [
            slider(domain, 'Buoyancy', 'Thermal buoyancy', 0, 4, 0.05),
            slider(domain, 'Gravity', 'Smoke weight', 0, 2, 0.05),
            slider(domain, 'Vorticity', 'Vorticity confinement', 0, 2, 0.01),
            slider(domain, 'Cooling', 'Cooling rate', 0, 3, 0.05),
            slider(domain, 'Combustion', 'Combustion rate', 0, 3, 0.05),
            slider(domain, 'AmbientTemperature', 'Ambient temperature', -1, 2, 0.05),
        ]));
        host.appendChild(card('Solver', 'Stability', [
            slider(domain, 'PressureIterations', 'Pressure iterations', 4, 80, 1, '', 0),
            slider(domain, 'Dissipation', 'Density retention', 0.95, 1, 0.001, '', 3),
            slider(domain, 'VelocityDissipation', 'Velocity retention', 0.95, 1, 0.001, '', 3),
            slider(domain, 'TimeScale', 'Time scale', 0.1, 3, 0.05, '×'),
        ]));
        const actions = element('div', 'action-row');
        const reset = element('button', '', `${icon('reset', 12)} Reset fields`);
        reset.addEventListener('click', () => app.reset());
        const frame = element('button', '', `${icon('frame', 12)} Frame domain`);
        frame.addEventListener('click', () => app.frameDomain());
        actions.append(reset, frame);
        host.appendChild(actions);
    }

    function renderForceInspector(host, force, snapshot)
    {
        const type = ForceTypeById[force.Type];
        const slotIndex = snapshot.scene.Forces.filter((candidate) => candidate.Enabled).indexOf(force);
        host.appendChild(hero(type.Tint, type.Name, `${FalloffLabels[force.Falloff]} falloff${slotIndex >= 0 ? ` · slot ${slotIndex + 1}` : ' · not evaluated'}`, `${formatNumber(force.Strength, 1)} m/s²`, type.Icon));
        host.appendChild(element('div', 'metric-grid',
            metric(formatNumber(force.Strength, 1), 'm/s²', 'Strength') +
            metric(force.Falloff === 'Constant' ? '∞' : formatNumber(force.Radius, 2), force.Falloff === 'Constant' ? '' : 'm', 'Reach') +
            metric(force.Position.map((value) => formatNumber(value, 1)).join(', '), '', 'Position') +
            metric(force.Direction.map((value) => formatNumber(value, 1)).join(', '), '', 'Axis')));
        const status = element('div', 'status-grid');
        status.appendChild(statusTile('Enabled', force.Enabled ? 'ON' : 'OFF', 'power', force.Enabled, () => app.updateNode(force.Id, 'Enabled', !force.Enabled)));
        status.appendChild(statusTile('Gizmo', force.Visible ? 'SHOWN' : 'HIDDEN', 'gizmo', force.Visible, () => app.updateNode(force.Id, 'Visible', !force.Visible)));
        host.appendChild(status);
        host.appendChild(element('div', 'section-head', '<span>Properties</span><span>Force, reach & placement</span>'));

        const typeControl = element('div', 'control');
        typeControl.appendChild(element('div', 'control-head', '<span>Generator type</span>'));
        const typeSelect = document.createElement('select');
        for (const candidate of ForceGeneratorTypes)
        {
            const option = document.createElement('option');
            option.value = candidate.Id; option.textContent = candidate.Name; option.selected = candidate.Id === force.Type;
            typeSelect.appendChild(option);
        }
        typeSelect.addEventListener('change', () =>
        {
            const next = ForceTypeById[typeSelect.value];
            app.updateNode(force.Id, 'Type', next.Id);
            if (force.Name === type.Name) app.updateNode(force.Id, 'Name', next.Name);
        });
        typeControl.appendChild(typeSelect);

        host.appendChild(card('Force', type.Id, [
            typeControl,
            slider(force, 'Strength', 'Strength', -60, 60, 0.5, 'm/s²', 1),
            slider(force, 'Radius', 'Reach radius', 0.1, 4, 0.05, 'm'),
            choice(force, 'Falloff', 'Falloff envelope', FalloffCurves, FalloffLabels),
        ]));
        const extras = [];
        if (type.Labels.Frequency) extras.push(slider(force, 'Frequency', type.Labels.Frequency, 0, type.Id === 'RadialBurst' ? 6 : 4, 0.05));
        if (type.Labels.Secondary) extras.push(slider(force, 'Secondary', type.Labels.Secondary, 0, type.Id === 'VortexColumn' ? 8 : 2, 0.05));
        if (extras.length) host.appendChild(card('Modulation', 'Type-specific', extras));
        host.appendChild(card('Placement', 'Drag gizmo in viewport', [
            vector(force, 'Position', 'Position (m)'),
            vector(force, 'Direction', 'Direction / axis', 0.1),
        ]));
        host.appendChild(nodeActions(force));
    }

    function renderEmitterInspector(host, emitter)
    {
        const burning = emitter.Fuel > 0;
        host.appendChild(hero(burning ? '#f97316' : '#cbd5e1', emitter.Shape + ' source', burning ? 'Combusting fuel · soot + heat' : 'Smoke + heat injection', `ρ ${formatNumber(emitter.Density, 1)} · T ${formatNumber(emitter.Temperature, 1)}`, burning ? 'flame' : 'smoke'));
        host.appendChild(element('div', 'metric-grid',
            metric(formatNumber(emitter.Radius, 2), 'm', 'Radius') +
            metric(formatNumber(Math.hypot(...emitter.Velocity), 1), 'm/s', 'Exit speed') +
            metric(formatNumber(emitter.Temperature, 1), '', 'Temperature') +
            metric(formatNumber(emitter.Fuel, 2), '', 'Fuel')));
        const status = element('div', 'status-grid');
        status.appendChild(statusTile('Enabled', emitter.Enabled ? 'ON' : 'OFF', 'power', emitter.Enabled, () => app.updateNode(emitter.Id, 'Enabled', !emitter.Enabled)));
        status.appendChild(statusTile('Pulsed', emitter.Pulse > 0 ? `${formatNumber(emitter.Pulse, 1)} s` : 'CONTINUOUS', 'burst', emitter.Pulse > 0, () => app.updateNode(emitter.Id, 'Pulse', emitter.Pulse > 0 ? 0 : 1.5)));
        host.appendChild(status);
        host.appendChild(element('div', 'section-head', '<span>Properties</span><span>Shape, injection & motion</span>'));
        host.appendChild(card('Shape', '', [
            choice(emitter, 'Shape', 'Emitter shape', EmitterShapes),
            slider(emitter, 'Radius', 'Radius', 0.05, 2, 0.01, 'm'),
            vector(emitter, 'Position', 'Position (m)'),
            emitter.Shape === 'Disc' ? vector(emitter, 'Orientation', 'Disc normal', 0.1) : null,
        ]));
        host.appendChild(card('Injection', 'Per second', [
            slider(emitter, 'Density', 'Smoke density', 0, 3, 0.05),
            slider(emitter, 'Temperature', 'Temperature', 0, 8, 0.1),
            slider(emitter, 'Fuel', 'Fuel', 0, 2, 0.05),
            slider(emitter, 'Jitter', 'Noise jitter', 0, 1, 0.02),
            slider(emitter, 'Pulse', 'Pulse period', 0, 5, 0.1, 's'),
        ]));
        host.appendChild(card('Exit Velocity', 'm/s', [vector(emitter, 'Velocity', 'Velocity', 0.1)]));
        host.appendChild(nodeActions(emitter));
    }

    function renderColliderInspector(host, collider)
    {
        host.appendChild(hero('#94a3b8', 'Sphere collider', 'No-slip solid boundary', `r ${formatNumber(collider.Radius, 2)} m`, 'collider'));
        host.appendChild(element('div', 'metric-grid',
            metric(formatNumber(collider.Radius, 2), 'm', 'Radius') +
            metric(collider.Position.map((value) => formatNumber(value, 1)).join(', '), '', 'Centre')));
        const status = element('div', 'status-grid');
        status.appendChild(statusTile('Enabled', collider.Enabled ? 'ON' : 'OFF', 'power', collider.Enabled, () => app.updateNode(collider.Id, 'Enabled', !collider.Enabled)));
        status.appendChild(statusTile('Visible', collider.Visible ? 'SHOWN' : 'HIDDEN', 'eye', collider.Visible, () => app.updateNode(collider.Id, 'Visible', !collider.Visible)));
        host.appendChild(status);
        host.appendChild(element('div', 'section-head', '<span>Properties</span><span>Placement</span>'));
        host.appendChild(card('Solid', 'Pressure boundary', [
            slider(collider, 'Radius', 'Radius', 0.05, 2, 0.01, 'm'),
            vector(collider, 'Position', 'Position (m)'),
        ]));
        host.appendChild(nodeActions(collider));
    }

    function renderVisualizerInspector(host, visualizer)
    {
        host.appendChild(hero('#38bdf8', visualizer.Name, 'Viewport diagnostic overlay', visualizer.Visible ? 'Shown' : 'Hidden', nodeIcon(visualizer)));
        const status = element('div', 'status-grid');
        status.appendChild(statusTile('Visible', visualizer.Visible ? 'ON' : 'OFF', 'eye', visualizer.Visible, () => app.updateNode(visualizer.Id, 'Visible', !visualizer.Visible)));
        if (visualizer.Ramp)
        {
            const legend = element('div', 'status-tile', `<span class="tile-icon" style="width:100%;height:10px;border-radius:5px;background:${rampGradient(visualizer.Ramp)}"></span><span class="tile-label">${visualizer.Ramp}</span><span class="tile-state">Colour ramp</span>`);
            legend.disabled = true;
            status.appendChild(legend);
        }
        host.appendChild(status);
        host.appendChild(element('div', 'section-head', '<span>Properties</span><span>Overlay settings</span>'));
        if (visualizer.Id === 'vis-volume')
        {
            host.appendChild(card('Fire', 'Incandescence', [
                lookChips(visualizer, 'Fire preset', FireLooks, (look) => visualizer.FireEmber === look.FireEmber),
                colour(visualizer, 'FireEmber', 'Ember (cool edge)'),
                colour(visualizer, 'FireFlame', 'Flame body'),
                colour(visualizer, 'FireCore', 'Hot core'),
                slider(visualizer, 'FireIntensity', 'Fire emission', 0, 12, 0.1),
                slider(visualizer, 'FireThreshold', 'Ignition temperature', 0.3, 3, 0.05),
                slider(visualizer, 'FireSoftness', 'Temperature span', 0.5, 8, 0.1),
                slider(visualizer, 'FireOpacity', 'Flame opacity', 0, 4, 0.05),
            ]));
            host.appendChild(card('Smoke', 'Scattering', [
                lookChips(visualizer, 'Smoke preset', SmokeLooks, (look) => visualizer.Albedo === look.Albedo),
                colour(visualizer, 'Albedo', 'Smoke albedo'),
                slider(visualizer, 'Density', 'Density scale', 0, 4, 0.05),
                slider(visualizer, 'Absorption', 'Extinction', 0.2, 12, 0.1, '/m'),
                slider(visualizer, 'Anisotropy', 'Forward scatter', 0, 0.9, 0.01),
            ]));
            host.appendChild(card('Lighting', 'Sun + sky', [
                choice(visualizer, 'Filter', 'Volume filtering', VolumeFilters, { Smooth: 'Smooth (trilinear)', Voxel: 'Voxel (show cells)' }),
                slider(visualizer, 'LightIntensity', 'Sun intensity', 0, 4, 0.05),
                colour(visualizer, 'Ambient', 'Ambient sky'),
                slider(visualizer, 'Exposure', 'Exposure', 0.2, 4, 0.05),
                slider(visualizer, 'Steps', 'March steps', 24, 256, 4, '', 0),
                slider(visualizer, 'ShadowSteps', 'Shadow steps', 0, 16, 1, '', 0),
            ]));
        }
        else if (visualizer.Id === 'vis-vectors')
        {
            host.appendChild(card('Vector Slice', 'Velocity arrows', [
                choice(visualizer, 'Axis', 'Slice axis', SliceAxes),
                slider(visualizer, 'Offset', 'Slice position', 0, 1, 0.01),
                slider(visualizer, 'Stride', 'Cell stride', 1, 8, 1, '', 0),
                slider(visualizer, 'Scale', 'Arrow scale', 0.02, 1.5, 0.01, 's'),
                slider(visualizer, 'MaxSpeed', 'Ramp max speed', 0.5, 20, 0.1, 'm/s'),
                choice(visualizer, 'Ramp', 'Colour ramp', ColourRamps),
            ]));
        }
        else if (visualizer.Id === 'vis-slice')
        {
            host.appendChild(card('Scalar Slice', 'Heat-map', [
                choice(visualizer, 'Channel', 'Channel', ScalarChannels, { ForceMagnitude: 'Force |F|' }),
                choice(visualizer, 'Axis', 'Slice axis', SliceAxes),
                slider(visualizer, 'Offset', 'Slice position', 0, 1, 0.01),
                slider(visualizer, 'Range', 'Ramp range', 0.1, 40, 0.1),
                slider(visualizer, 'Opacity', 'Opacity', 0, 1, 0.02),
                choice(visualizer, 'Ramp', 'Colour ramp', ColourRamps),
            ]));
        }
        else if (visualizer.Id === 'vis-tracers')
        {
            host.appendChild(card('Flow Tracers', 'GPU particles', [
                slider(visualizer, 'Count', 'Tracer count', 256, 8192, 256, '', 0),
                slider(visualizer, 'Lifetime', 'Lifetime', 0.5, 12, 0.1, 's'),
                slider(visualizer, 'Size', 'Point size', 1, 6, 0.5, 'px'),
                slider(visualizer, 'MaxSpeed', 'Ramp max speed', 0.5, 20, 0.1, 'm/s'),
                choice(visualizer, 'Ramp', 'Colour ramp', ColourRamps),
            ]));
        }
        else
        {
            host.appendChild(card('Gizmos', 'Forces + emitters', [slider(visualizer, 'Opacity', 'Opacity', 0, 1, 0.02)]));
            host.appendChild(element('p', 'note', 'Gizmos are inspection overlays only — they never alter solver state. Click a gizmo centre to select it, drag to move (hold Shift to lock height).'));
        }
    }

    function nodeActions(node)
    {
        const actions = element('div', 'action-row');
        const duplicate = element('button', '', `${icon('copy', 12)} Duplicate`);
        duplicate.addEventListener('click', () => app.duplicateNode(node.Id));
        const remove = element('button', 'danger', `${icon('trash', 12)} Delete`);
        remove.addEventListener('click', () => app.removeNode(node.Id));
        actions.append(duplicate, remove);
        return actions;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  VIEWPORT TOOLBAR + TRANSPORT
    //------------------------------------------------------------------------------------------------------------------------

    function renderToolbar(snapshot)
    {
        const host = refs.toolbar;
        host.innerHTML = '';
        const scene = snapshot.scene;
        const rowA = element('div', 'toolbar-row');
        rowA.innerHTML = `<div class="scene-title"><span class="scene-icon">${icon('scene', 15)}</span><span class="meta"><strong>${scene.Name}</strong><span>${scene.Forces.length} forces · ${scene.Emitters.length} emitters · ${scene.Domain.Resolution.join('×')}</span></span></div><span class="chip">PREVIEW</span>`;
        const segmented = element('div', 'segmented');
        for (const [key, label, glyph] of [['edit', 'Edit', 'pencil'], ['sim', 'Sim', 'vectors'], ['play', 'Play', 'play']])
        {
            const button = element('button', state.mode === key ? 'active' : '', `${icon(glyph, 11)} ${label}`);
            button.addEventListener('click', () => setMode(key));
            segmented.appendChild(button);
        }
        rowA.appendChild(segmented);
        host.appendChild(rowA);

        const rowB = element('div', 'toolbar-row');
        const addForce = element('button', 'pill primary', `${icon('plus', 12)} Add Force <span class="shortcut">Drawer</span>`);
        addForce.addEventListener('click', () => toggleDrawer(!state.drawerOpen || state.drawerTab !== 'forces', 'forces'));
        rowB.appendChild(addForce);
        const frame = element('button', 'view-toggle', icon('frame', 14));
        frame.title = 'Frame domain (F)';
        frame.addEventListener('click', () => app.frameDomain());
        rowB.appendChild(frame);
        const cameraSelect = element('div', 'pill-select', `<span class="leading-icon">${icon('perspective', 13)}</span>`);
        const select = document.createElement('select');
        for (const [value, label] of [['threeQuarter', 'Perspective'], ['front', 'Front'], ['side', 'Side'], ['top', 'Top']])
        {
            const option = document.createElement('option'); option.value = value; option.textContent = label; select.appendChild(option);
        }
        select.addEventListener('change', () => app.setCameraPreset(select.value));
        cameraSelect.appendChild(select);
        rowB.appendChild(cameraSelect);
        rowB.appendChild(element('div', 'toolbar-spacer'));
        for (const [id, glyph, title] of [['vis-volume', 'volume', 'Volume render'], ['vis-vectors', 'vectors', 'Velocity vectors'], ['vis-slice', 'slice', 'Scalar slice'], ['vis-tracers', 'tracers', 'Flow tracers'], ['vis-gizmos', 'gizmo', 'Force gizmos']])
        {
            const visualizer = Object.values(scene.Visualizers).find((candidate) => candidate.Id === id);
            const button = element('button', 'view-toggle' + (visualizer.Visible ? ' active' : ''), icon(glyph, 14));
            button.title = title;
            button.addEventListener('click', () => app.updateNode(id, 'Visible', !visualizer.Visible));
            rowB.appendChild(button);
        }
        host.appendChild(rowB);
    }

    function setMode(mode)
    {
        state.mode = mode;
        const gizmos = app.scene.Visualizers.ForceGizmos;
        if (mode === 'play') { gizmos.Visible = false; app.scene.Visualizers.VelocityVectors.Visible = false; app.play(); }
        else if (mode === 'edit') { gizmos.Visible = true; app.pause(); }
        else { gizmos.Visible = true; app.play(); }
    }

    function renderTransport(snapshot)
    {
        const host = refs.transport;
        host.innerHTML = '';
        const play = element('button', 'primary', icon(snapshot.playing ? 'pause' : 'play', 14));
        play.title = snapshot.playing ? 'Pause (Space)' : 'Play (Space)';
        play.addEventListener('click', () => app.togglePlay());
        const step = element('button', '', icon('step', 14));
        step.title = 'Step one frame (.)';
        step.addEventListener('click', () => app.stepOnce());
        const reset = element('button', '', icon('reset', 14));
        reset.title = 'Reset fields (R)';
        reset.addEventListener('click', () => app.reset());
        const readout = element('div', 'readout', `<strong id="transport-time">0.00 s</strong><span id="transport-frame">frame 0</span>`);
        host.append(play, step, reset, readout);
    }

    function renderLegend(snapshot)
    {
        const host = refs.legend;
        host.innerHTML = '';
        const visualizers = snapshot.scene.Visualizers;
        const entries = [];
        if (visualizers.VelocityVectors.Visible) entries.push({ title: 'Velocity', ramp: visualizers.VelocityVectors.Ramp, min: '0', max: `${formatNumber(visualizers.VelocityVectors.MaxSpeed, 1)} m/s` });
        if (visualizers.Tracers.Visible && !(visualizers.VelocityVectors.Visible && visualizers.Tracers.Ramp === visualizers.VelocityVectors.Ramp)) entries.push({ title: 'Tracer speed', ramp: visualizers.Tracers.Ramp, min: '0', max: `${formatNumber(visualizers.Tracers.MaxSpeed, 1)} m/s` });
        if (visualizers.ScalarSlice.Visible)
        {
            const signed = visualizers.ScalarSlice.Channel === 'Pressure' || visualizers.ScalarSlice.Channel === 'Divergence';
            entries.push({ title: visualizers.ScalarSlice.Channel, ramp: visualizers.ScalarSlice.Ramp, min: signed ? `−${formatNumber(visualizers.ScalarSlice.Range, 1)}` : '0', max: formatNumber(visualizers.ScalarSlice.Range, 1) });
        }
        for (const entry of entries)
            host.appendChild(element('div', 'legend-card', `<strong>${entry.title}</strong><div class="ramp" style="background:${rampGradient(entry.ramp)}"></div><div class="range"><span>${entry.min}</span><span>${entry.max}</span></div>`));
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  NOTCH + DRAWER
    //------------------------------------------------------------------------------------------------------------------------

    function renderNotch()
    {
        refs.notch.innerHTML = `<span class="notch-dot"></span><span>Project-Zero</span><span style="color:#555">·</span><span>Fluid Studio</span>`;
        refs.notch.addEventListener('click', () => { state.notchOpen = !state.notchOpen; refs.notchMenu.classList.toggle('open', state.notchOpen); });
        const menu = refs.notchMenu;
        menu.innerHTML = '';
        menu.appendChild(element('h4', '', 'Workspace'));
        const workspaces = element('div', 'notch-grid');
        for (const [label, sub, active] of [['World', 'Scene editor', false], ['Fluid', 'Gas + fire solver', true], ['ForceField', 'Vector field design', false]])
        {
            workspaces.appendChild(element('button', active ? 'active' : '', `<strong>${label}</strong><span>${sub}</span>`));
        }
        menu.appendChild(workspaces);
        menu.appendChild(element('h4', '', 'Scene document'));
        const documentRow = element('div', 'notch-grid');
        const exportButton = element('button', '', `<strong>${icon('download', 12)} Export</strong><span>.FluidScene JSON</span>`);
        exportButton.addEventListener('click', () =>
        {
            const blob = new Blob([app.exportJson()], { type: 'application/json' });
            const link = document.createElement('a');
            link.href = URL.createObjectURL(blob);
            link.download = `${app.scene.Name.replace(/\s+/g, '')}.FluidScene.json`;
            link.click();
            setTimeout(() => URL.revokeObjectURL(link.href), 1000);
        });
        const importButton = element('button', '', `<strong>${icon('upload', 12)} Import</strong><span>Load JSON</span>`);
        importButton.addEventListener('click', () =>
        {
            const input = document.createElement('input');
            input.type = 'file'; input.accept = 'application/json';
            input.addEventListener('change', () =>
            {
                const file = input.files && input.files[0];
                if (!file) return;
                file.text().then((text) => app.importJson(text)).catch((error) => alert(error.message));
            });
            input.click();
        });
        const resetButton = element('button', '', `<strong>${icon('reset', 12)} Reload</strong><span>Reset preset</span>`);
        resetButton.addEventListener('click', () => app.loadPreset(app.scene.PresetId || ScenePresets[0].Id));
        documentRow.append(exportButton, importButton, resetButton);
        menu.appendChild(documentRow);
        menu.appendChild(element('p', 'note', 'Shortcuts — Space play/pause · . step · R reset · F frame · Delete remove selection · LMB orbit · RMB/MMB pan · Wheel dolly · drag gizmo to move (Shift locks height).'));
        document.addEventListener('pointerdown', (event) =>
        {
            if (state.notchOpen && !menu.contains(event.target) && !refs.notch.contains(event.target)) { state.notchOpen = false; menu.classList.remove('open'); }
        });
    }

    function toggleDrawer(open, tab)
    {
        state.drawerOpen = open;
        if (tab) state.drawerTab = tab;
        refs.drawer.classList.toggle('open', open);
        renderDrawer();
    }

    function renderDrawer()
    {
        const drawer = refs.drawer;
        drawer.innerHTML = '';
        const head = element('div', 'drawer-head', `<span style="color:#ddd;font-size:12px">${icon('folder', 13)}</span>`);
        const seg = element('div', 'seg');
        for (const [key, label] of [['presets', 'Scene Presets'], ['forces', 'Force Generators'], ['emitters', 'Emitters & Colliders']])
        {
            const button = element('button', state.drawerTab === key ? 'active' : '', label);
            button.addEventListener('click', () => { state.drawerTab = key; renderDrawer(); });
            seg.appendChild(button);
        }
        head.appendChild(seg);
        head.appendChild(element('span', 'hint', state.drawerTab === 'forces' ? `Click a card to construct it in the domain · ${app.scene.Forces.length}/${MaximumForceGenerators} slots used` : 'Click a card to construct'));
        const close = element('button', 'icon-button', '×');
        close.addEventListener('click', () => toggleDrawer(false));
        head.appendChild(close);
        drawer.appendChild(head);
        const body = element('div', 'drawer-body');
        if (state.drawerTab === 'presets')
        {
            for (const preset of ScenePresets)
            {
                const cardNode = element('button', 'asset-card', `<div class="thumb" style="background:linear-gradient(140deg,#2a2a2a,#141414)">${icon(preset.Icon, 34, 1)}</div><strong>${preset.Name}</strong><span>${preset.Summary}</span><span class="badge">${preset.Domain.Resolution.join('×')}</span>`);
                cardNode.addEventListener('click', () => app.loadPreset(preset.Id));
                body.appendChild(cardNode);
            }
        }
        else if (state.drawerTab === 'forces')
        {
            for (const type of ForceGeneratorTypes)
            {
                const cardNode = element('button', 'asset-card', `<div class="thumb" style="background:linear-gradient(140deg,${type.Tint}55,#141414)">${icon(type.Icon, 34, 1)}</div><strong>${type.Name}</strong><span>${type.Summary}</span><span class="badge">${type.Defaults.Falloff === 'Constant' ? 'Infinite' : FalloffLabels[type.Defaults.Falloff]} falloff</span>`);
                cardNode.addEventListener('click', () => { app.addForce(type.Id); });
                body.appendChild(cardNode);
            }
        }
        else
        {
            for (const [shape, summary] of [['Sphere', 'Volumetric source of smoke, heat or fuel.'], ['Disc', 'Flat nozzle — orient the normal to aim the jet.'], ['Box', 'Cubic source region with soft edges.']])
            {
                const cardNode = element('button', 'asset-card', `<div class="thumb" style="background:linear-gradient(140deg,#f9731655,#141414)">${icon('flame', 34, 1)}</div><strong>${shape} Emitter</strong><span>${summary}</span><span class="badge">Emitter</span>`);
                cardNode.addEventListener('click', () => app.addEmitter(shape));
                body.appendChild(cardNode);
            }
            const collider = element('button', 'asset-card', `<div class="thumb" style="background:linear-gradient(140deg,#94a3b855,#141414)">${icon('collider', 34, 1)}</div><strong>Sphere Collider</strong><span>Solid obstacle: no-slip walls with pressure Neumann boundary.</span><span class="badge">Collider</span>`);
            collider.addEventListener('click', () => app.addCollider());
            body.appendChild(collider);
        }
        drawer.appendChild(body);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  METRICS + STATUS BAR
    //------------------------------------------------------------------------------------------------------------------------

    function updateMetrics(metrics)
    {
        state.metrics = metrics;
        const time = document.getElementById('transport-time');
        const frame = document.getElementById('transport-frame');
        if (time) time.textContent = `${metrics.simulationTime.toFixed(2)} s`;
        if (frame) frame.textContent = `frame ${metrics.simulationFrame}`;
        refs.statusFps.textContent = `${metrics.fps.toFixed(0)} fps`;
        refs.statusStep.textContent = `${metrics.stepMilliseconds.toFixed(1)} ms · ${metrics.passes} passes`;
        refs.statusGrid.textContent = `${metrics.resolution.join('×')} · ${formatCells(metrics.cells)}`;
        refs.statusForces.textContent = `${metrics.forces} active · ${metrics.emitters} emitters`;
        refs.statusText.innerHTML = `HTML preview · <b>WebGL2 GPU solver</b> · ${metrics.playing ? 'simulating' : 'paused'} · no engine connection`;
    }

    function renderAll(snapshot)
    {
        state.snapshot = snapshot;
        renderOutliner(snapshot);
        renderInspector(snapshot);
        renderToolbar(snapshot);
        renderTransport(snapshot);
        renderLegend(snapshot);
        if (state.drawerOpen) renderDrawer();
        if (state.metrics) updateMetrics(state.metrics);
    }

    renderNotch();
    refs.drawerTab.innerHTML = `${icon('folder', 12)} Asset Browser`;
    refs.drawerTab.addEventListener('click', () => toggleDrawer(!state.drawerOpen));
    document.addEventListener('keydown', (event) =>
    {
        if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'f')
        {
            event.preventDefault();
            const input = refs.outliner.querySelector('.search-pill input');
            if (input) input.focus();
        }
    });

    return { renderAll, updateMetrics, toggleDrawer };
}
