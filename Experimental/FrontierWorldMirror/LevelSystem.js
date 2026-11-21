//==========================================================================================================================================
//                                                        LEVELSYSTEM.JS
//==========================================================================================================================================
// Multi-Level persistence, built-in Frontier levels, and Create Level template generator for FrontierWorldMirror.

import {
  BuildMaterialRecord,
  CameraFlareDefaults,
  CameraTonemapDefaults,
  CanonicalEditorCamera,
  CanonicalRenderSettings,
  ResolveRenderSettings,
  ShowcaseInitialRows,
  ShowcaseInitialValues,
} from "./ShowcaseGalleryScene.js";
import { BuildDefaultDiagnosticForceRig } from "./ForceFieldCatalog.js";
import {
  BuildCylinderConeMaterial,
  BuildTrafficConeMaterial,
} from "./WorldDragDrop.js";

export const LEVEL_STORAGE_KEY = "Frontier.WorldMirrorLevels.v6";

const Clone = (Value) => JSON.parse(JSON.stringify(Value));

// Canonical component defaults for the Editor Camera's Lens Flare &
// Tonemapping sub-components live in ShowcaseGalleryScene.js (imported above).

export function EnsureRenderSettingsInLevel(Lvl) {
  if (!Lvl || !Array.isArray(Lvl.Rows)) return Lvl;
  let Rows = Lvl.Rows;
  const HasRenderSettings = Rows.some((R) => R.Id === "render-settings");
  if (!HasRenderSettings) {
    const CamIdx = Rows.findIndex((R) => R.Id === "camera");
    const RenderRow = {
      Id: "render-settings",
      Name: "Render Settings",
      Panel: "render-settings",
      Icon: "render-settings",
      Parent: "camera",
      Description: "Shadows, GI & Reflections · Per-pipeline quality",
    };
    Rows = [...Rows];
    if (CamIdx >= 0) Rows.splice(CamIdx + 1, 0, RenderRow);
    else Rows.push(RenderRow);
  }
  // Editor camera carries two components (Blueprint-style sub-entries):
  // Lens Flare and Tonemapping. Injected into every level so the camera's
  // component model is complete regardless of template.
  const CameraSubs = [
    {
      Id: "camera-flare",
      Name: "Lens Flare",
      Panel: "flare",
      Icon: "lens-flare",
      Parent: "camera",
      Description: "Editor camera lens flare component",
    },
    {
      Id: "camera-tonemap",
      Name: "Tonemapping",
      Panel: "post",
      Icon: "environment-exposure",
      Parent: "camera",
      Description: "Editor camera tonemapping component",
    },
  ].filter((Sub) => !Rows.some((R) => R.Id === Sub.Id));
  if (CameraSubs.length) {
    const CamIdx = Rows.findIndex((R) => R.Id === "camera");
    Rows = [...Rows];
    if (CamIdx >= 0) Rows.splice(CamIdx + 1, 0, ...CameraSubs);
    else Rows.push(...CameraSubs);
  }
  let Values = {
    ...(Lvl.Values || {}),
    "render-settings": ResolveRenderSettings(Lvl.Values),
  };
  if (!Values["camera-flare"]) {
    Values["camera-flare"] = Clone(CameraFlareDefaults);
  }
  if (!Values["camera-tonemap"]) {
    Values["camera-tonemap"] = Clone(CameraTonemapDefaults);
  }
  if (Lvl.Id === "level-showcase-gallery" && !Rows.some((R) => R.Id === "control-vehicle")) {
    const VehicleRow = ShowcaseInitialRows.find((R) => R.Id === "control-vehicle");
    if (VehicleRow) {
      Rows = [...Rows];
      const SphereIdx = Rows.findIndex((R) => R.Id === "abbe-sphere");
      if (SphereIdx >= 0) Rows.splice(SphereIdx + 1, 0, Clone(VehicleRow));
      else Rows.push(Clone(VehicleRow));
      if (!Values["control-vehicle"] && ShowcaseInitialValues["control-vehicle"]) {
        Values["control-vehicle"] = Clone(ShowcaseInitialValues["control-vehicle"]);
      }
    }
  }
  for (const EnvId of ["atmosphere", "clouds", "moon", "stars"]) {
    if (!Rows.some((R) => R.Panel === EnvId)) {
      const EnvRow = ShowcaseInitialRows.find((R) => R.Id === EnvId);
      if (EnvRow) {
        Rows = [...Rows];
        const SunIdx = Rows.findIndex((R) => R.Panel === "sun" || R.Id === "world");
        if (SunIdx >= 0) Rows.splice(SunIdx + 1, 0, Clone(EnvRow));
        else Rows.push(Clone(EnvRow));
      }
    }
    if (!Values[EnvId] && ShowcaseInitialValues[EnvId]) {
      Values[EnvId] = Clone(ShowcaseInitialValues[EnvId]);
    }
  }
  return { ...Lvl, Rows, Values };
}

