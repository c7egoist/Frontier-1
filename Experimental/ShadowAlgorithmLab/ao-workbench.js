import {
  ExpandVehicleInstances,
  ParseVehicleFvb1Buffer,
  TLAS_RESERVED_NODES,
} from "../FrontierWorldMirror/VehicleBvhAssets.js";

const METHODS = [
  { id: 0, key: "udf", label: "UDF Clipmap", title: "Unsigned Distance Clipmap AO", blurb: "Sphere-traces positive distance to the nearest vehicle surface, without relying on nearest-triangle face signs. The exact floor plane is handled analytically to avoid false occluders on open or inconsistently wound meshes." },
  { id: 1, key: "ssao", label: "SSAO", title: "Screen-Space AO", blurb: "Rotated screen-space sample kernel over the position buffer. Fast, but can show screen-space noise and lose off-screen occluders." },
  { id: 2, key: "gtao", label: "GTAO", title: "Horizon / GTAO", blurb: "Screen-space horizon integration across angular slices. Usually smoother than a sparse SSAO kernel, but still depth-buffer limited." },
  { id: 3, key: "ray", label: "BVH Ray AO", title: "BVH Ray-Traced AO", blurb: "Cosine-hemisphere visibility rays through the source ControlVehicle BLAS. This is the slower geometry reference, not a beauty render." },
  { id: 4, key: "cacao", label: "CACAO*", title: "CACAO-style Adaptive AO", blurb: "An adaptive-sampling screen-space AO approximation inspired by AMD FidelityFX CACAO. It is a WebGL 2 port for comparison—not the native FidelityFX SDK, and not automatically better than GTAO." },
];

const GRID = [64, 40, 32];
const CLIP_CENTER = [0, 0, 0.88];
const FLOOR_Z = 0;
const TAU = Math.PI * 2;
const $ = (id) => document.getElementById(id);
const ui = {
  canvas: $("aoCanvas"),
  frame: $("canvasFrame"),
  tabs: $("methodTabs"),
  device: $("deviceLabel"),
  renderStatus: $("renderStatus"),
  resolution: $("resolutionLabel"),
  message: $("canvasMessage"),
  compareButton: $("compareButton"),
  panModeButton: $("panModeButton"),
  panModeHudButton: $("panModeHudButton"),
  frameVehicleHudButton: $("frameVehicleHudButton"),
  compareLegend: $("compareLegend"),
  benchmarkButton: $("benchmarkButton"),
  benchmarkStatus: $("benchmarkStatus"),
  resetView: $("resetViewButton"),
  rebuildButton: $("rebuildButton"),
  methodTitle: $("methodTitle"),
  methodDescription: $("methodDescription"),
  selectedCost: $("selectedCost"),
  costSource: $("costSource"),
  rebuildCost: $("rebuildCost"),
  rebuildDetail: $("rebuildDetail"),
  clipmapReadout: $("clipmapReadout"),
  progress: $("buildProgressBar"),
  progressText: $("buildProgressText"),
  costTable: $("costTable"),
  radius: $("radiusControl"),
  radiusValue: $("radiusValue"),
  samples: $("samplesControl"),
  samplesValue: $("samplesValue"),
  bias: $("biasControl"),
  biasValue: $("biasValue"),
  strength: $("strengthControl"),
  strengthValue: $("strengthValue"),
  voxel: $("voxelControl"),
  voxelValue: $("voxelValue"),
  levels: $("levelsControl"),
  levelsValue: $("levelsValue"),
  filter: $("filterControl"),
};

const state = {
  gl: null,
  floatColorExt: null,
  timerExt: null,
  ready: false,
  failed: false,
  method: 0,
  compare: false,
  panMode: false,
  radius: Number(ui.radius.value),
  samples: Number(ui.samples.value),
  bias: Number(ui.bias.value),
  strength: Number(ui.strength.value),
  voxelSize: Number(ui.voxel.value),
  levels: Number(ui.levels.value),
  linearUdf: ui.filter.checked,
  maxSteps: 24,
  renderScale: 0.66,
  width: 1,
  height: 1,
  targetWidth: 0,
  targetHeight: 0,
  camera: { yaw: -0.94, pitch: 0.25, distance: 10.3, target: [0, 0, 0.82], userMoved: false, fitAspect: 0 },
  vehicleBounds: null,
  pointer: null,
  program: null,
  gbufferProgram: null,
  gbuffer: null,
  meshVao: null,
  meshVbo: null,
  meshVertexCount: 0,
  assets: null,
  instances: [],
  instanceRoots: new Int32Array(9),
  udfTextures: [],
  udfFields: [],
  vehicleNodeTexture: null,
  vehicleTriTexture: null,
  vehicleInstanceTexture: null,
  buildId: 0,
  buildMs: null,
  buildCells: 0,
  buildInProgress: false,
  cost: METHODS.map(() => ({ gpuMs: null, submitMs: null, count: 0 })),
  pendingQueries: [],
  benchmark: null,
  frameCounter: 0,
  lastUiUpdate: 0,
  lastFrameTime: performance.now(),
};

function setMessage(text, visible = true) {
  ui.message.textContent = text;
  ui.message.hidden = !visible;
}

function clamp(x, a, b) { return Math.max(a, Math.min(b, x)); }
function dot3(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
function cross3(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
function normalize3(a) { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; }
function mat4Multiply(a, b) {
  const out = new Float32Array(16);
  for (let c = 0; c < 4; ++c) for (let r = 0; r < 4; ++r) {
    out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
  }
  return out;
}
function cameraMatrices(aspect) {
  const { yaw, pitch, distance, target } = state.camera;
  const cp = Math.cos(pitch), sp = Math.sin(pitch);
  const eye = [
    target[0] + distance * cp * Math.cos(yaw),
    target[1] + distance * cp * Math.sin(yaw),
    target[2] + distance * sp,
  ];
  const forward = normalize3([target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]]);
  let right = cross3(forward, [0, 0, 1]);
  if (Math.hypot(...right) < 1e-5) right = [1, 0, 0];
  right = normalize3(right);
  const up = normalize3(cross3(right, forward));
  const view = new Float32Array(16);
  view[0] = right[0]; view[4] = right[1]; view[8] = right[2]; view[12] = -dot3(right, eye);
  view[1] = up[0]; view[5] = up[1]; view[9] = up[2]; view[13] = -dot3(up, eye);
  view[2] = -forward[0]; view[6] = -forward[1]; view[10] = -forward[2]; view[14] = dot3(forward, eye);
  view[15] = 1;
  const fov = 46 * Math.PI / 180;
  const fy = 1 / Math.tan(fov * 0.5);
  const near = 0.05, far = 90;
  const proj = new Float32Array(16);
  proj[0] = fy / Math.max(0.1, aspect);
  proj[5] = fy;
  proj[10] = (far + near) / (near - far);
  proj[11] = -1;
  proj[14] = (2 * far * near) / (near - far);
  const viewProj = mat4Multiply(proj, view);
  return { eye, viewProj, projY: fy };
}

function shader(gl, type, source) {
  const sh = gl.createShader(type);
  gl.shaderSource(sh, source);
  gl.compileShader(sh);
  if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
    const log = gl.getShaderInfoLog(sh) || "unknown shader compile error";
    gl.deleteShader(sh);
    throw new Error(log);
  }
  return sh;
}
function program(gl, vsSource, fsSource) {
  const vs = shader(gl, gl.VERTEX_SHADER, vsSource);
  const fs = shader(gl, gl.FRAGMENT_SHADER, fsSource);
  const p = gl.createProgram();
  gl.attachShader(p, vs); gl.attachShader(p, fs); gl.linkProgram(p);
  gl.deleteShader(vs); gl.deleteShader(fs);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
    const log = gl.getProgramInfoLog(p) || "unknown program link error";
    gl.deleteProgram(p);
    throw new Error(log);
  }
  return p;
}

const GBUFFER_VS = `#version 300 es
precision highp float;
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
uniform mat4 uViewProj;
out vec3 vWorldPosition;
out vec3 vWorldNormal;
void main() {
  vWorldPosition = aPosition;
  vWorldNormal = normalize(aNormal);
  gl_Position = uViewProj * vec4(aPosition, 1.0);
}`;
const GBUFFER_FS = `#version 300 es
precision highp float;
in vec3 vWorldPosition;
in vec3 vWorldNormal;
layout(location=0) out vec4 outPosition;
layout(location=1) out vec4 outNormal;
void main() {
  outPosition = vec4(vWorldPosition, 1.0);
  outNormal = vec4(normalize(vWorldNormal), 1.0);
}`;
const FULLSCREEN_VS = `#version 300 es
precision highp float;
out vec2 vUv;
void main() {
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  vUv = p * 0.5;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}`;
const AO_FS = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler3D;
in vec2 vUv;
out vec4 outColor;

uniform sampler2D uPositionTex;
uniform sampler2D uNormalTex;
uniform sampler3D uUdf0;
uniform sampler3D uUdf1;
uniform sampler3D uUdf2;
uniform sampler2D uNodeTex;
uniform sampler2D uTriTex;
uniform sampler2D uInstanceTex;
uniform vec2 uResolution;
uniform vec3 uCameraPosition;
uniform mat4 uViewProj;
uniform float uProjectionY;
uniform float uRadius;
uniform float uBias;
uniform float uStrength;
uniform float uVoxelSize;
uniform int uSamples;
uniform int uSteps;
uniform int uMethod;
uniform int uClipmapLevels;
uniform int uNodeWidth;
uniform int uTriWidth;
uniform int uInstanceCount;
uniform int uInstanceRoot[9];
uniform vec3 uUdfCenter;
uniform vec3 uUdfHalfExtent[3];
uniform float uUdfRange[3];
uniform float uGroundZ;

