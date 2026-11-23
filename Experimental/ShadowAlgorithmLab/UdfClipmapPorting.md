# Unsigned distance clipmaps for AO (C++ port notes)

## Why the previous field produced black blobs

The old voxel builder assigned a sign from the side of the nearest triangle. That is only a local half-space test; it is not a reliable inside/outside test for an open, thin, mirrored, or inconsistently wound vehicle mesh. False negative distances were then treated by the ray marcher as immediate intersections. The AO clipmaps now store **unsigned distance to the nearest surface** instead. AO needs distance to a blocker, not solid containment, so this avoids inventing inside regions. The floor is an analytic plane, not part of the voxel field.

## Build one clipmap level

Use voxel-center samples, world-space distances, a truncated range (the workbench uses `range = 8 * cellSize`), and the mesh BVH for nearest-triangle queries. Union the mesh surfaces with `min`; do not assign a triangle-side sign.

```cpp
float BuildUdfVoxel(Vec3 worldP, float range, Span<const Instance> instances)
{
    float bestWorldDistance = range;
    for (const Instance& inst : instances)
    {
        if (DistanceToAabb(worldP, inst.worldBounds) >= bestWorldDistance)
            continue;

        const float minScale = MinimumBasisScale(inst.localToWorld);
        const Vec3 localP = TransformPoint(inst.worldToLocal, worldP);
        const float localLimit = bestWorldDistance / minScale;
        const float localDistance = QueryNearestTriangleDistance(
            inst.localBvh, localP, localLimit);

        // Exact for rigid/uniform-scale transforms; a conservative lower bound
        // for orthogonal non-uniform scales. See the transform note below.
        bestWorldDistance = std::min(bestWorldDistance, localDistance * minScale);
    }
    return std::clamp(bestWorldDistance, 0.0f, range);
}

uint8_t EncodeUdf(float distance, float range)
{
    const float normalized = std::clamp(distance / range, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::floor(normalized * 255.0f));
}
```

The floor plane is combined at sample time with `min(udf, abs(worldPosition.z - groundZ))`. Decode an R8/UNORM sample as `distance = sample.r * range`—**not** `sample.r * 2 - 1`.

### Transform note

Distances must be in world units. Multiplying local distance by the minimum basis scale is exact for rigid or uniform-scale transforms and conservative for orthogonal non-uniform scales. If transforms can contain shear, bake the instance into a world-space BVH or compute world-space nearest-triangle distance; do not use an unqualified local-space distance as a world-space SDF.

## Sample the nested clipmaps

Start with the coarsest level that contains the point; blend each finer field into it near that finer level's border. For normalized clip coordinates `uv`, a simple blend weight is:

```cpp
float edge = min(min(uv.x, 1-uv.x), min(min(uv.y, 1-uv.y), min(uv.z, 1-uv.z)));
float fineWeight = SmoothStep(0.0f, 0.10f, edge);
distance = Lerp(coarseDistance, fineDistance, fineWeight);
cellSize = Lerp(coarseCellSize, fineCellSize, fineWeight);
```

The clipmap texture stores non-negative values in `[0, range]`; use the level's cell size for hit tolerance and stepping. Keep cascade centers aligned to voxel increments when scrolling production clipmaps.

## AO ray march

Bias the origin **along the surface normal**, then march from `t = 0`. Do not use a fixed minimum step larger than the sampled distance; that can jump over a thin blocker.

```cpp
Vec3 rayOrigin = worldPosition + normal * max(surfaceBias, fineCellSize * 0.65f);
float t = 0.0f;
float previousDistance = 0.0f;
float previousStep = 0.0f;
bool hasPrevious = false;
for (int step = 0; step < maxSteps && t < radius; ++step)
{
    float cellSize;
    const Vec3 sampleP = rayOrigin + rayDirection * t;
    float d = SampleBlendedUdf(sampleP, cellSize);
    d = min(d, abs(sampleP.z - groundZ));

    if (d < max(1e-4f, cellSize * 0.18f)) {
        occluded = true;
        break;
    }

    // Only soften AO when the ray is approaching a surface. This prevents a
    // ray skimming parallel to the floor from treating that floor as a blocker.
    const float approach = hasPrevious
        ? Saturate((previousDistance - d) / max(previousStep, cellSize * 0.1f))
        : 0.0f;
    const float nearRatio = Saturate(d / max(cellSize * 0.45f, t * 1.15f));
    const float falloff = Saturate(1.0f - t / radius);
    rayOcclusion = max(rayOcclusion, (1.0f - nearRatio) * approach * falloff * 0.82f);

    const float stepLength = min(d * 0.75f, min(0.48f, radius - t));
    if (stepLength <= 1e-5f) break;
    previousDistance = d;
    previousStep = stepLength;
    hasPrevious = true;
    t += stepLength;
}
```

Use the exact same world-space radius, bias, clip center, voxel sizes, and sample directions when comparing with BVH rays. This UDF is intended for AO ray queries; if another system needs inside/outside classification, compute that separately with a robust parity/winding test on watertight geometry.