function BuildLookDevStudioLevel() {
  const Rows = [
    { Id: "scene", Name: "Look-Dev Studio", Panel: "group", Icon: "folder-scene", Parent: null, Description: "Material Look-Dev collection" },
    {
      Id: "studio-floor",
      Name: "Slate Cyclorama Floor",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Matte charcoal studio floor · 6.40 × 6.40 m",
    },
    {
      Id: "studio-backdrop",
      Name: "Studio Backdrop Wall",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Neutral mid-grey backdrop · 6.40 × 3.00 m",
    },
    {
      Id: "titanium-plinth",
      Name: "Brushed Titanium Plinth",
      PrimitiveShape: "cylinder",
      Panel: "geometry",
      Icon: "editor-cylinder",
      Parent: "scene",
      Preview: true,
      Description: "Anisotropic conductor pedestal · R 0.68 m",
    },
    {
      Id: "hero-clearcoat-orb",
      Name: "Cobalt Clear-Coat Sphere",
      PrimitiveShape: "sphere",
      Panel: "geometry",
      Icon: "editor-sphere",
      Parent: "scene",
      Preview: true,
      Description: "Dual-layer automotive clear-coat hero sphere",
    },
    {
      Id: "velvet-torus",
      Name: "Crimson Velvet Torus",
      PrimitiveShape: "torus",
      Panel: "geometry",
      Icon: "editor-torus",
      Parent: "scene",
      Preview: true,
      Description: "Fabric Charlie sheen specimen",
    },
    {
      Id: "jade-subsurface-orb",
      Name: "Subsurface Jade Orb",
      PrimitiveShape: "sphere",
      Panel: "geometry",
      Icon: "editor-sphere",
      Parent: "scene",
      Preview: true,
      Description: "Wrap-lit subsurface scattering specimen",
    },
    { Id: "cameras", Name: "Cameras", Panel: "group", Icon: "camera", Parent: null, Description: "Editor cameras" },
    { Id: "camera", Name: "Editor Camera", Panel: "camera", Icon: "camera", Parent: "cameras", Description: "Permanent editor navigation camera" },
    { Id: "post", Name: "ACES Post-Process", Panel: "post", Icon: "environment-exposure", Parent: "cameras", Description: "Exposure & tonemapping" },
    { Id: "world", Name: "World", Panel: "group", Icon: "folder-world", Parent: null, Description: "Solar & sky environment" },
    { Id: "sun", Name: "Key Rim Sun", Panel: "sun", Icon: "sun", Parent: "world", Preview: true, Description: "Directional · 4.2 W/m²" },
    { Id: "lighting", Name: "Lighting", Panel: "group", Icon: "folder-generic", Parent: null, Description: "Studio LTC area lights" },
    {
      Id: "key-softbox",
      Name: "Studio Overhead Softbox",
      Panel: "light",
      Icon: "editor-area-light",
      Parent: "lighting",
      Preview: true,
      Description: "LTC Quad · 2.10 × 1.50 m",
    },
  ];

  const VelvetMat = BuildMaterialRecord({
    Type: "Fabric",
    BaseLinear: [0.38, 0.04, 0.09],
    Roughness: 0.82,
  });
  VelvetMat.Material.Channels.sheen = { Source: "Fill", Fill: "#f09ab0" };
  VelvetMat.Material.Channels.sheenRoughness = { Source: "Fill", Fill: 0.45 };

  const JadeMat = BuildMaterialRecord({
    Type: "Subsurface",
    BaseLinear: [0.14, 0.52, 0.36],
    Roughness: 0.26,
  });
  JadeMat.Material.Channels.subsurface = { Source: "Fill", Fill: "#4be0a2" };
  JadeMat.Material.Channels.thickness = { Source: "Fill", Fill: 0.35 };

  const Values = {
    "studio-floor": {
      Position: [0, 0.3, -0.68],
      Rotation: [0, 0, 0],
      Scale: [6.4, 6.4, 0.16],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({ Type: "Standard", BaseLinear: [0.16, 0.17, 0.19], Roughness: 0.46 }),
    },
    "studio-backdrop": {
      Position: [0, 2.55, 0.82],
      Rotation: [0, 0, 0],
      Scale: [6.4, 0.18, 3.0],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({ Type: "Standard", BaseLinear: [0.26, 0.28, 0.32], Roughness: 0.62 }),
    },
    "titanium-plinth": {
      Position: [0, 0.45, -0.36],
      Rotation: [0, 0, 0],
      Scale: [1.35, 1.35, 0.48],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({ Type: "Metal", BaseLinear: [0.66, 0.69, 0.74], Metallic: 1.0, Roughness: 0.22, Anisotropy: 0.65 }),
    },
    "hero-clearcoat-orb": {
      Position: [0, 0.45, 0.38],
      Rotation: [0, 0, 0],
      Scale: [1.02, 1.02, 1.02],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({ Type: "Clear coat", BaseLinear: [0.06, 0.24, 0.86], Metallic: 0.25, Roughness: 0.28, Coat: 1.0, CoatRoughness: 0.04 }),
    },
    "velvet-torus": {
      Position: [-1.18, 0.35, -0.22],
      Rotation: [28, -18, 15],
      Scale: [0.92, 0.92, 0.56],
      "Cast shadows": true,
      GI: true,
      ...VelvetMat,
    },
    "jade-subsurface-orb": {
      Position: [1.16, 0.35, -0.2],
      Rotation: [0, 0, 0],
      Scale: [0.84, 0.84, 0.84],
      "Cast shadows": true,
      GI: true,
      ...JadeMat,
    },
    camera: {
      Position: [0.0, -3.15, 0.48],
      Target: [0.0, 0.42, 0.08],
      "Focal Length": 30.0,
      "Sensor Width": 36.0,
      Aperture: 4.0,
      "Subject Distance": 3.6,
    },
    sun: {
      Direction: [-0.48, -0.52, 0.70],
      SolarIrradiance: [4.2, 4.05, 3.9],
      SkyRadiance: [0.14, 0.20, 0.34],
      Intensity: 4.2,
      "Angular Diameter": 2.4,
      Temperature: 5600,
    },
    "key-softbox": {
      Position: [0.25, -0.15, 1.95],
      Rotation: [0, 0, 0],
      Scale: [2.1, 1.5, 1.0],
      Intensity: 8.8,
      EmittedRadiance: [8.8, 8.2, 7.6],
      ReferenceInspector: {
        Type: "LuminousSurface",
        Properties: {
          lumens: 2800,
          dimmer: 1,
          color: "#fff6ec",
          temperature: 5500,
          useTemp: false,
          width: 2.1,
          height: 1.5,
          pos: [0.25, -0.15, 1.95],
          rot: [0, 0, 0],
        },
      },
    },
  };

  return {
    Id: "level-lookdev-studio",
    Name: "Look-Dev Plinth Studio",
    Notes: "Three-specimen OpenPBR material stage (Clear-Coat, Charlie Velvet Sheen, Subsurface Jade).",
    Rows,
    Values,
    Hidden: {},
    Collapsed: {},
    Selected: "hero-clearcoat-orb",
  };
}