const float PI = 3.141592653589793;
const float TAU = 6.283185307179586;

float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}
void basisFromNormal(vec3 n, out vec3 t, out vec3 b) {
  vec3 a = abs(n.z) < 0.92 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  t = normalize(cross(a, n));
  b = cross(n, t);
}
vec3 cosineDirection(vec3 n, int i, int count, float rotation) {
  float u = (float(i) + 0.5) / float(max(count, 1));
  float r = sqrt(u);
  float phi = float(i) * 2.399963229728653 + rotation;
  vec3 t, b;
  basisFromNormal(n, t, b);
  return normalize(t * (cos(phi) * r) + b * (sin(phi) * r) + n * sqrt(max(0.0, 1.0 - u)));
}
bool inGrid(vec3 uvw) {
  return all(greaterThanEqual(uvw, vec3(0.002))) && all(lessThanEqual(uvw, vec3(0.998)));
}
float clipmapEdgeDistance(vec3 uvw) {
  vec3 edge = min(uvw, 1.0 - uvw);
  return min(edge.x, min(edge.y, edge.z));
}
float sampleUdf(vec3 p, out float fieldCell) {
  float fieldDistance = max(uRadius, 1.0);
  fieldCell = uVoxelSize;
  bool hasField = false;

  // Start with the coarsest valid level, then blend finer levels in across their borders.
  if (uClipmapLevels > 2) {
    vec3 uv = (p - uUdfCenter) / (2.0 * uUdfHalfExtent[2]) + 0.5;
    if (inGrid(uv)) {
      fieldDistance = texture(uUdf2, uv).r * uUdfRange[2];
      fieldCell = uUdfRange[2] / 8.0;
      hasField = true;
    }
  }
  if (uClipmapLevels > 1) {
    vec3 uv = (p - uUdfCenter) / (2.0 * uUdfHalfExtent[1]) + 0.5;
    if (inGrid(uv)) {
      float fineDistance = texture(uUdf1, uv).r * uUdfRange[1];
      float fineCell = uUdfRange[1] / 8.0;
      if (hasField) {
        float blend = smoothstep(0.0, 0.10, clipmapEdgeDistance(uv));
        fieldDistance = mix(fieldDistance, fineDistance, blend);
        fieldCell = mix(fieldCell, fineCell, blend);
      } else {
        fieldDistance = fineDistance;
        fieldCell = fineCell;
        hasField = true;
      }
    }
  }
  if (uClipmapLevels > 0) {
    vec3 uv = (p - uUdfCenter) / (2.0 * uUdfHalfExtent[0]) + 0.5;
    if (inGrid(uv)) {
      float fineDistance = texture(uUdf0, uv).r * uUdfRange[0];
      float fineCell = uUdfRange[0] / 8.0;
      if (hasField) {
        float blend = smoothstep(0.0, 0.10, clipmapEdgeDistance(uv));
        fieldDistance = mix(fieldDistance, fineDistance, blend);
        fieldCell = mix(fieldCell, fineCell, blend);
      } else {
        fieldDistance = fineDistance;
        fieldCell = fineCell;
        hasField = true;
      }
    }
  }

  // The ground is an infinite plane, so evaluate it exactly instead of voxelizing it.
  return min(fieldDistance, abs(p.z - uGroundZ));
}
float udfClipmapAO(vec3 p, vec3 n, vec2 pixel) {
  int count = clamp(uSamples, 4, 32);
  float rotation = hash12(pixel) * TAU;
  float occSum = 0.0;
  float originOffset = max(uBias, uVoxelSize * 0.65);
  vec3 rayOrigin = p + n * originOffset;
  for (int i = 0; i < 32; ++i) {
    if (i >= count) break;
    vec3 rd = cosineDirection(n, i, count, rotation);
    float t = 0.0;
    float rayOcc = 0.0;
    float previousDistance = 0.0;
    float previousStep = 0.0;
    bool hasPrevious = false;
    for (int stepIdx = 0; stepIdx < 64; ++stepIdx) {
      if (stepIdx >= uSteps || t >= uRadius) break;
      float fieldCell;
      float d = sampleUdf(rayOrigin + rd * t, fieldCell);
      if (d < max(1e-4, fieldCell * 0.18)) {
        rayOcc = 1.0;
        break;
      }
      float approach = hasPrevious
        ? clamp((previousDistance - d) / max(previousStep, fieldCell * 0.1), 0.0, 1.0)
        : 0.0;
      float nearRatio = clamp(d / max(fieldCell * 0.45, t * 1.15), 0.0, 1.0);
      float falloff = clamp(1.0 - t / uRadius, 0.0, 1.0);
      rayOcc = max(rayOcc, (1.0 - nearRatio) * approach * falloff * 0.82);
      float stepLength = min(d * 0.75, min(0.48, uRadius - t));
      if (stepLength <= 1e-5) break;
      previousDistance = d;
      previousStep = stepLength;
      hasPrevious = true;
      t += stepLength;
    }
    occSum += rayOcc;
  }
  return clamp(1.0 - uStrength * occSum / float(count), 0.0, 1.0);
}
float screenPixelRadius(vec3 p) {
  float depth = max(0.25, length(p - uCameraPosition));
  return clamp(uRadius * uProjectionY * uResolution.y / (2.0 * depth), 2.0, 128.0);
}
float ssao(vec3 p, vec3 n, vec2 uv) {
  int count = clamp(uSamples, 4, 32);
  float pixRadius = screenPixelRadius(p);
  float rot = hash12(gl_FragCoord.xy) * TAU;
  float occ = 0.0;
  for (int i = 0; i < 32; ++i) {
    if (i >= count) break;
    float u = (float(i) + 0.5) / float(count);
    float a = rot + float(i) * 2.39996323;
    float r = sqrt(u) * pixRadius;
    vec2 duv = vec2(cos(a), sin(a)) * r / uResolution;
    vec2 suv = uv + duv;
    if (any(lessThan(suv, vec2(0.002))) || any(greaterThan(suv, vec2(0.998)))) continue;
    vec4 sampleP = texture(uPositionTex, suv);
    if (sampleP.a < 0.5) continue;
    vec3 delta = sampleP.xyz - p;
    float d = length(delta);
    if (d <= uBias || d >= uRadius) continue;
    float facing = max(0.0, dot(n, delta / max(d, 1e-5)) - 0.06);
    float rangeW = clamp(1.0 - d / uRadius, 0.0, 1.0);
    occ += facing * rangeW;
  }
  return clamp(1.0 - uStrength * occ / float(count) * 2.35, 0.0, 1.0);
}
float gtao(vec3 p, vec3 n, vec2 uv) {
  int directions = clamp(uSamples / 2, 4, 16);
  int steps = clamp(uSteps / 3, 4, 14);
  float pixRadius = screenPixelRadius(p);
  float rotation = hash12(gl_FragCoord.xy + vec2(19.0, 7.0)) * TAU;
  float occ = 0.0;
  for (int d = 0; d < 16; ++d) {
    if (d >= directions) break;
    float angle = rotation + float(d) * (TAU / float(directions));
    vec2 axis = vec2(cos(angle), sin(angle));
    float horizon = 0.0;
    for (int s = 1; s <= 14; ++s) {
      if (s > steps) break;
      float u = float(s) / float(steps);
      float stride = mix(0.18, 1.0, u * u) * pixRadius;
      vec2 suv = uv + axis * stride / uResolution;
      if (any(lessThan(suv, vec2(0.002))) || any(greaterThan(suv, vec2(0.998)))) continue;
      vec4 sampleP = texture(uPositionTex, suv);
      if (sampleP.a < 0.5) continue;
      vec3 delta = sampleP.xyz - p;
      float dist = length(delta);
      if (dist <= uBias || dist >= uRadius) continue;
      float elevation = max(0.0, dot(n, delta / max(dist, 1e-5)) - 0.03);
      float distanceWeight = clamp(1.0 - dist / uRadius, 0.0, 1.0);
      horizon = max(horizon, elevation * distanceWeight);
    }
    occ += horizon * (0.76 + 0.24 * horizon);
  }
  return clamp(1.0 - uStrength * occ / float(directions) * 1.9, 0.0, 1.0);
}

