/*============================================================================================================================================
                                                GLOBALDISTANCEINCREMENTALPROBE.CPP
============================================================================================================================================*/
// 🧩 Deterministic probe: dirty-region global distance field rebake matches a full rebake inside the moved
//    region, retains valid data outside it, and touches a small fraction of the grid.

#include "Engine/Mathematics/AxisCoordinates.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
using Frontier::Mathematics::Axis3D;

constexpr uint32_t GridRes    = 48u;
constexpr uint32_t VoxelCount = GridRes * GridRes * GridRes;
constexpr float    HalfSpan   = 6.0f;
constexpr float    VoxelSize  = (2.0f * HalfSpan) / static_cast<float>(GridRes);
constexpr float    MaxBakeDist = VoxelSize * 5.5f;

struct SphereOccluder
{
    Axis3D Centre { -2.5f, 0.0f, 0.0f };
    float  Radius = 1.0f;
};

float EvaluateSceneDistance(const SphereOccluder& Occluder, const Axis3D& P)
{
    const Axis3D D = P - Occluder.Centre;
    const float Raw = std::sqrt(Frontier::Mathematics::EvaluateDot(D, D)) - Occluder.Radius;
    return std::clamp(Raw, -MaxBakeDist, MaxBakeDist);
}

uint32_t VoxelIndex(uint32_t X, uint32_t Y, uint32_t Z)
{
    return Z * GridRes * GridRes + Y * GridRes + X;
}

Axis3D VoxelPosition(uint32_t X, uint32_t Y, uint32_t Z)
{
    const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(GridRes);
    const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(GridRes);
    const float W = (static_cast<float>(Z) + 0.5f) / static_cast<float>(GridRes);
    return { -HalfSpan + U * 2.0f * HalfSpan, -HalfSpan + V * 2.0f * HalfSpan, -HalfSpan + W * 2.0f * HalfSpan };
}

// Mirrors GlobalDistanceBakeEntry: full march, or march only inside the dirty box (+ 5.5 voxel margin).
void BakeField(
    std::vector<float>& Field,
    const SphereOccluder& Occluder,
    bool DirtyRestricted,
    const Axis3D& DirtyMin,
    const Axis3D& DirtyMax,
    uint64_t& OutBakedVoxels)
{
    OutBakedVoxels = 0u;
    const float Margin = MaxBakeDist;
    for (uint32_t Z = 0u; Z < GridRes; ++Z)
    {
        for (uint32_t Y = 0u; Y < GridRes; ++Y)
        {
            for (uint32_t X = 0u; X < GridRes; ++X)
            {
                const Axis3D P = VoxelPosition(X, Y, Z);
                if (DirtyRestricted)
                {
                    const bool Outside =
                        P.X < DirtyMin.X - Margin || P.X > DirtyMax.X + Margin ||
                        P.Y < DirtyMin.Y - Margin || P.Y > DirtyMax.Y + Margin ||
                        P.Z < DirtyMin.Z - Margin || P.Z > DirtyMax.Z + Margin;
                    if (Outside)
                    {
                        continue; // Retain the previous value, exactly like the compute early-out
                    }
                }
                Field[VoxelIndex(X, Y, Z)] = EvaluateSceneDistance(Occluder, P);
                ++OutBakedVoxels;
            }
        }
    }
}

float SampleField(const std::vector<float>& Field, const Axis3D& P)
{
    const Axis3D Normalized { (P.X + HalfSpan) / (2.0f * HalfSpan), (P.Y + HalfSpan) / (2.0f * HalfSpan), (P.Z + HalfSpan) / (2.0f * HalfSpan) };
    const int X0 = std::clamp(static_cast<int>(std::floor(Normalized.X * GridRes)), 0, static_cast<int>(GridRes) - 1);
    const int Y0 = std::clamp(static_cast<int>(std::floor(Normalized.Y * GridRes)), 0, static_cast<int>(GridRes) - 1);
    const int Z0 = std::clamp(static_cast<int>(std::floor(Normalized.Z * GridRes)), 0, static_cast<int>(GridRes) - 1);
    return Field[VoxelIndex(static_cast<uint32_t>(X0), static_cast<uint32_t>(Y0), static_cast<uint32_t>(Z0))];
}

bool Report(bool Condition, const char* Label)
{
    std::printf("%s : %s\n", Condition ? "PASS" : "FAIL", Label);
    return Condition;
}

} // namespace