function BuildEmptyProvingGroundLevel(Id = "level-proving-ground", Name = "Empty Proving Ground", Notes = "") {
  const Rows = [
    { Id: "scene", Name, Panel: "group", Icon: "folder-scene", Parent: null, Description: "Level collection" },
    {
      Id: "ground-plane",
      Name: "Proving Ground Slab",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Architectural reference slab · 8.00 × 8.00 m",
    },
    {
      Id: "starter-cube",
      Name: "Starter Blockout Cube",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Origin 1 m calibration cube",
    },
    { Id: "cameras", Name: "Cameras", Panel: "group", Icon: "camera", Parent: null, Description: "Editor cameras" },
    { Id: "camera", Name: "Editor Camera", Panel: "camera", Icon: "camera", Parent: "cameras", Description: "Permanent editor navigation camera" },
    { Id: "post", Name: "ACES Post-Process", Panel: "post", Icon: "environment-exposure", Parent: "cameras", Description: "Exposure & tonemapping" },
    { Id: "world", Name: "World", Panel: "group", Icon: "folder-world", Parent: null, Description: "Solar environment" },
    { Id: "sun", Name: "Sun", Panel: "sun", Icon: "sun", Parent: "world", Preview: true, Description: "Directional · 5.4 W/m²" },
  ];

  const Values = {
    scene: { Notes: Notes || "Clean architectural proving ground level." },
    "ground-plane": {
      Position: [0, 0.4, -0.68],
      Rotation: [0, 0, 0],
      Scale: [8.0, 8.0, 0.16],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({ Type: "Standard", BaseLinear: [0.58, 0.59, 0.61], Roughness: 0.68 }),
    },
    "starter-cube": {
      Position: [0, 0.4, -0.1],
      Rotation: [0, 0, 18],
      Scale: [1.0, 1.0, 1.0],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({ Type: "Standard", BaseLinear: [0.84, 0.56, 0.24], Roughness: 0.34 }),
    },
    camera: {
      Position: [...CanonicalEditorCamera.Position],
      Target: [...CanonicalEditorCamera.Target],
      "Focal Length": CanonicalEditorCamera.FocalLengthMm,
      "Sensor Width": CanonicalEditorCamera.SensorWidthMm,
      Aperture: CanonicalEditorCamera.ApertureFStop,
      "Subject Distance": CanonicalEditorCamera.SubjectDistanceM,
    },
    sun: {
      Direction: [0.45, -0.58, 0.68],
      SolarIrradiance: [5.4, 5.1, 4.65],
      SkyRadiance: [0.18, 0.26, 0.40],
      Intensity: 5.4,
      "Angular Diameter": 2.06,
      Temperature: 5650,
    },
  };

  return {
    Id,
    Name,
    Notes: Notes || "Clean architectural blockout level.",
    Rows,
    Values,
    Hidden: {},
    Collapsed: {},
    Selected: "starter-cube",
  };
}