vec4 nodeFetch(int nodeIndex, int part) {
  int texel = nodeIndex * 2 + part;
  return texelFetch(uNodeTex, ivec2(texel % uNodeWidth, texel / uNodeWidth), 0);
}
vec4 triFetch(int triIndex, int part) {
  int texel = triIndex * 4 + part;
  return texelFetch(uTriTex, ivec2(texel % uTriWidth, texel / uTriWidth), 0);
}
bool rayBox(vec3 ro, vec3 rd, vec3 lo, vec3 hi, float maxT) {
  vec3 safeRd = vec3(
    abs(rd.x) < 1e-7 ? (rd.x < 0.0 ? -1e-7 : 1e-7) : rd.x,
    abs(rd.y) < 1e-7 ? (rd.y < 0.0 ? -1e-7 : 1e-7) : rd.y,
    abs(rd.z) < 1e-7 ? (rd.z < 0.0 ? -1e-7 : 1e-7) : rd.z
  );
  vec3 a = (lo - ro) / safeRd;
  vec3 b = (hi - ro) / safeRd;
  vec3 mn = min(a, b);
  vec3 mx = max(a, b);
  float nearT = max(mn.x, max(mn.y, mn.z));
  float farT = min(mx.x, min(mx.y, mx.z));
  return farT >= max(nearT, 0.0) && nearT < maxT;
}
float hitTriangle(vec3 ro, vec3 rd, vec3 a, vec3 b, vec3 c) {
  vec3 e1 = b - a, e2 = c - a;
  vec3 pv = cross(rd, e2);
  float det = dot(e1, pv);
  if (abs(det) < 1e-8) return 1e20;
  float invDet = 1.0 / det;
  vec3 tv = ro - a;
  float u = dot(tv, pv) * invDet;
  if (u < 0.0 || u > 1.0) return 1e20;
  vec3 qv = cross(tv, e1);
  float v = dot(rd, qv) * invDet;
  if (v < 0.0 || u + v > 1.0) return 1e20;
  float t = dot(e2, qv) * invDet;
  return t > 0.008 ? t : 1e20;
}
float traceInstance(vec3 ro, vec3 rd, float maxT, int instanceIdx) {
  vec4 w0 = texelFetch(uInstanceTex, ivec2(4, instanceIdx), 0);
  vec4 w1 = texelFetch(uInstanceTex, ivec2(5, instanceIdx), 0);
  vec4 w2 = texelFetch(uInstanceTex, ivec2(6, instanceIdx), 0);
  vec4 w3 = texelFetch(uInstanceTex, ivec2(7, instanceIdx), 0);
  mat4 w2l = mat4(w0, w1, w2, w3);
  vec3 lro = (w2l * vec4(ro, 1.0)).xyz;
  vec3 lrd = (w2l * vec4(rd, 0.0)).xyz;
  int stack[64];
  int top = 0;
  stack[top++] = uInstanceRoot[instanceIdx];
  float bestT = maxT;
  for (int visit = 0; visit < 256; ++visit) {
    if (top <= 0) break;
    int nodeIdx = stack[--top];
    vec4 n0 = nodeFetch(nodeIdx, 0);
    vec4 n1 = nodeFetch(nodeIdx, 1);
    if (!rayBox(lro, lrd, n0.xyz, n1.xyz, bestT)) continue;
    int firstOrChild = int(floor(n0.w + 0.5));
    int count = int(floor(n1.w + 0.5));
    if (count > 0) {
      for (int k = 0; k < 4; ++k) {
        if (k >= count) break;
        int triIdx = firstOrChild + k;
        vec3 a = triFetch(triIdx, 0).xyz;
        vec3 b = triFetch(triIdx, 1).xyz;
        vec3 c = triFetch(triIdx, 2).xyz;
        bestT = min(bestT, hitTriangle(lro, lrd, a, b, c));
      }
    } else {
      if (top < 62) stack[top++] = firstOrChild + 1;
      if (top < 63) stack[top++] = firstOrChild;
    }
  }
  return bestT;
}
bool traceVehicleAndGround(vec3 ro, vec3 rd, float maxT) {
  if (rd.z < -1e-5) {
    float floorT = (uGroundZ - ro.z) / rd.z;
    if (floorT > 0.008 && floorT < maxT) return true;
  }
  float closest = maxT;
  for (int i = 0; i < 9; ++i) {
    if (i >= uInstanceCount) break;
    closest = min(closest, traceInstance(ro, rd, closest, i));
  }
  return closest < maxT;
}
float rayAo(vec3 p, vec3 n, vec2 pixel) {
  int count = clamp(uSamples, 4, 32);
  float rotation = hash12(pixel + vec2(37.0, 11.0)) * TAU;
  float blocked = 0.0;
  for (int i = 0; i < 32; ++i) {
    if (i >= count) break;
    vec3 rd = cosineDirection(n, i, count, rotation);
    if (traceVehicleAndGround(p + n * uBias, rd, uRadius)) blocked += 1.0;
  }
  return clamp(1.0 - uStrength * blocked / float(count), 0.0, 1.0);
}

void cacaoAccumulateSample(vec3 center, vec3 normal, vec2 sampleUv, vec3 viewToCamera, float radius,
                           inout float obscuranceSum, inout float weightSum,
                           inout float momentSum, inout float momentSquareSum, inout float momentCount) {
  float obscurance = 0.0;
  float haloWeight = 1.0;
  if (all(greaterThanEqual(sampleUv, vec2(0.002))) && all(lessThanEqual(sampleUv, vec2(0.998)))) {
    vec4 samplePosition = texture(uPositionTex, sampleUv);
    if (samplePosition.a > 0.5) {
      vec3 delta = samplePosition.xyz - center;
      float distanceSq = dot(delta, delta);
      float radiusSq = radius * radius;
      if (distanceSq > uBias * uBias && distanceSq < radiusSq) {
        float distanceToSample = sqrt(distanceSq);
        float horizon = max(0.0, dot(normal, delta) / max(distanceToSample, 1e-5) - 0.06);
        float falloff = max(0.0, 1.0 - distanceSq / max(radiusSq, 1e-5));
        obscurance = horizon * falloff;
        haloWeight = 1.0 - 0.4 * clamp(max(0.0, dot(delta, viewToCamera)) / max(radius, 1e-4), 0.0, 1.0);
      }
    }
  }
  obscuranceSum += obscurance * haloWeight;
  weightSum += haloWeight;
  momentSum += obscurance;
  momentSquareSum += obscurance * obscurance;
  momentCount += 1.0;
}

void cacaoTap(vec3 center, vec3 normal, vec2 uv, vec3 viewToCamera, float radiusPixels, float radius,
              float rotation, int tapIndex, int maxSamples,
              inout float obscuranceSum, inout float weightSum,
              inout float momentSum, inout float momentSquareSum, inout float momentCount) {
  float radial = sqrt((float(tapIndex) + 0.5) / float(maxSamples)) * radiusPixels * 0.85;
  float angle = rotation + float(tapIndex) * 2.39996323;
  vec2 offset = vec2(cos(angle), sin(angle)) * radial / uResolution;
  cacaoAccumulateSample(center, normal, uv + offset, viewToCamera, radius,
                        obscuranceSum, weightSum, momentSum, momentSquareSum, momentCount);
  cacaoAccumulateSample(center, normal, uv - offset, viewToCamera, radius,
                        obscuranceSum, weightSum, momentSum, momentSquareSum, momentCount);
}

float cacaoDetailTerm(vec3 center, vec3 normal, vec3 neighbour, float radius) {
  vec3 delta = neighbour - center;
  float distanceSq = dot(delta, delta);
  float radiusSq = radius * radius;
  if (distanceSq <= uBias * uBias || distanceSq >= radiusSq * 0.25) return 0.0;
  float distanceToSample = sqrt(distanceSq);
  float horizon = max(0.0, dot(normal, delta) / max(distanceToSample, 1e-5) - 0.06);
  return horizon * max(0.0, 1.0 - 4.0 * distanceSq / max(radiusSq, 1e-5));
}