int main()
{
    bool Pass = true;

    SphereOccluder Occluder {};
    std::vector<float> Field(VoxelCount, MaxBakeDist);
    uint64_t Baked = 0u;
    BakeField(Field, Occluder, false, {}, {}, Baked);
    Pass = Report(Baked == VoxelCount, "first build marches every voxel") && Pass;

    // Move the occluder and dirty only the swept box (union of old and new footprint).
    const Axis3D PriorCentre = Occluder.Centre;
    Occluder.Centre = { 1.5f, 0.0f, 0.0f };
    const float  Reach = 1.75f;
    const Axis3D DirtyMin { std::min(PriorCentre.X, Occluder.Centre.X) - Reach,
                            std::min(PriorCentre.Y, Occluder.Centre.Y) - Reach,
                            std::min(PriorCentre.Z, Occluder.Centre.Z) - Reach };
    const Axis3D DirtyMax { std::max(PriorCentre.X, Occluder.Centre.X) + Reach,
                            std::max(PriorCentre.Y, Occluder.Centre.Y) + Reach,
                            std::max(PriorCentre.Z, Occluder.Centre.Z) + Reach };

    std::vector<float> Incremental = Field;
    BakeField(Incremental, Occluder, true, DirtyMin, DirtyMax, Baked);
    const float DirtyFraction = static_cast<float>(Baked) / static_cast<float>(VoxelCount);

    std::vector<float> FullRebake(VoxelCount, MaxBakeDist);
    uint64_t FullBaked = 0u;
    BakeField(FullRebake, Occluder, false, DirtyMin, DirtyMax, FullBaked);

    // Inside the moved footprint the incremental field must equal the full rebake.
    float WorstInsideError = 0.0f;
    for (float X = -0.9f; X <= 2.6f; X += 0.25f)
    {
        for (float Y = -0.9f; Y <= 0.9f; Y += 0.25f)
        {
            WorstInsideError = std::max(WorstInsideError, std::fabs(SampleField(Incremental, { X, Y, 0.0f }) - SampleField(FullRebake, { X, Y, 0.0f })));
        }
    }
    // Far away from both footprints the retained voxels must still match the full rebake (still valid).
    float WorstOutsideError = 0.0f;
    for (float X = 3.6f; X <= 5.2f; X += 0.2f)
    {
        for (float Y = 3.6f; Y <= 5.2f; Y += 0.2f)
        {
            WorstOutsideError = std::max(WorstOutsideError, std::fabs(SampleField(Incremental, { X, Y, 0.0f }) - SampleField(FullRebake, { X, Y, 0.0f })));
        }
    }

    Pass = Report(WorstInsideError <= 1.0e-4f, "moved-object region matches a full rebake exactly") && Pass;
    Pass = Report(WorstOutsideError <= 1.0e-4f, "untouched region stays valid (identical to a full rebake)") && Pass;
    Pass = Report(DirtyFraction < 0.35f, "a 2.5 m object move stamps under 35% of the grid") && Pass;

    // Realistic per-frame drag step: a slow drag advances the object a few centimetres per frame.
    const Axis3D StepStart = Occluder.Centre;
    Occluder.Centre = StepStart + Axis3D { 0.15f, 0.0f, 0.0f };
    const Axis3D StepMin { StepStart.X - Reach, StepStart.Y - Reach, StepStart.Z - Reach };
    const Axis3D StepMax { Occluder.Centre.X + Reach, Occluder.Centre.Y + Reach, Occluder.Centre.Z + Reach };
    uint64_t StepBaked = 0u;
    BakeField(Incremental, Occluder, true, StepMin, StepMax, StepBaked);
    const float StepFraction = static_cast<float>(StepBaked) / static_cast<float>(VoxelCount);
    Pass = Report(StepFraction < 0.20f, "a 0.15 m per-frame drag step stamps under 20% of the grid") && Pass;

    std::printf("dirty voxels   : %llu / %u (%.1f%%) for a 2.5 m move\n",
                static_cast<unsigned long long>(Baked), VoxelCount, static_cast<double>(DirtyFraction * 100.0f));
    std::printf("dirty voxels   : %llu / %u (%.1f%%) for a 0.15 m move\n",
                static_cast<unsigned long long>(StepBaked), VoxelCount, static_cast<double>(StepFraction * 100.0f));
    std::printf("%s\n", Pass ? "RESULT: PASS" : "RESULT: FAIL");
    return Pass ? 0 : 1;
}