export function BuildDefaultLevels() {
  return [
    EnsureRenderSettingsInLevel(BuildLobbyLevel()),
    EnsureRenderSettingsInLevel({
      Id: "level-showcase-gallery",
      Name: "Cornell-Studio Gallery",
      Notes: "Canonical C++ <-> HTML Architectural Cornell-Studio Gallery with ControlVehicle.",
      Rows: Clone(ShowcaseInitialRows),
      Values: Clone(ShowcaseInitialValues),
      Hidden: {},
      Collapsed: {},
      Selected: "chrome-sphere",
    }),
    EnsureRenderSettingsInLevel(BuildLookDevStudioLevel()),
    EnsureRenderSettingsInLevel(BuildEmptyProvingGroundLevel()),
  ];
}

export function BuildLobbyLevel(
  Id = "level-frontier-lobby",
  Name = "Frontier Lobby",
  Notes = "",
) {
  const FloorTop = -0.68 + 0.14 * 0.5;
  const AircraftStandHeight = 0.62;
  const Rows = [
    {
      Id: "scene",
      Name,
      Panel: "group",
      Icon: "folder-scene",
      Parent: null,
      Description: "Frontier welcome lobby · ControlVehicle + display aircraft",
    },
    {
      Id: "lobby-floor",
      Name: "Terrazzo Welcome Hall Floor",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Warm stone lobby floor · 12.80 × 13.20 m",
    },
    {
      Id: "lobby-back-wall",
      Name: "Welcome Hall Rear Wall",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Rear lobby wall with a recessed welcome feature",
    },
    {
      Id: "lobby-left-wall",
      Name: "West Gallery Wall",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Open-sided architectural lobby enclosure",
    },
    {
      Id: "lobby-right-wall",
      Name: "East Gallery Wall",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Open-sided architectural lobby enclosure",
    },
    {
      Id: "lobby-entry-pier-left",
      Name: "Entry Portal · West Pier",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Cobalt stone entry portal pier",
    },
    {
      Id: "lobby-entry-pier-right",
      Name: "Entry Portal · East Pier",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Cobalt stone entry portal pier",
    },
    {
      Id: "lobby-entry-lintel",
      Name: "Entry Portal · Header",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Brushed-metal entry portal header",
    },
    {
      Id: "lobby-reception-counter",
      Name: "Reception Counter",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Dark timber welcome desk with a stone counter top",
    },
    {
      Id: "lobby-reception-top",
      Name: "Reception Counter · Stone Top",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Light honed-stone reception work surface",
    },
    {
      Id: "lobby-welcome-panel",
      Name: "Welcome Feature Wall",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Cobalt architectural panel behind reception",
    },
    {
      Id: "lobby-welcome-inlay",
      Name: "Welcome Panel · Metallic Inlay",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Brushed-metal horizontal feature inlay",
    },
    {
      Id: "control-vehicle",
      Name: "ControlVehicle Display",
      PrimitiveShape: "vehicle",
      Panel: "geometry",
      Icon: "vehicle",
      Parent: "scene",
      Preview: true,
      Description: "ControlVehicle BVH Mesh · Clear-coat visitor display",
    },
    {
      Id: "lobby-aircraft",
      Name: "Lobby Display Monoplane",
      PrimitiveShape: "box",
      PropId: "lobby_aircraft",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Cobalt-and-ivory trainer aircraft on its own museum plinth",
    },
    {
      Id: "cameras",
      Name: "Cameras",
      Panel: "group",
      Icon: "camera",
      Parent: null,
      Description: "Editor cameras",
    },
    {
      Id: "camera",
      Name: "Editor Camera",
      Panel: "camera",
      Icon: "camera",
      Parent: "cameras",
      Description: "Permanent editor navigation camera",
    },
    {
      Id: "world",
      Name: "World",
      Panel: "group",
      Icon: "folder-world",
      Parent: null,
      Description: "C++-parity Sun, physical sky and volumetric clouds",
    },
    {
      Id: "sun",
      Name: "Lobby Sun",
      Panel: "sun",
      Icon: "sun",
      Parent: "world",
      Preview: true,
      Description: "Frontier solar lighting · physical Atmosphere and Clouds",
    },
  ];

  const Values = {
    scene: {
      Notes:
        Notes ||
        "Open welcome hall with a ControlVehicle and a display aircraft under the native-parity Sun, Sky and Clouds.",
    },
    "lobby-floor": {
      Position: [0, 0.25, -0.68],
      Rotation: [0, 0, 0],
      Scale: [12.8, 13.2, 0.14],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({
        Type: "Standard",
        BaseLinear: [0.62, 0.60, 0.56],
        Roughness: 0.62,
      }),
    },
    "lobby-back-wall": {
      Position: [0, 6.45, 2.0],
      Rotation: [0, 0, 0],
      Scale: [12.8, 0.3, 5.2],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({
        Type: "Standard",
        BaseLinear: [0.70, 0.72, 0.75],
        Roughness: 0.76,
      }),
    },
    "lobby-left-wall": {
      Position: [-6.25, 0.0, 2.0],
      Rotation: [0, 0, 0],
      Scale: [0.3, 12.9, 5.2],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({
        Type: "Standard",
        BaseLinear: [0.48, 0.51, 0.56],
        Roughness: 0.72,
      }),
    },
    "lobby-right-wall": {
      Position: [6.25, 0.0, 2.0],
      Rotation: [0, 0, 0],
      Scale: [0.3, 12.9, 5.2],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({
        Type: "Standard",
        BaseLinear: [0.66, 0.68, 0.70],
        Roughness: 0.72,
      }),
    },
    "lobby-entry-pier-left": {
      Position: [-4.65, -6.15, 1.30],
      Rotation: [0, 0, 0],
      Scale: [0.44, 0.4, 3.8],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({
        Type: "Clear coat",
        BaseLinear: [0.10, 0.17, 0.29],
        Metallic: 0.12,
        Roughness: 0.28,
        Coat: 0.58,
        CoatRoughness: 0.08,
      }),
    },
    "lobby-entry-pier-right": {
      Position: [4.65, -6.15, 1.30],
      Rotation: [0, 0, 0],
      Scale: [0.44, 0.4, 3.8],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({
        Type: "Clear coat",
        BaseLinear: [0.10, 0.17, 0.29],
        Metallic: 0.12,
        Roughness: 0.28,
        Coat: 0.58,
        CoatRoughness: 0.08,
      }),
    },
    "lobby-entry-lintel": {
      Position: [0, -6.15, 3.3],
      Rotation: [0, 0, 0],
      Scale: [9.7, 0.5, 0.32],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({
        Type: "Metal",
        BaseLinear: [0.56, 0.59, 0.63],
        Metallic: 0.82,
        Roughness: 0.25,
      }),
    },
    "lobby-reception-counter": {
      Position: [0, 4.1, FloorTop + 0.34],
      Rotation: [0, 0, 0],
      Scale: [1.45, 0.42, 0.34],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({
        Type: "Standard",
        BaseLinear: [0.13, 0.17, 0.22],
        Roughness: 0.42,
      }),
    },
    "lobby-reception-top": {
      Position: [0, 4.1, FloorTop + 0.75],
      Rotation: [0, 0, 0],
      Scale: [3.16, 0.96, 0.14],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({
        Type: "Standard",
        BaseLinear: [0.76, 0.74, 0.68],
        Roughness: 0.31,
      }),
    },
    "lobby-welcome-panel": {
      Position: [0, 6.27, 3.05],
      Rotation: [0, 0, 0],
      Scale: [4.1, 0.09, 1.08],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({
        Type: "Clear coat",
        BaseLinear: [0.035, 0.075, 0.16],
        Metallic: 0.16,
        Roughness: 0.26,
        Coat: 0.72,
        CoatRoughness: 0.06,
      }),
    },
    "lobby-welcome-inlay": {
      Position: [0, 6.205, 2.66],
      Rotation: [0, 0, 0],
      Scale: [2.1, 0.07, 0.07],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({
        Type: "Metal",
        BaseLinear: [0.73, 0.56, 0.31],
        Metallic: 0.88,
        Roughness: 0.2,
      }),
    },
    "control-vehicle": {
      Position: [-2.3, -0.55, FloorTop],
      Rotation: [0, 0, -14],
      Scale: [0.52, 0.52, 0.52],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({
        Type: "Clear coat",
        BaseLinear: [0.82, 0.87, 0.95],
        Metallic: 0.24,
        Roughness: 0.14,
        Coat: 1.0,
        CoatRoughness: 0.03,
      }),
    },
    "lobby-aircraft": {
      Position: [2.45, 0.45, FloorTop + AircraftStandHeight],
      Rotation: [0, 0, 0],
      Scale: [1, 1, 1],
      "Cast shadows": true,
      GI: true,
    },
    camera: {
      Position: [0, -12.4, 4.15],
      Target: [0, 0.4, 1.15],
      "Focal Length": CanonicalEditorCamera.FocalLengthMm,
      "Sensor Width": CanonicalEditorCamera.SensorWidthMm,
      Aperture: CanonicalEditorCamera.ApertureFStop,
      "Subject Distance": 13.0,
    },
    sun: Clone(ShowcaseInitialValues.sun),
  };

  return {
    Id,
    Name,
    Notes:
      Notes ||
      "Welcome lobby for the Frontier Editor with a ControlVehicle and a detailed aircraft display.",
    Rows,
    Values,
    Hidden: {},
    Collapsed: {},
    Selected: "control-vehicle",
  };
}