// Single-pass WebGL2 approximation of CACAO's adaptive, edge-aware sampling idea.
// It raises the per-pixel tap budget around depth/normal changes and noisy base estimates;
// AMD's native multi-pass FidelityFX implementation is not executed here.
float cacaoAdaptiveAO(vec3 p, vec3 n, vec2 uv) {
  int maxSamples = clamp(uSamples, 4, 32);
  int baseSamples = min(5, maxSamples);
  float radius = max(uRadius, uBias * 2.0);
  float depth = max(0.001, length(p - uCameraPosition));
  float radiusPixels = screenPixelRadius(p);
  vec3 viewToCamera = normalize(uCameraPosition - p);
  float rotation = hash12(gl_FragCoord.xy + vec2(13.0, 41.0)) * TAU;
  vec2 pixel = 1.0 / uResolution;

  vec4 pL = texture(uPositionTex, uv - vec2(pixel.x, 0.0));
  vec4 pR = texture(uPositionTex, uv + vec2(pixel.x, 0.0));
  vec4 pT = texture(uPositionTex, uv - vec2(0.0, pixel.y));
  vec4 pB = texture(uPositionTex, uv + vec2(0.0, pixel.y));
  bool validL = uv.x > pixel.x && pL.a > 0.5;
  bool validR = uv.x < 1.0 - pixel.x && pR.a > 0.5;
  bool validT = uv.y > pixel.y && pT.a > 0.5;
  bool validB = uv.y < 1.0 - pixel.y && pB.a > 0.5;
  float depthL = validL ? length(pL.xyz - uCameraPosition) : depth;
  float depthR = validR ? length(pR.xyz - uCameraPosition) : depth;
  float depthT = validT ? length(pT.xyz - uCameraPosition) : depth;
  float depthB = validB ? length(pB.xyz - uCameraPosition) : depth;

  float edgeL = 0.0, edgeR = 0.0, edgeT = 0.0, edgeB = 0.0;
  if (validL && validR) {
    float slopeChange = min(abs(depthL - depth), abs((depthL - depth) + (depthR - depth)));
    edgeL = edgeR = clamp(1.3 - slopeChange / max(depth * 0.04, 1e-4), 0.0, 1.0);
  }
  if (validT && validB) {
    float slopeChange = min(abs(depthT - depth), abs((depthT - depth) + (depthB - depth)));
    edgeT = edgeB = clamp(1.3 - slopeChange / max(depth * 0.04, 1e-4), 0.0, 1.0);
  }
  vec3 nL = validL ? normalize(texture(uNormalTex, uv - vec2(pixel.x, 0.0)).xyz) : n;
  vec3 nR = validR ? normalize(texture(uNormalTex, uv + vec2(pixel.x, 0.0)).xyz) : n;
  vec3 nT = validT ? normalize(texture(uNormalTex, uv - vec2(0.0, pixel.y)).xyz) : n;
  vec3 nB = validB ? normalize(texture(uNormalTex, uv + vec2(0.0, pixel.y)).xyz) : n;
  float normalChange = 0.0;
  normalChange += validL ? 1.0 - clamp(dot(n, nL), -1.0, 1.0) : 1.0;
  normalChange += validR ? 1.0 - clamp(dot(n, nR), -1.0, 1.0) : 1.0;
  normalChange += validT ? 1.0 - clamp(dot(n, nT), -1.0, 1.0) : 1.0;
  normalChange += validB ? 1.0 - clamp(dot(n, nB), -1.0, 1.0) : 1.0;
  float depthActivity = clamp(1.0 - 0.25 * (edgeL + edgeR + edgeT + edgeB), 0.0, 1.0);
  float normalActivity = clamp(normalChange * 0.425, 0.0, 1.0);
  float localImportance = clamp(depthActivity * 0.58 + normalActivity * 0.42, 0.0, 1.0);

  float obscuranceSum = 0.0;
  float weightSum = 0.0;
  float momentSum = 0.0;
  float momentSquareSum = 0.0;
  float momentCount = 0.0;
  for (int i = 0; i < 5; ++i) {
    if (i >= baseSamples) break;
    cacaoTap(p, n, uv, viewToCamera, radiusPixels, radius, rotation, i, maxSamples,
             obscuranceSum, weightSum, momentSum, momentSquareSum, momentCount);
  }
  float meanBase = momentSum / max(momentCount, 1.0);
  float variance = max(0.0, momentSquareSum / max(momentCount, 1.0) - meanBase * meanBase);
  float importance = clamp(max(localImportance, sqrt(variance) * 2.0 + meanBase * 0.75), 0.0, 1.0);
  int adaptiveSamples = min(maxSamples, baseSamples + int(ceil(float(maxSamples - baseSamples) * importance)));
  for (int i = 5; i < 32; ++i) {
    if (i >= adaptiveSamples) break;
    cacaoTap(p, n, uv, viewToCamera, radiusPixels, radius, rotation, i, maxSamples,
             obscuranceSum, weightSum, momentSum, momentSquareSum, momentCount);
  }

  float detailSum = 0.0;
  detailSum += validL ? cacaoDetailTerm(p, n, pL.xyz, radius) * edgeL : 0.0;
  detailSum += validR ? cacaoDetailTerm(p, n, pR.xyz, radius) * edgeR : 0.0;
  detailSum += validT ? cacaoDetailTerm(p, n, pT.xyz, radius) * edgeT : 0.0;
  detailSum += validB ? cacaoDetailTerm(p, n, pB.xyz, radius) * edgeB : 0.0;
  float detailWeight = max(edgeL + edgeR + edgeT + edgeB, 1e-3);
  float detail = detailSum / detailWeight;
  float meanObscurance = obscuranceSum / max(weightSum, 1e-4);
  float silhouetteFade = clamp(1.0 - depthActivity * 0.32, 0.65, 1.0);
  float visibilityLoss = clamp((meanObscurance * 2.6 + detail * 0.7) * uStrength * silhouetteFade, 0.0, 0.92);
  return 1.0 - visibilityLoss;
}