function BuildAutomotiveConeSlalomLevel(
  CustomId = "level-cone-slalom",
  CustomName = "Automotive Cone Slalom",
  CustomNotes = "",
) {
  const Rows = [
    { Id: "scene", Name: CustomName, Panel: "group", Icon: "folder-scene", Parent: null, Description: "Automotive proving ground" },
    {
      Id: "asphalt-pad",
      Name: "Proving Ground Asphalt",
      PrimitiveShape: "box",
      Panel: "geometry",
      Icon: "editor-cube",
      Parent: "scene",
      Preview: true,
      Description: "Dark tarmac proving pad · 14.00 × 14.00 m",
    },
    {
      Id: "control-vehicle",
      Name: "ControlVehicle",
      PrimitiveShape: "vehicle",
      Panel: "geometry",
      Icon: "vehicle",
      Parent: "scene",
      Preview: true,
      Description: "ControlVehicle BVH Mesh · Full-Scale 1:1 Proving Ground Car",
    },
    {
      Id: "traffic-cone-1",
      Name: "Traffic Cone · Gate A1",
      PrimitiveShape: "cone",
      Panel: "geometry",
      Icon: "editor-cone",
      Parent: "scene",
      Preview: true,
      Description: "Tapered car traffic cone · reflective collar",
    },
    {
      Id: "traffic-cone-2",
      Name: "Traffic Cone · Apex B",
      PrimitiveShape: "cone",
      Panel: "geometry",
      Icon: "editor-cone",
      Parent: "scene",
      Preview: true,
      Description: "Tapered car traffic cone · reflective collar",
    },
    {
      Id: "traffic-cone-3",
      Name: "Traffic Cone · Exit C1",
      PrimitiveShape: "cone",
      Panel: "geometry",
      Icon: "editor-cone",
      Parent: "scene",
      Preview: true,
      Description: "Tapered car traffic cone · reflective collar",
    },
    {
      Id: "cylinder-cone-1",
      Name: "Cylinder Cone · Lane West",
      PrimitiveShape: "cylinder-cone",
      Panel: "geometry",
      Icon: "editor-cylinder",
      Parent: "scene",
      Preview: true,
      Description: "Cylinder-like car lane delineator post",
    },
    {
      Id: "cylinder-cone-2",
      Name: "Cylinder Cone · Lane East",
      PrimitiveShape: "cylinder-cone",
      Panel: "geometry",
      Icon: "editor-cylinder",
      Parent: "scene",
      Preview: true,
      Description: "Cylinder-like car lane delineator post",
    },
    {
      Id: "cylinder-cone-3",
      Name: "Cylinder Cone · Finish Post",
      PrimitiveShape: "cylinder-cone",
      Panel: "geometry",
      Icon: "editor-cylinder",
      Parent: "scene",
      Preview: true,
      Description: "Cylinder-like car lane delineator post",
    },
    { Id: "cameras", Name: "Cameras", Panel: "group", Icon: "camera", Parent: null, Description: "Editor cameras" },
    { Id: "camera", Name: "Editor Camera", Panel: "camera", Icon: "camera", Parent: "cameras", Description: "Permanent editor navigation camera" },
    { Id: "post", Name: "ACES Post-Process", Panel: "post", Icon: "environment-exposure", Parent: "cameras", Description: "Exposure & tonemapping" },
    { Id: "world", Name: "World", Panel: "group", Icon: "folder-world", Parent: null, Description: "Solar & sky environment" },
    { Id: "sun", Name: "Track Sun", Panel: "sun", Icon: "sun", Parent: "world", Preview: true, Description: "Directional · 4.6 W/m²" },
    { Id: "lighting", Name: "Lighting", Panel: "group", Icon: "folder-generic", Parent: null, Description: "Track area lights" },
    {
      Id: "pit-rig",
      Name: "Pit Lane Overhead Rig",
      Panel: "light",
      Icon: "editor-area-light",
      Parent: "lighting",
      Preview: true,
      Description: "LTC Quad · 2.60 × 1.60 m",
    },
  ];

  const Values = {
    "asphalt-pad": {
      Position: [0, 0.5, -0.68],
      Rotation: [0, 0, 0],
      Scale: [14.0, 14.0, 0.16],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({ Type: "Standard", BaseLinear: [0.09, 0.095, 0.105], Roughness: 0.68 }),
    },
    "control-vehicle": {
      Position: [0.0, 0.65, -0.60],
      Rotation: [0, 0, -24],
      Scale: [0.78, 0.78, 0.78],
      "Cast shadows": true,
      GI: true,
      ...BuildMaterialRecord({
        Type: "Clear coat",
        BaseLinear: [0.88, 0.91, 0.97],
        Metallic: 0.22,
        Roughness: 0.14,
        Coat: 1.0,
        CoatRoughness: 0.03,
      }),
    },
    "traffic-cone-1": {
      Position: [-0.95, -0.85, -0.21],
      Rotation: [0, 0, 0],
      Scale: [0.48, 0.48, 0.78],
      "Cast shadows": true,
      GI: true,
      Material: BuildTrafficConeMaterial(),
    },
    "traffic-cone-2": {
      Position: [0.15, 0.25, -0.21],
      Rotation: [0, 0, 0],
      Scale: [0.48, 0.48, 0.78],
      "Cast shadows": true,
      GI: true,
      Material: BuildTrafficConeMaterial(),
    },
    "traffic-cone-3": {
      Position: [-0.82, 1.45, -0.21],
      Rotation: [0, 0, 0],
      Scale: [0.48, 0.48, 0.78],
      "Cast shadows": true,
      GI: true,
      Material: BuildTrafficConeMaterial(),
    },
    "cylinder-cone-1": {
      Position: [-1.75, -0.15, -0.16],
      Rotation: [0, 0, 0],
      Scale: [0.40, 0.40, 0.88],
      "Cast shadows": true,
      GI: true,
      Material: BuildCylinderConeMaterial(),
    },
    "cylinder-cone-2": {
      Position: [1.35, -0.35, -0.16],
      Rotation: [0, 0, 0],
      Scale: [0.40, 0.40, 0.88],
      "Cast shadows": true,
      GI: true,
      Material: BuildCylinderConeMaterial(),
    },
    "cylinder-cone-3": {
      Position: [1.18, 1.35, -0.16],
      Rotation: [0, 0, 0],
      Scale: [0.40, 0.40, 0.88],
      "Cast shadows": true,
      GI: true,
      Material: BuildCylinderConeMaterial(),
    },
    camera: {
      Position: [0.0, -4.35, 1.42],
      Target: [0.0, 0.35, -0.08],
      "Focal Length": CanonicalEditorCamera.FocalLengthMm,
      "Sensor Width": CanonicalEditorCamera.SensorWidthMm,
      Speed: 1.0,
    },
    post: { Exposure: 0.1 },
    sun: {
      Direction: [-0.46, -0.52, 0.72],
      Irradiance: [4.5, 4.15, 3.65],
      "Angular Radius": 0.0093,
      "Sky Radiance": [0.22, 0.30, 0.44],
    },
    "pit-rig": {
      Position: [0, 0.3, 2.35],
      Intensity: 7.2,
      Width: 2.6,
      Height: 1.6,
      EmittedRadiance: [7.2, 7.0, 6.6],
    },
  };

  return {
    Id: CustomId,
    Name: CustomName,
    Notes: CustomNotes || "Automotive chicane stage with Traffic Cones & Cylinder Cones.",
    Rows,
    Values,
    Hidden: {},
    Collapsed: {},
    Selected: "traffic-cone-2",
  };
}


export const LEVEL_TEMPLATES = Object.freeze([
  {
    Id: "lobby",
    Category: "Architectural",
    Badge: "WELCOME HALL",
    Spec: "13 objects · ControlVehicle + aircraft",
    Title: "Frontier Lobby",
    Subtitle: "Open welcome hall, reception desk, display car and aircraft under the physical sky",
  },
  {
    Id: "gallery",
    Category: "Architectural",
    Badge: "GI SHOWCASE",
    Spec: "17 entities · Softbox + Abbe Glass",
    Title: "Cornell-Studio Gallery",
    Subtitle: "Canonical colour-bleed enclosure with chrome, gold plinth & crown glass",
  },
  {
    Id: "force-lab",
    Category: "Simulation",
    Badge: "VECTORFIELD LAB",
    Spec: "3 forces · Wind + Vortex + Curl",
    Title: "Wind & Vortex ForceField Lab",
    Subtitle: "Cornell-Studio stage pre-wired with Linear Wind, Cyclonic Vortex & Curl Turbulence",
  },
  {
    Id: "slalom",
    Category: "Automotive",
    Badge: "CAR CHICANE",
    Spec: "6 cones · Traffic + Cylinder Cones",
    Title: "Automotive Cone Slalom",
    Subtitle: "Tarmac proving ground pre-staged with Traffic Cones & Cylinder Cones",
  },
  {
    Id: "lookdev",
    Category: "Look-Dev",
    Badge: "MATERIAL LAB",
    Spec: "12 entities · Cyclorama + Plinth",
    Title: "Look-Dev Plinth Studio",
    Subtitle: "Infinite cyclorama stage with Clear-Coat, Charlie Velvet & Subsurface spheres",
  },
  {
    Id: "empty",
    Category: "Blank",
    Badge: "CLEAN SLATE",
    Spec: "7 entities · Ground + Sun",
    Title: "Empty Proving Ground",
    Subtitle: "Minimal ground slab, calibration cube, directional Sun & Editor Camera",
  },
  {
    Id: "duplicate",
    Category: "Architectural",
    Badge: "CLONE ACTIVE",
    Spec: "1:1 snapshot · Active level",
    Title: "Duplicate Current Level",
    Subtitle: "Clone all entities, transforms, forces & 20-channel materials from active level",
  },
]);