void main() {
  vec4 p4 = texture(uPositionTex, vUv);
  if (p4.a < 0.5) {
    outColor = vec4(vec3(0.032), 1.0);
    return;
  }
  vec3 n = normalize(texture(uNormalTex, vUv).xyz);
  float ao = 1.0;
  if (uMethod == 0) ao = udfClipmapAO(p4.xyz, n, gl_FragCoord.xy);
  else if (uMethod == 1) ao = ssao(p4.xyz, n, vUv);
  else if (uMethod == 2) ao = gtao(p4.xyz, n, vUv);
  else if (uMethod == 3) ao = rayAo(p4.xyz, n, gl_FragCoord.xy);
  else ao = cacaoAdaptiveAO(p4.xyz, n, vUv);
  float gray = pow(clamp(ao, 0.0, 1.0), 0.92);
  outColor = vec4(vec3(gray), 1.0);
}`;

function locs(gl, p, names) {
  const o = {};
  for (const name of names) o[name] = gl.getUniformLocation(p, name);
  return o;
}
function set1f(gl, location, value) { if (location !== null) gl.uniform1f(location, value); }
function set1i(gl, location, value) { if (location !== null) gl.uniform1i(location, value); }
function set3f(gl, location, x, y, z) { if (location !== null) gl.uniform3f(location, x, y, z); }
function makeTexture2D(gl, internal, format, type, width, height, filter = gl.NEAREST, data = null) {
  const t = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, t);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.texImage2D(gl.TEXTURE_2D, 0, internal, width, height, 0, format, type, data);
  return t;
}
function makeTexture3D(gl, width, height, depth, data, filter = gl.LINEAR) {
  const t = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_3D, t);
  gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MIN_FILTER, filter);
  gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MAG_FILTER, filter);
  gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);
  gl.texImage3D(gl.TEXTURE_3D, 0, gl.R8, width, height, depth, 0, gl.RED, gl.UNSIGNED_BYTE, data);
  return t;
}
function createGBuffer(gl, width, height) {
  const fbo = gl.createFramebuffer();
  gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
  const pos = makeTexture2D(gl, gl.RGBA16F, gl.RGBA, gl.HALF_FLOAT, width, height, gl.NEAREST);
  const norm = makeTexture2D(gl, gl.RGBA16F, gl.RGBA, gl.HALF_FLOAT, width, height, gl.NEAREST);
  gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, pos, 0);
  gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, gl.TEXTURE_2D, norm, 0);
  const depth = gl.createRenderbuffer();
  gl.bindRenderbuffer(gl.RENDERBUFFER, depth);
  gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, width, height);
  gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, depth);
  gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1]);
  const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  if (status !== gl.FRAMEBUFFER_COMPLETE) throw new Error(`Float G-buffer incomplete (0x${status.toString(16)}). This demo needs WebGL 2 float render targets.`);
  return { fbo, pos, norm, depth, width, height };
}

function decodeOct(packedFloat) {
  const packed = Math.round(packedFloat);
  const u = ((packed & 1023) / 1023) * 2 - 1;
  const v = (((packed >> 10) & 1023) / 1023) * 2 - 1;
  let x = u, y = v, z = 1 - Math.abs(u) - Math.abs(v);
  if (z < 0) {
    const ox = x, oy = y;
    x = (1 - Math.abs(oy)) * (ox >= 0 ? 1 : -1);
    y = (1 - Math.abs(ox)) * (oy >= 0 ? 1 : -1);
  }
  return normalize3([x, y, z]);
}
function transformPoint(m, p) {
  return [m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13], m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]];
}
function transformNormal(m, n) {
  return normalize3([m[0] * n[0] + m[4] * n[1] + m[8] * n[2], m[1] * n[0] + m[5] * n[1] + m[9] * n[2], m[2] * n[0] + m[6] * n[1] + m[10] * n[2]]);
}
function buildVehicleGeometry(assets, instances) {
  const data = [];
  const push = (p, n) => data.push(p[0], p[1], p[2], n[0], n[1], n[2]);
  for (const inst of instances) {
    const mesh = assets.meshMeta[inst.meshIdx];
    for (let t = mesh.triOffset; t < mesh.triOffset + mesh.triCount; ++t) {
      const b = t * 16;
      const p0 = [assets.triTexData[b], assets.triTexData[b + 1], assets.triTexData[b + 2]];
      const p1 = [assets.triTexData[b + 4], assets.triTexData[b + 5], assets.triTexData[b + 6]];
      const p2 = [assets.triTexData[b + 8], assets.triTexData[b + 9], assets.triTexData[b + 10]];
      const n0 = decodeOct(assets.triTexData[b + 7]);
      const n1 = decodeOct(assets.triTexData[b + 11]);
      const n2 = decodeOct(assets.triTexData[b + 15]);
      push(transformPoint(inst.l2w, p0), transformNormal(inst.l2w, n0));
      push(transformPoint(inst.l2w, p1), transformNormal(inst.l2w, n1));
      push(transformPoint(inst.l2w, p2), transformNormal(inst.l2w, n2));
    }
  }
  const floorZ = FLOOR_Z;
  const extent = 24;
  const floorN = [0, 0, 1];
  push([-extent, -extent, floorZ], floorN); push([extent, -extent, floorZ], floorN); push([extent, extent, floorZ], floorN);
  push([-extent, -extent, floorZ], floorN); push([extent, extent, floorZ], floorN); push([-extent, extent, floorZ], floorN);
  return new Float32Array(data);
}
function makeWorldBounds(localBounds, l2w) {
  const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (let c = 0; c < 8; ++c) {
    const p = [c & 1 ? localBounds.max[0] : localBounds.min[0], c & 2 ? localBounds.max[1] : localBounds.min[1], c & 4 ? localBounds.max[2] : localBounds.min[2]];
    const w = transformPoint(l2w, p);
    for (let a = 0; a < 3; ++a) { lo[a] = Math.min(lo[a], w[a]); hi[a] = Math.max(hi[a], w[a]); }
  }
  return { lo, hi };
}
function computeVehicleBounds(assets, instances) {
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  for (const inst of instances) {
    const local = assets.meshLocalBounds[inst.meshIdx];
    if (!local) continue;
    const bounds = makeWorldBounds(local, inst.l2w);
    for (let axis = 0; axis < 3; ++axis) {
      min[axis] = Math.min(min[axis], bounds.lo[axis]);
      max[axis] = Math.max(max[axis], bounds.hi[axis]);
    }
  }
  const center = min.map((value, axis) => (value + max[axis]) * 0.5);
  const half = min.map((value, axis) => (max[axis] - value) * 0.5);
  return { min, max, center, radius: Math.hypot(...half) };
}
function fitCameraToVehicle(aspect = null) {
  if (!state.vehicleBounds) return;
  const rect = ui.canvas.getBoundingClientRect();
  const viewAspect = aspect || rect.width / Math.max(1, rect.height) || 1;
  const fovY = 46 * Math.PI / 180;
  const fovX = 2 * Math.atan(Math.tan(fovY * 0.5) * viewAspect);
  const limitingHalfFov = Math.min(fovY, fovX) * 0.5;
  state.camera.target = state.vehicleBounds.center.slice();
  state.camera.distance = Math.max(0.5, state.vehicleBounds.radius) / Math.sin(limitingHalfFov) * 1.85;
  state.camera.fitAspect = viewAspect;
  state.camera.userMoved = false;
}
function panCamera(dx, dy) {
  const rect = ui.canvas.getBoundingClientRect();
  const height = Math.max(1, rect.height);
  const yaw = state.camera.yaw;
  const pitch = state.camera.pitch;
  const right = [-Math.sin(yaw), Math.cos(yaw), 0];
  const up = [-Math.sin(pitch) * Math.cos(yaw), -Math.sin(pitch) * Math.sin(yaw), Math.cos(pitch)];
  const worldPerPixel = 2 * state.camera.distance * Math.tan(23 * Math.PI / 180) / height;
  for (let axis = 0; axis < 3; ++axis) {
    state.camera.target[axis] += (-right[axis] * dx + up[axis] * dy) * worldPerPixel;
  }
}
function pointAabbDistanceSq(x, y, z, bounds) {
  const dx = x < bounds.lo[0] ? bounds.lo[0] - x : x > bounds.hi[0] ? x - bounds.hi[0] : 0;
  const dy = y < bounds.lo[1] ? bounds.lo[1] - y : y > bounds.hi[1] ? y - bounds.hi[1] : 0;
  const dz = z < bounds.lo[2] ? bounds.lo[2] - z : z > bounds.hi[2] ? z - bounds.hi[2] : 0;
  return dx * dx + dy * dy + dz * dz;
}
function closestPointTriangle(px, py, pz, triData, b, out) {
  const ax = triData[b], ay = triData[b + 1], az = triData[b + 2];
  const bx = triData[b + 4], by = triData[b + 5], bz = triData[b + 6];
  const cx = triData[b + 8], cy = triData[b + 9], cz = triData[b + 10];
  const abx = bx - ax, aby = by - ay, abz = bz - az;
  const acx = cx - ax, acy = cy - ay, acz = cz - az;
  const apx = px - ax, apy = py - ay, apz = pz - az;
  const d1 = abx * apx + aby * apy + abz * apz;
  const d2 = acx * apx + acy * apy + acz * apz;
  let qx, qy, qz;
  if (d1 <= 0 && d2 <= 0) { qx = ax; qy = ay; qz = az; }
  else {
    const bpx = px - bx, bpy = py - by, bpz = pz - bz;
    const d3 = abx * bpx + aby * bpy + abz * bpz;
    const d4 = acx * bpx + acy * bpy + acz * bpz;
    if (d3 >= 0 && d4 <= d3) { qx = bx; qy = by; qz = bz; }
    else {
      const vc = d1 * d4 - d3 * d2;
      if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        const v = d1 / (d1 - d3); qx = ax + v * abx; qy = ay + v * aby; qz = az + v * abz;
      } else {
        const cpx = px - cx, cpy = py - cy, cpz = pz - cz;
        const d5 = abx * cpx + aby * cpy + abz * cpz;
        const d6 = acx * cpx + acy * cpy + acz * cpz;
        if (d6 >= 0 && d5 <= d6) { qx = cx; qy = cy; qz = cz; }
        else {
          const vb = d5 * d2 - d1 * d6;
          if (vb <= 0 && d2 >= 0 && d6 <= 0) {
            const w = d2 / (d2 - d6); qx = ax + w * acx; qy = ay + w * acy; qz = az + w * acz;
          } else {
            const va = d3 * d6 - d5 * d4;
            if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
              const w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
              qx = bx + w * (cx - bx); qy = by + w * (cy - by); qz = bz + w * (cz - bz);
            } else {
              const denom = 1 / (va + vb + vc);
              const v = vb * denom, w = vc * denom;
              qx = ax + abx * v + acx * w; qy = ay + aby * v + acy * w; qz = az + abz * v + acz * w;
            }
          }
        }
      }
    }
  }
  const dx = px - qx, dy = py - qy, dz = pz - qz;
  out.qx = qx; out.qy = qy; out.qz = qz;
  out.d2 = dx * dx + dy * dy + dz * dz;
}
function closestUnsignedDistance(px, py, pz, assets, root, maxDistance, scratch) {
  const nodes = assets.nodeTexData;
  const tris = assets.triTexData;
  const maxSq = maxDistance * maxDistance;
  let bestSq = maxSq;
  const stack = scratch.stack;
  let top = 0;
  stack[top++] = root;
  while (top > 0) {
    const node = stack[--top];
    const b = node * 8;
    const dx = px < nodes[b] ? nodes[b] - px : px > nodes[b + 4] ? px - nodes[b + 4] : 0;
    const dy = py < nodes[b + 1] ? nodes[b + 1] - py : py > nodes[b + 5] ? py - nodes[b + 5] : 0;
    const dz = pz < nodes[b + 2] ? nodes[b + 2] - pz : pz > nodes[b + 6] ? pz - nodes[b + 6] : 0;
    if (dx * dx + dy * dy + dz * dz >= bestSq) continue;
    const first = Math.round(nodes[b + 3]);
    const count = Math.round(nodes[b + 7]);
    if (count > 0) {
      for (let k = 0; k < count; ++k) {
        closestPointTriangle(px, py, pz, tris, (first + k) * 16, scratch.closest);
        if (scratch.closest.d2 < bestSq) bestSq = scratch.closest.d2;
      }
    } else {
      if (top < 62) stack[top++] = first + 1;
      if (top < 63) stack[top++] = first;
    }
  }
  return Math.sqrt(bestSq);
}
function minimumWorldScale(m) {
  const sx = Math.hypot(m[0], m[1], m[2]);
  const sy = Math.hypot(m[4], m[5], m[6]);
  const sz = Math.hypot(m[8], m[9], m[10]);
  return Math.max(1e-6, Math.min(sx, sy, sz));
}
function transformWorldToLocal(m, x, y, z, out) {
  out[0] = m[0] * x + m[4] * y + m[8] * z + m[12];
  out[1] = m[1] * x + m[5] * y + m[9] * z + m[13];
  out[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
}
function nextFrame() { return new Promise((resolve) => requestAnimationFrame(resolve)); }

async function rebuildClipmaps() {
  if (!state.ready || !state.assets || state.buildInProgress) return;
  const token = ++state.buildId;
  state.buildInProgress = true;
  ui.rebuildButton.disabled = true;
  ui.progress.style.width = "0%";
  ui.progressText.textContent = "Building unsigned-distance clipmaps…";
  ui.renderStatus.textContent = "Building AO clipmaps";
  const started = performance.now();
  const assets = state.assets;
  const gl = state.gl;
  const dims = GRID;
  const clipFields = [];
  const count = state.levels;
  const totalSlices = count * dims[2];
  const scratch = { stack: new Int32Array(64), closest: {} };
  const local = new Float32Array(3);
  const clips = state.instances.map((inst) => ({
    inst,
    root: TLAS_RESERVED_NODES + assets.meshMeta[inst.meshIdx].nodeOffset,
    bounds: makeWorldBounds(assets.meshLocalBounds[inst.meshIdx], inst.l2w),
    distanceScale: minimumWorldScale(inst.l2w),
  }));
  let completedSlices = 0;
  let builtCells = 0;
  try {
    for (let level = 0; level < count; ++level) {
      const cell = state.voxelSize * (1 << level);
      const half = [dims[0] * cell * 0.5, dims[1] * cell * 0.5, dims[2] * cell * 0.5];
      const range = cell * 8.0;
      const bytes = new Uint8Array(dims[0] * dims[1] * dims[2]);
      for (let z = 0; z < dims[2]; ++z) {
        if (token !== state.buildId) return;
        const wz = CLIP_CENTER[2] - half[2] + (z + 0.5) * cell;
        for (let y = 0; y < dims[1]; ++y) {
          const wy = CLIP_CENTER[1] - half[1] + (y + 0.5) * cell;
          for (let x = 0; x < dims[0]; ++x) {
            const wx = CLIP_CENTER[0] - half[0] + (x + 0.5) * cell;
            let distance = range;
            for (const clip of clips) {
              const bestWorldDistance = Math.min(range, distance);
              if (pointAabbDistanceSq(wx, wy, wz, clip.bounds) >= bestWorldDistance * bestWorldDistance) continue;
              transformWorldToLocal(clip.inst.w2l, wx, wy, wz, local);
              const maxLocalDistance = bestWorldDistance / clip.distanceScale;
              const localDistance = closestUnsignedDistance(local[0], local[1], local[2], assets, clip.root, maxLocalDistance, scratch);
              distance = Math.min(distance, localDistance * clip.distanceScale);
            }
            const encoded = clamp(distance / range, 0, 1);
            // Floor quantization keeps the stored UDF from rounding up across thin features.
            bytes[(z * dims[1] + y) * dims[0] + x] = Math.floor(encoded * 255);
          }
        }
        ++completedSlices;
        builtCells += dims[0] * dims[1];
        ui.progress.style.width = `${Math.round(100 * completedSlices / totalSlices)}%`;
        ui.progressText.textContent = `Cascade ${level + 1}/${count} · slice ${z + 1}/${dims[2]}`;
        if ((z & 1) === 1) await nextFrame();
      }
      if (token !== state.buildId) return;
      const texture = makeTexture3D(gl, dims[0], dims[1], dims[2], bytes, state.linearUdf ? gl.LINEAR : gl.NEAREST);
      clipFields.push({ texture, half, range, cell, bytes });
    }
    if (token !== state.buildId) return;
    for (const old of state.udfTextures) gl.deleteTexture(old);
    state.udfFields = clipFields;
    state.udfTextures = clipFields.map((field) => field.texture);
    state.buildMs = performance.now() - started;
    state.buildCells = builtCells;
    ui.progress.style.width = "100%";
    ui.progressText.textContent = `${builtCells.toLocaleString()} cells ready`;
    ui.rebuildCost.textContent = `${state.buildMs.toFixed(0)} ms`;
    ui.rebuildDetail.textContent = `${count} levels · CPU build`;
    const ranges = clipFields.map((f, i) => `L${i}: ${f.cell.toFixed(2)} m cells`).join(" · ");
    ui.clipmapReadout.textContent = `${ranges} · ${builtCells.toLocaleString()} voxels · ${((builtCells) / 1e6).toFixed(2)} M samples`;
    ui.renderStatus.textContent = "AO clipmaps ready";
    updateCosts();
  } catch (error) {
    console.error(error);
    ui.progressText.textContent = "Clipmap build failed";
    ui.renderStatus.textContent = "Clipmap build failed";
    setMessage(`The UDF clipmap build failed: ${error.message}`, true);
  } finally {
    if (token === state.buildId) {
      state.buildInProgress = false;
      ui.rebuildButton.disabled = false;
    }
  }
}

function uploadVehicleTextures(gl, assets, instances) {
  const node = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, node);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, assets.nodeTexWidth, assets.nodeTexHeight, 0, gl.RGBA, gl.FLOAT, assets.nodeTexData);

  const tri = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, tri);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, assets.triTexWidth, assets.triTexHeight, 0, gl.RGBA, gl.FLOAT, assets.triTexData);

  const instanceData = new Float32Array(10 * instances.length * 4);
  for (let i = 0; i < instances.length; ++i) {
    const inst = instances[i], base = i * 40;
    instanceData.set(inst.l2w, base);
    instanceData.set(inst.w2l, base + 16);
    instanceData[base + 32] = inst.matOverride + 1;
    instanceData[base + 33] = inst.pickCode;
    instanceData[base + 34] = inst.castShadows ? 1 : 0;
    instanceData[base + 35] = inst.coat;
    instanceData[base + 36] = inst.paint[0];
    instanceData[base + 37] = inst.paint[1];
    instanceData[base + 38] = inst.paint[2];
    instanceData[base + 39] = inst.rough;
  }
  const instance = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, instance);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, 10, instances.length, 0, gl.RGBA, gl.FLOAT, instanceData);
  return { node, tri, instance };
}

function bindTexture2D(gl, unit, texture, uniformLocation) {
  gl.activeTexture(gl.TEXTURE0 + unit);
  gl.bindTexture(gl.TEXTURE_2D, texture);
  if (uniformLocation !== null) gl.uniform1i(uniformLocation, unit);
}
function bindTexture3D(gl, unit, texture, uniformLocation) {
  gl.activeTexture(gl.TEXTURE0 + unit);
  gl.bindTexture(gl.TEXTURE_3D, texture);
  if (uniformLocation !== null) gl.uniform1i(uniformLocation, unit);
}

function updateCosts() {
  const timingHeader = state.timerExt ? "GPU ms" : "Submit ms";
  const rows = [
    `<div class="cost-row header"><span>AO method</span><span>${timingHeader}</span><span>Build</span></div>`,
  ];
  for (const method of METHODS) {
    const c = state.cost[method.id];
    const measured = state.timerExt ? c.gpuMs : c.submitMs;
    const measuredText = measured === null ? "—" : `${measured.toFixed(2)}`;
    const buildText = method.id === 0 && state.buildMs !== null ? `${(state.buildMs / 1000).toFixed(2)} s` : "—";
    rows.push(`<div class="cost-row ${method.id === state.method ? "active" : ""}" data-method="${method.id}"><span class="cost-name"><i class="cost-dot"></i>${method.label}</span><span class="cost-gpu">${measuredText}</span><span class="cost-build">${buildText}</span></div>`);
  }
  ui.costTable.innerHTML = rows.join("");
  const current = state.cost[state.method];
  const measured = state.timerExt ? current.gpuMs : current.submitMs;
  ui.selectedCost.textContent = measured === null ? "—" : `${measured.toFixed(2)} ms`;
  ui.costSource.textContent = state.timerExt ? "GPU AO pass / frame" : "CPU submit / frame";
}

function setMethod(id) {
  state.method = id;
  const method = METHODS[id];
  for (const button of ui.tabs.querySelectorAll("button")) {
    const active = Number(button.dataset.method) === id;
    button.classList.toggle("active", active);
    button.setAttribute("aria-selected", String(active));
  }
  ui.methodTitle.textContent = method.title;
  ui.methodDescription.textContent = method.blurb;
  updateCosts();
}
function createTabs() {
  ui.tabs.innerHTML = METHODS.map((m) => `<button class="method-tab ${m.id === state.method ? "active" : ""}" type="button" role="tab" aria-selected="${m.id === state.method}" data-method="${m.id}">${m.label}</button>`).join("");
  ui.tabs.addEventListener("click", (event) => {
    const button = event.target.closest("button[data-method]");
    if (!button) return;
    setMethod(Number(button.dataset.method));
  });
  ui.compareLegend.innerHTML = METHODS.map((m) => `<span>${m.label}</span>`).join("");
}

function resizeCanvas() {
  if (!state.gl) return;
  const rect = ui.canvas.getBoundingClientRect();
  const aspect = rect.width / Math.max(1, rect.height);
  if (state.vehicleBounds && !state.camera.userMoved && Math.abs(aspect - state.camera.fitAspect) > 0.01) {
    fitCameraToVehicle(aspect);
  }
  const dpr = Math.min(window.devicePixelRatio || 1, 1.5);
  const w = Math.max(1, Math.round(rect.width * dpr * state.renderScale));
  const h = Math.max(1, Math.round(rect.height * dpr * state.renderScale));
  if (ui.canvas.width !== w || ui.canvas.height !== h) {
    ui.canvas.width = w;
    ui.canvas.height = h;
    state.width = w;
    state.height = h;
    ui.resolution.textContent = `${w} × ${h} · ${Math.round(state.renderScale * 100)}%`;
    if (state.gbuffer) {
      state.gl.deleteFramebuffer(state.gbuffer.fbo);
      state.gl.deleteTexture(state.gbuffer.pos);
      state.gl.deleteTexture(state.gbuffer.norm);
      state.gl.deleteRenderbuffer(state.gbuffer.depth);
    }
    state.gbuffer = createGBuffer(state.gl, w, h);
  }
}

function renderGBuffer() {
  const gl = state.gl;
  const camera = cameraMatrices(state.width / Math.max(1, state.height));
  gl.bindFramebuffer(gl.FRAMEBUFFER, state.gbuffer.fbo);
  gl.viewport(0, 0, state.width, state.height);
  gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1]);
  gl.enable(gl.DEPTH_TEST);
  gl.depthFunc(gl.LEQUAL);
  gl.disable(gl.BLEND);
  gl.clearColor(0, 0, 0, 0);
  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  gl.useProgram(state.gbufferProgram);
  gl.uniformMatrix4fv(state.gbufferLoc.uViewProj, false, camera.viewProj);
  gl.bindVertexArray(state.meshVao);
  gl.drawArrays(gl.TRIANGLES, 0, state.meshVertexCount);
  gl.bindVertexArray(null);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  return camera;
}

function pollQueries() {
  const gl = state.gl;
  if (!state.timerExt || !state.pendingQueries.length) return;
  const disjoint = gl.getParameter(state.timerExt.GPU_DISJOINT_EXT);
  const keep = [];
  for (const item of state.pendingQueries) {
    if (!gl.getQueryParameter(item.query, gl.QUERY_RESULT_AVAILABLE)) {
      keep.push(item);
      continue;
    }
    if (!disjoint) {
      const ms = gl.getQueryParameter(item.query, gl.QUERY_RESULT) / 1e6;
      const record = state.cost[item.method];
      record.gpuMs = record.gpuMs === null ? ms : record.gpuMs * 0.82 + ms * 0.18;
      record.count += 1;
    }
    gl.deleteQuery(item.query);
  }
  state.pendingQueries = keep;
}
function beginQuery(method) {
  if (!state.timerExt) return null;
  const gl = state.gl;
  const q = gl.createQuery();
  gl.beginQuery(state.timerExt.TIME_ELAPSED_EXT, q);
  return { query: q, method };
}
function endQuery(q) {
  if (!q) return;
  const gl = state.gl;
  gl.endQuery(state.timerExt.TIME_ELAPSED_EXT);
  state.pendingQueries.push(q);
}

function drawAoPass(method, camera, viewport, measure = true) {
  const gl = state.gl;
  const p = state.program;
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.viewport(viewport.x, viewport.y, viewport.w, viewport.h);
  gl.disable(gl.DEPTH_TEST);
  gl.disable(gl.BLEND);
  gl.useProgram(p);
  bindTexture2D(gl, 0, state.gbuffer.pos, state.aoLoc.uPositionTex);
  bindTexture2D(gl, 1, state.gbuffer.norm, state.aoLoc.uNormalTex);
  const fallback = state.udfTextures[0] || state.whiteUdf;
  bindTexture3D(gl, 2, state.udfTextures[0] || fallback, state.aoLoc.uUdf0);
  bindTexture3D(gl, 3, state.udfTextures[1] || fallback, state.aoLoc.uUdf1);
  bindTexture3D(gl, 4, state.udfTextures[2] || fallback, state.aoLoc.uUdf2);
  bindTexture2D(gl, 5, state.vehicleNodeTexture, state.aoLoc.uNodeTex);
  bindTexture2D(gl, 6, state.vehicleTriTexture, state.aoLoc.uTriTex);
  bindTexture2D(gl, 7, state.vehicleInstanceTexture, state.aoLoc.uInstanceTex);
  set1i(gl, state.aoLoc.uMethod, method);
  set1i(gl, state.aoLoc.uSamples, state.samples);
  set1i(gl, state.aoLoc.uSteps, state.maxSteps);
  set1i(gl, state.aoLoc.uClipmapLevels, state.udfFields.length);
  set1i(gl, state.aoLoc.uNodeWidth, state.assets.nodeTexWidth);
  set1i(gl, state.aoLoc.uTriWidth, state.assets.triTexWidth);
  set1i(gl, state.aoLoc.uInstanceCount, state.instances.length);
  if (state.aoLoc.uInstanceRoot !== null) gl.uniform1iv(state.aoLoc.uInstanceRoot, state.instanceRoots);
  if (state.aoLoc.uUdfCenter !== null) gl.uniform3fv(state.aoLoc.uUdfCenter, new Float32Array(CLIP_CENTER));
  if (state.aoLoc.uUdfHalfExtent !== null) {
    const flat = new Float32Array(9);
    for (let i = 0; i < 3; ++i) {
      const half = state.udfFields[i]?.half || [1, 1, 1];
      flat.set(half, i * 3);
    }
    gl.uniform3fv(state.aoLoc.uUdfHalfExtent, flat);
  }
  if (state.aoLoc.uUdfRange !== null) {
    const ranges = new Float32Array(3);
    for (let i = 0; i < 3; ++i) ranges[i] = state.udfFields[i]?.range || 1;
    gl.uniform1fv(state.aoLoc.uUdfRange, ranges);
  }
  set1f(gl, state.aoLoc.uRadius, state.radius);
  set1f(gl, state.aoLoc.uBias, state.bias);
  set1f(gl, state.aoLoc.uStrength, state.strength);
  set1f(gl, state.aoLoc.uVoxelSize, state.voxelSize);
  set1f(gl, state.aoLoc.uGroundZ, FLOOR_Z);
  set2f(gl, state.aoLoc.uResolution, state.width, state.height);
  set3f(gl, state.aoLoc.uCameraPosition, camera.eye[0], camera.eye[1], camera.eye[2]);
  if (state.aoLoc.uViewProj !== null) gl.uniformMatrix4fv(state.aoLoc.uViewProj, false, camera.viewProj);
  set1f(gl, state.aoLoc.uProjectionY, camera.projY);
  const start = measure ? performance.now() : 0;
  const q = measure ? beginQuery(method) : null;
  gl.drawArrays(gl.TRIANGLES, 0, 3);
  if (measure) {
    endQuery(q);
    const submitMs = performance.now() - start;
    const record = state.cost[method];
    record.submitMs = record.submitMs === null ? submitMs : record.submitMs * 0.82 + submitMs * 0.18;
    if (!q) record.count += 1;
  }
}
function set2f(gl, loc, x, y) { if (loc !== null) gl.uniform2f(loc, x, y); }

function drawFrame() {
  if (!state.ready || state.failed) return;
  resizeCanvas();
  if (state.width < 2 || state.height < 2) return;
  const gl = state.gl;
  const camera = renderGBuffer();
  pollQueries();
  if (state.benchmark) {
    const bench = state.benchmark;
    const id = bench.methods[bench.index];
    drawAoPass(id, camera, { x: 0, y: 0, w: state.width, h: state.height });
    --bench.remaining;
    if (bench.remaining <= 0) {
      ++bench.index;
      if (bench.index >= bench.methods.length) {
        state.benchmark = null;
        ui.benchmarkButton.disabled = false;
        ui.benchmarkButton.textContent = "Benchmark methods";
        ui.benchmarkStatus.textContent = state.timerExt
          ? "Benchmark complete. Values are moving-average GPU AO-pass costs."
          : "Benchmark complete. Values are CPU draw-submit timings, not GPU execution times.";
      } else {
        bench.remaining = bench.framesPerMethod;
        ui.benchmarkStatus.textContent = `Benchmarking ${METHODS[bench.methods[bench.index]].label} · method ${bench.index + 1}/${bench.methods.length}`;
      }
    }
  } else if (state.compare) {
    const panelW = Math.floor(state.width / 3);
    const panelH = Math.floor(state.height / 3);
    const sideInset = Math.floor(state.width / 6);
    const verticalInset = Math.floor(state.height / 6);
    const upperRowY = state.height - verticalInset - panelH;
    const viewports = [
      { x: 0, y: upperRowY, w: panelW, h: panelH },
      { x: panelW, y: upperRowY, w: panelW, h: panelH },
      { x: panelW * 2, y: upperRowY, w: state.width - panelW * 2, h: panelH },
      { x: sideInset, y: verticalInset, w: panelW, h: panelH },
      { x: sideInset + panelW, y: verticalInset, w: panelW, h: panelH },
    ];
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, state.width, state.height);
    gl.clearColor(0.032, 0.032, 0.032, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    for (const method of METHODS) drawAoPass(method.id, camera, viewports[method.id], false);
  } else {
    drawAoPass(state.method, camera, { x: 0, y: 0, w: state.width, h: state.height });
  }
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  if (state.frameCounter++ % 12 === 0) {
    const now = performance.now();
    const fps = 1000 / Math.max(0.1, now - state.lastFrameTime);
    state.lastFrameTime = now;
    if (!state.benchmark) ui.renderStatus.textContent = state.buildInProgress ? "Updating clipmaps" : `${fps.toFixed(0)} FPS · AO only`;
    if (now - state.lastUiUpdate > 260) {
      state.lastUiUpdate = now;
      updateCosts();
    }
  }
}
function loop() {
  try { drawFrame(); }
  catch (error) {
    console.error(error);
    state.failed = true;
    setMessage(`Render error: ${error.message}`, true);
  }
  requestAnimationFrame(loop);
}

function setClipmapFilter() {
  const gl = state.gl;
  if (!gl) return;
  const filter = state.linearUdf ? gl.LINEAR : gl.NEAREST;
  for (const texture of state.udfTextures) {
    gl.bindTexture(gl.TEXTURE_3D, texture);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MIN_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MAG_FILTER, filter);
  }
}
function setCompare(enabled) {
  state.compare = enabled;
  ui.compareButton.setAttribute("aria-pressed", String(enabled));
  ui.compareButton.classList.toggle("button-primary", enabled);
  ui.compareLegend.hidden = !enabled;
  ui.benchmarkStatus.textContent = enabled
    ? "Five same-view AO panels. Use Benchmark for full-canvas per-method timings."
    : "Select a method or run the benchmark to collect timings.";
}
function startBenchmark() {
  if (!state.ready || state.buildInProgress || state.benchmark) return;
  state.compare = false;
  setCompare(false);
  for (const item of state.pendingQueries) state.gl.deleteQuery(item.query);
  state.pendingQueries = [];
  state.cost = METHODS.map(() => ({ gpuMs: null, submitMs: null, count: 0 }));
  updateCosts();
  state.benchmark = { methods: METHODS.map((method) => method.id), index: 0, framesPerMethod: 24, remaining: 24 };
  ui.benchmarkButton.disabled = true;
  ui.benchmarkButton.textContent = "Benchmarking…";
  ui.benchmarkStatus.textContent = `Warming and measuring ${METHODS[0].label} · method 1/${METHODS.length}`;
}

function installControls() {
  const updateRadius = () => { state.radius = Number(ui.radius.value); ui.radiusValue.textContent = `${state.radius.toFixed(2)} m`; };
  const updateSamples = () => { state.samples = Number(ui.samples.value); ui.samplesValue.textContent = String(state.samples); };
  const updateBias = () => { state.bias = Number(ui.bias.value); ui.biasValue.textContent = `${state.bias.toFixed(3)} m`; };
  const updateStrength = () => { state.strength = Number(ui.strength.value); ui.strengthValue.textContent = `${state.strength.toFixed(2)}×`; };
  updateRadius(); updateSamples(); updateBias(); updateStrength();
  ui.radius.addEventListener("input", updateRadius);
  ui.samples.addEventListener("input", updateSamples);
  ui.bias.addEventListener("input", updateBias);
  ui.strength.addEventListener("input", updateStrength);
  ui.voxel.addEventListener("input", () => {
    state.voxelSize = Number(ui.voxel.value);
    ui.voxelValue.textContent = `${state.voxelSize.toFixed(2)} m`;
    scheduleRebuild();
  });
  ui.levels.addEventListener("input", () => {
    state.levels = Number(ui.levels.value);
    ui.levelsValue.textContent = String(state.levels);
    scheduleRebuild();
  });
  ui.filter.addEventListener("change", () => { state.linearUdf = ui.filter.checked; setClipmapFilter(); });
  ui.compareButton.addEventListener("click", () => setCompare(!state.compare));
  const updatePanMode = () => {
    state.panMode = !state.panMode;
    for (const button of [ui.panModeButton, ui.panModeHudButton]) {
      button.setAttribute("aria-pressed", String(state.panMode));
      button.classList.toggle("button-primary", state.panMode);
    }
    ui.canvas.classList.toggle("pan-mode", state.panMode);
  };
  const frameVehicle = () => {
    state.camera.yaw = -0.94;
    state.camera.pitch = 0.25;
    fitCameraToVehicle();
  };
  ui.panModeButton.addEventListener("click", updatePanMode);
  ui.panModeHudButton.addEventListener("click", updatePanMode);
  ui.resetView.addEventListener("click", frameVehicle);
  ui.frameVehicleHudButton.addEventListener("click", frameVehicle);
  ui.rebuildButton.addEventListener("click", () => rebuildClipmaps());
  ui.benchmarkButton.addEventListener("click", startBenchmark);
  ui.canvas.addEventListener("contextmenu", (event) => event.preventDefault());
  ui.canvas.addEventListener("auxclick", (event) => event.preventDefault());
  const beginCameraDrag = (id, x, y, mode) => {
    state.pointer = { id, x, y, mode };
    ui.canvas.classList.toggle("is-panning", mode === "pan");
  };
  const moveCameraDrag = (id, x, y) => {
    if (!state.pointer || state.pointer.id !== id) return;
    const dx = x - state.pointer.x, dy = y - state.pointer.y;
    state.pointer.x = x; state.pointer.y = y;
    if (state.pointer.mode === "pan") {
      panCamera(dx, dy);
    } else {
      // Match the expected orbit-drag direction: pull left to orbit left, pull up to look up.
      state.camera.yaw -= dx * 0.008;
      state.camera.pitch = clamp(state.camera.pitch + dy * 0.006, -0.12, 1.15);
    }
    state.camera.userMoved = true;
  };
  const endCameraDrag = (id = null) => {
    if (id !== null && state.pointer?.id !== id) return;
    state.pointer = null;
    ui.canvas.classList.remove("is-panning");
  };
  // Mouse fallback deliberately uses mouse events: middle/right-button drags do not
  // reliably expose pointer capture on every embedded preview/browser combination.
  ui.canvas.addEventListener("mousedown", (event) => {
    if (event.button !== 0 && event.button !== 1 && event.button !== 2) return;
    const pan = state.panMode || event.button === 1 || event.button === 2 || event.shiftKey || event.altKey || event.ctrlKey;
    beginCameraDrag("mouse", event.clientX, event.clientY, pan ? "pan" : "orbit");
    event.preventDefault();
  });
  window.addEventListener("mousemove", (event) => moveCameraDrag("mouse", event.clientX, event.clientY));
  window.addEventListener("mouseup", () => endCameraDrag("mouse"));
  ui.canvas.addEventListener("pointerdown", (event) => {
    if (event.pointerType === "mouse") return;
    const pan = state.panMode || event.shiftKey || event.altKey || event.ctrlKey;
    beginCameraDrag(event.pointerId, event.clientX, event.clientY, pan ? "pan" : "orbit");
    ui.canvas.setPointerCapture(event.pointerId);
    event.preventDefault();
  });
  ui.canvas.addEventListener("pointermove", (event) => {
    if (event.pointerType === "mouse") return;
    moveCameraDrag(event.pointerId, event.clientX, event.clientY);
  });
  ui.canvas.addEventListener("pointerup", (event) => endCameraDrag(event.pointerId));
  ui.canvas.addEventListener("pointercancel", (event) => endCameraDrag(event.pointerId));
  ui.canvas.addEventListener("lostpointercapture", (event) => endCameraDrag(event.pointerId));
  window.addEventListener("blur", () => endCameraDrag());
  ui.canvas.addEventListener("wheel", (event) => {
    event.preventDefault();
    state.camera.distance = clamp(state.camera.distance * Math.exp(event.deltaY * 0.001), 2.0, 50.0);
    state.camera.userMoved = true;
  }, { passive: false });
  const observer = new ResizeObserver(() => resizeCanvas());
  observer.observe(ui.frame);
}
let rebuildTimer = null;
function scheduleRebuild() {
  clearTimeout(rebuildTimer);
  if (!state.ready) return;
  rebuildTimer = setTimeout(() => {
    if (state.buildInProgress) {
      ++state.buildId;
      state.buildInProgress = false;
    }
    rebuildClipmaps();
  }, 240);
}

async function init() {
  createTabs();
  setMethod(0);
  installControls();
  try {
    const gl = ui.canvas.getContext("webgl2", { alpha: false, antialias: false, depth: true, powerPreference: "high-performance" });
    if (!gl) throw new Error("WebGL 2 is unavailable in this browser.");
    state.gl = gl;
    state.floatColorExt = gl.getExtension("EXT_color_buffer_float");
    if (!state.floatColorExt) throw new Error("This browser does not expose EXT_color_buffer_float, required by the world-position AO buffer.");
    state.timerExt = gl.getExtension("EXT_disjoint_timer_query_webgl2");
    state.gbufferProgram = program(gl, GBUFFER_VS, GBUFFER_FS);
    state.program = program(gl, FULLSCREEN_VS, AO_FS);
    state.gbufferLoc = locs(gl, state.gbufferProgram, ["uViewProj"]);
    state.aoLoc = locs(gl, state.program, [
      "uPositionTex", "uNormalTex", "uUdf0", "uUdf1", "uUdf2", "uNodeTex", "uTriTex", "uInstanceTex", "uUdfCenter",
      "uResolution", "uCameraPosition", "uViewProj", "uProjectionY", "uRadius", "uBias", "uStrength", "uVoxelSize",
      "uSamples", "uSteps", "uMethod", "uClipmapLevels", "uNodeWidth", "uTriWidth", "uInstanceCount", "uGroundZ",
    ]);
    state.aoLoc.uInstanceRoot = gl.getUniformLocation(state.program, "uInstanceRoot[0]");
    state.aoLoc.uUdfHalfExtent = gl.getUniformLocation(state.program, "uUdfHalfExtent[0]");
    state.aoLoc.uUdfRange = gl.getUniformLocation(state.program, "uUdfRange[0]");
    gl.useProgram(state.program);
    set1i(gl, state.aoLoc.uPositionTex, 0); set1i(gl, state.aoLoc.uNormalTex, 1);
    set1i(gl, state.aoLoc.uUdf0, 2); set1i(gl, state.aoLoc.uUdf1, 3); set1i(gl, state.aoLoc.uUdf2, 4);
    set1i(gl, state.aoLoc.uNodeTex, 5); set1i(gl, state.aoLoc.uTriTex, 6); set1i(gl, state.aoLoc.uInstanceTex, 7);
    state.whiteUdf = makeTexture3D(gl, 1, 1, 1, new Uint8Array([255]), gl.NEAREST);
    gl.enable(gl.DEPTH_TEST);
    gl.disable(gl.CULL_FACE);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    ui.device.textContent = state.timerExt ? "WebGL 2 · GPU timer queries" : "WebGL 2 · CPU-submit timing fallback";
    ui.renderStatus.textContent = "Loading ControlVehicle source";

    const response = await fetch("/Experimental/ShadowAlgorithmLab/Assets/VehicleBenchmarkAssets.bin");
    if (!response.ok) throw new Error(`Vehicle asset request failed (${response.status}).`);
    const arrayBuffer = await response.arrayBuffer();
    const assets = ParseVehicleFvb1Buffer(arrayBuffer);
    const vehicle = { PickCode: 501, Position: [0, 0, 0], Rotation: [0, 0, 0], Scale: [1, 1, 1], CastShadows: true, BaseColor: [0.9, 0.9, 0.9], Roughness: 0.4, Metallic: 0.0, Coat: 0.0 };
    const instances = ExpandVehicleInstances([vehicle], assets);
    if (!instances.length || assets.totalTriangles < 1000) throw new Error("ControlVehicle mesh data was empty or incomplete.");
    state.assets = assets;
    state.instances = instances;
    state.vehicleBounds = computeVehicleBounds(assets, instances);
    fitCameraToVehicle();
    for (let i = 0; i < instances.length; ++i) state.instanceRoots[i] = TLAS_RESERVED_NODES + assets.meshMeta[instances[i].meshIdx].nodeOffset;
    const vertexData = buildVehicleGeometry(assets, instances);
    state.meshVertexCount = vertexData.length / 6;
    state.meshVao = gl.createVertexArray();
    state.meshVbo = gl.createBuffer();
    gl.bindVertexArray(state.meshVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, state.meshVbo);
    gl.bufferData(gl.ARRAY_BUFFER, vertexData, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 24, 0);
    gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 24, 12);
    gl.bindVertexArray(null);
    const gpuTextures = uploadVehicleTextures(gl, assets, instances);
    state.vehicleNodeTexture = gpuTextures.node;
    state.vehicleTriTexture = gpuTextures.tri;
    state.vehicleInstanceTexture = gpuTextures.instance;
    state.ready = true;
    const instanceTriangles = instances.reduce((sum, inst) => sum + assets.meshMeta[inst.meshIdx].triCount, 0);
    ui.device.textContent = `${state.timerExt ? "GPU timing" : "CPU timing"} · ${instanceTriangles.toLocaleString()} instantiated triangles`;
    ui.renderStatus.textContent = "Building AO clipmaps";
    ui.rebuildButton.disabled = false;
    requestAnimationFrame(loop);
    await rebuildClipmaps();
    ui.benchmarkButton.disabled = false;
  } catch (error) {
    console.error(error);
    state.failed = true;
    ui.renderStatus.textContent = "Demo initialization failed";
    ui.device.textContent = "WebGL 2 · unavailable";
    setMessage(error.message, true);
    ui.progressText.textContent = "Initialization failed";
  }
}

init();