function BuildForceFieldLabLevel(NewId, CleanName, Notes = "") {
  const Rows = Clone(ShowcaseInitialRows);
  const Values = Clone(ShowcaseInitialValues);
  Rows.push({
    Id: "forces",
    Name: "Forces",
    Panel: "group",
    Icon: "folder-world",
    Parent: null,
    Description: "19-Law VectorField SimulationSpace",
  });
  const Rig = BuildDefaultDiagnosticForceRig();
  for (const Node of Rig) {
    Rows.push({
      Id: Node.Id,
      Name: Node.Name,
      Panel: "force",
      ForceModel: Node.Force.Model,
      Icon: `force-model:${Node.Force.Model}`,
      Parent: "forces",
      Preview: true,
      Description: `${Node.Force.Model} · R=${Node.Force.Radius}m`,
    });
    Values[Node.Id] = {
      Powered: true,
      Position: [...Node.Position],
      Rotation: [...Node.Rotation],
      Scale: [1, 1, 1],
      Force: Clone(Node.Force),
    };
  }
  return {
    Id: NewId,
    Name: CleanName,
    Notes: Notes || "VectorField Studio Wind + Cyclonic Vortex + Curl Turbulence stage.",
    Rows,
    Values,
    Hidden: {},
    Collapsed: {},
    Selected: Rig[1]?.Id || Rig[0]?.Id,
  };
}

export function CreateNewLevelRecord({ Name, TemplateId, Notes = "", CurrentLevel }) {
  const CleanName = String(Name || "").trim() || "Untitled Level";
  const NewId =
    "level-" +
    CleanName.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") +
    "-" +
    Math.random().toString(36).slice(2, 6);

  if (TemplateId === "duplicate" && CurrentLevel) {
    const Copy = Clone(CurrentLevel);
    Copy.Id = NewId;
    Copy.Name = CleanName;
    Copy.Notes = Notes || `Duplicated from ${CurrentLevel.Name}`;
    if (Copy.Rows?.[0]?.Id === "scene") Copy.Rows[0].Name = CleanName;
    return EnsureRenderSettingsInLevel(Copy);
  }

  if (TemplateId === "lobby") {
    return EnsureRenderSettingsInLevel(BuildLobbyLevel(NewId, CleanName, Notes));
  }

  if (TemplateId === "gallery") {
    return EnsureRenderSettingsInLevel({
      Id: NewId,
      Name: CleanName,
      Notes: Notes || "Cornell-Studio Gallery level instance.",
      Rows: Clone(ShowcaseInitialRows),
      Values: Clone(ShowcaseInitialValues),
      Hidden: {},
      Collapsed: {},
      Selected: "chrome-sphere",
    });
  }

  if (TemplateId === "force-lab") {
    return EnsureRenderSettingsInLevel(BuildForceFieldLabLevel(NewId, CleanName, Notes));
  }

  if (TemplateId === "slalom") {
    return EnsureRenderSettingsInLevel(BuildAutomotiveConeSlalomLevel(NewId, CleanName, Notes));
  }

  if (TemplateId === "lookdev") {
    const Base = BuildLookDevStudioLevel();
    Base.Id = NewId;
    Base.Name = CleanName;
    if (Notes) Base.Notes = Notes;
    Base.Rows[0].Name = CleanName;
    return EnsureRenderSettingsInLevel(Base);
  }

  return EnsureRenderSettingsInLevel(BuildEmptyProvingGroundLevel(NewId, CleanName, Notes));
}

export function LoadSavedLevelsState() {
  const Defaults = BuildDefaultLevels();
  if (typeof window === "undefined") {
    return { ActiveLevelId: Defaults[0].Id, Levels: Defaults };
  }
  try {
    window.localStorage.removeItem("Frontier.WorldMirrorLevels.v1");
    window.localStorage.removeItem("Frontier.WorldMirrorLevels.v2");
    window.localStorage.removeItem("Frontier.WorldMirrorLevels.v3");
    window.localStorage.removeItem("Frontier.WorldMirrorLevels.v4");
    const Raw = window.localStorage.getItem(LEVEL_STORAGE_KEY);
    if (!Raw) return { ActiveLevelId: Defaults[0].Id, Levels: Defaults };
    const Parsed = JSON.parse(Raw);
    if (!Array.isArray(Parsed?.Levels) || Parsed.Levels.length === 0) {
      return { ActiveLevelId: Defaults[0].Id, Levels: Defaults };
    }
    const StoredLevels = Parsed.Levels.map((L) => EnsureRenderSettingsInLevel(L));
    const LobbyId = Defaults[0].Id;
    const StoredLobby = StoredLevels.find((L) => L?.Id === LobbyId);
    const NormalizedLevels = [
      StoredLobby || Defaults[0],
      ...StoredLevels.filter((L) => L?.Id !== LobbyId),
    ];
    const MigratingGalleryDefault =
      !StoredLobby && Parsed.ActiveLevelId === "level-showcase-gallery";
    const Active = MigratingGalleryDefault
      ? LobbyId
      : NormalizedLevels.some((L) => L?.Id === Parsed.ActiveLevelId)
        ? Parsed.ActiveLevelId
        : LobbyId;
    return { ActiveLevelId: Active, Levels: NormalizedLevels };
  } catch {
    return { ActiveLevelId: Defaults[0].Id, Levels: Defaults };
  }
}

export function ResetSavedLevelsState() {
  const Defaults = BuildDefaultLevels();
  const Fresh = { ActiveLevelId: Defaults[0].Id, Levels: Defaults };
  if (typeof window !== "undefined") {
    try {
      window.localStorage.removeItem("Frontier.WorldMirrorLevels.v1");
      window.localStorage.removeItem("Frontier.WorldMirrorLevels.v2");
      window.localStorage.removeItem(LEVEL_STORAGE_KEY);
    } catch {}
  }
  return Fresh;
}

export function SaveLevelsState(State) {
  if (typeof window === "undefined") return;
  try {
    window.localStorage.setItem(LEVEL_STORAGE_KEY, JSON.stringify(State));
  } catch {}
}
