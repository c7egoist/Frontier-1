/*============================================================================================================================================
                                                         DEFORMINGCAPSULERIG.CPP
============================================================================================================================================*/
// 🧩 Implementation of analytic 32-byte tapered bone capsules, sub-0.1 ms humanoid pose update, ray occlusion, & soft solar cones

#include "Engine/SpatialAcceleration/DeformingCapsuleRig.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::SpatialAcceleration
{

namespace
{

[[nodiscard]] float SmoothHermiteStep(float Edge0, float Edge1, float InputX) noexcept
{
    const float Normalized = std::clamp((InputX - Edge0) / std::max(1.0e-6f, Edge1 - Edge0), 0.0f, 1.0f);
    return Normalized * Normalized * (3.0f - 2.0f * Normalized);
}

[[nodiscard]] float SmoothQuadraticMinimum(float LeftA, float RightB, float BlendK) noexcept
{
    const float Overlap = std::max(BlendK - std::fabs(LeftA - RightB), 0.0f) / BlendK;
    return std::min(LeftA, RightB) - Overlap * Overlap * BlendK * 0.25f;
}

void BuildOrthonormalFrame(
    const Mathematics::Axis3D& UnitNormal,
    Mathematics::Axis3D&       OutTangent,
    Mathematics::Axis3D&       OutBitangent) noexcept
{
    const float SignZ  = (UnitNormal.Z >= 0.0f) ? 1.0f : -1.0f;
    const float ScaleA = -1.0f / (SignZ + UnitNormal.Z);
    const float CrossB = UnitNormal.X * UnitNormal.Y * ScaleA;
    OutTangent         = { 1.0f + SignZ * UnitNormal.X * UnitNormal.X * ScaleA, SignZ * CrossB, -SignZ * UnitNormal.X };
    OutBitangent       = { CrossB, SignZ + UnitNormal.Y * UnitNormal.Y * ScaleA, -UnitNormal.Y };
}

void AppendSingleTaperedLimb(
    GeometrySpace::PolygonNetwork& OutNetwork,
    const TaperedBoneCapsule&      Capsule,
    uint16_t                       MaterialSlot,
    uint32_t                       RadialSides)
{
    const Mathematics::Axis3D AxisVec = Capsule.JointEnd - Capsule.JointStart;
    const float AxisLen = Mathematics::EvaluateLength(AxisVec);
    if (AxisLen <= 1.0e-5f || RadialSides < 3u)
    {
        return;
    }

    const Mathematics::Axis3D UnitAxis = AxisVec * (1.0f / AxisLen);
    Mathematics::Axis3D TangentU {}, BitangentV {};
    BuildOrthonormalFrame(UnitAxis, TangentU, BitangentV);

    constexpr uint32_t RingCount = 4u;
    std::vector<uint32_t> RingVertices(static_cast<std::size_t>(RingCount) * RadialSides, 0u);
    for (uint32_t RingIdx = 0u; RingIdx < RingCount; ++RingIdx)
    {
        const float Frac   = static_cast<float>(RingIdx) / static_cast<float>(RingCount - 1u);
        const auto  Center = Capsule.JointStart + AxisVec * Frac;
        const float Radius = Capsule.RadiusStart + (Capsule.RadiusEnd - Capsule.RadiusStart) * Frac;
        for (uint32_t SideIdx = 0u; SideIdx < RadialSides; ++SideIdx)
        {
            const float Angle = Constants::TauFloat * static_cast<float>(SideIdx) / static_cast<float>(RadialSides);
            const auto  Point = Center + (TangentU * std::cos(Angle) + BitangentV * std::sin(Angle)) * Radius;
            RingVertices[static_cast<std::size_t>(RingIdx) * RadialSides + SideIdx] = OutNetwork.AppendVertex(Point);
        }
    }

    const uint32_t PoleStart = OutNetwork.AppendVertex(Capsule.JointStart - UnitAxis * Capsule.RadiusStart);
    const uint32_t PoleEnd   = OutNetwork.AppendVertex(Capsule.JointEnd   + UnitAxis * Capsule.RadiusEnd);

    for (uint32_t SideIdx = 0u; SideIdx < RadialSides; ++SideIdx)
    {
        const uint32_t NextSide = (SideIdx + 1u) % RadialSides;
        const std::array<uint32_t, 3u> CapStartTri { PoleStart, RingVertices[NextSide], RingVertices[SideIdx] };
        (void)OutNetwork.AppendFace(CapStartTri, {}, {}, 0u, MaterialSlot);

        const uint32_t TopOffset = (RingCount - 1u) * RadialSides;
        const std::array<uint32_t, 3u> CapEndTri { PoleEnd, RingVertices[TopOffset + SideIdx], RingVertices[TopOffset + NextSide] };
        (void)OutNetwork.AppendFace(CapEndTri, {}, {}, 0u, MaterialSlot);
    }

    for (uint32_t RingIdx = 0u; RingIdx + 1u < RingCount; ++RingIdx)
    {
        for (uint32_t SideIdx = 0u; SideIdx < RadialSides; ++SideIdx)
        {
            const uint32_t NextSide = (SideIdx + 1u) % RadialSides;
            const uint32_t V00 = RingVertices[static_cast<std::size_t>(RingIdx) * RadialSides + SideIdx];
            const uint32_t V01 = RingVertices[static_cast<std::size_t>(RingIdx) * RadialSides + NextSide];
            const uint32_t V10 = RingVertices[static_cast<std::size_t>(RingIdx + 1u) * RadialSides + SideIdx];
            const uint32_t V11 = RingVertices[static_cast<std::size_t>(RingIdx + 1u) * RadialSides + NextSide];
            const std::array<uint32_t, 3u> Tri0 { V00, V01, V11 };
            const std::array<uint32_t, 3u> Tri1 { V00, V11, V10 };
            (void)OutNetwork.AppendFace(Tri0, {}, {}, 0u, MaterialSlot);
            (void)OutNetwork.AppendFace(Tri1, {}, {}, 0u, MaterialSlot);
        }
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              TAPERED BONE CAPSULE METHODS
//------------------------------------------------------------------------------------------------------------------------

float TaperedBoneCapsule::EvaluateSignedDistance(const Mathematics::Axis3D& WorldPoint) const noexcept
{
    const Mathematics::Axis3D Segment   = JointEnd - JointStart;
    const Mathematics::Axis3D Relative  = WorldPoint - JointStart;
    const float               LengthSq  = Mathematics::EvaluateDot(Segment, Segment);
    const float               Fraction  = (LengthSq > 1.0e-8f)
        ? std::clamp(Mathematics::EvaluateDot(Relative, Segment) / LengthSq, 0.0f, 1.0f)
        : 0.0f;
    const float RadiusAtFraction = RadiusStart + (RadiusEnd - RadiusStart) * Fraction;
    return Mathematics::EvaluateLength(Relative - Segment * Fraction) - RadiusAtFraction;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              DEFORMING CAPSULE RIG METHODS
//------------------------------------------------------------------------------------------------------------------------

float DeformingCapsuleRig::EvaluateMinimumSignedDistance(const Mathematics::Axis3D& WorldPoint) const noexcept
{
    if (Capsules.empty())
    {
        return 1.0e5f;
    }

    float BestDistance = 1.0e5f;
    for (const auto& Capsule : Capsules)
    {
        BestDistance = std::min(BestDistance, Capsule.EvaluateSignedDistance(WorldPoint));
    }
    return BestDistance;
}

bool DeformingCapsuleRig::QueryRayOccluded(
    const Mathematics::Axis3D& Origin,
    const Mathematics::Axis3D& UnitDirection,
    float                      MinimumDistance,
    float                      MaximumDistance) const noexcept
{
    if (Capsules.empty() || MaximumDistance <= MinimumDistance)
    {
        return false;
    }

    float Travel = std::max(MinimumDistance, 1.0e-3f);
    for (uint32_t StepIdx = 0u; StepIdx < 48u && Travel <= MaximumDistance; ++StepIdx)
    {
        const float SignedDist = EvaluateMinimumSignedDistance(Origin + UnitDirection * Travel);
        if (SignedDist <= 5.0e-4f)
        {
            return true;
        }
        Travel += std::max(SignedDist * 0.85f, 0.012f);
    }
    return false;
}

float DeformingCapsuleRig::EvaluateSoftConeVisibility(
    const Mathematics::Axis3D& ReceiverPoint,
    const Mathematics::Axis3D& ReceiverNormal,
    const Mathematics::Axis3D& UnitDirectionToLight,
    float                      TangentConeHalfAngle,
    float                      MaxDistanceMeters) const noexcept
{
    if (Capsules.empty())
    {
        return 1.0f;
    }

    float MinimumRatio = 2.0f;
    float Travel       = 0.045f;
    const float ClampedTan = std::max(0.004f, TangentConeHalfAngle);

    for (uint32_t StepIdx = 0u; StepIdx < 40u && Travel < MaxDistanceMeters; ++StepIdx)
    {
        const Mathematics::Axis3D SamplePos = ReceiverPoint + UnitDirectionToLight * Travel;
        const float PlaneFade = SmoothHermiteStep(0.015f, 0.10f, Mathematics::EvaluateDot(SamplePos - ReceiverPoint, ReceiverNormal));
        const float CapsuleD  = EvaluateMinimumSignedDistance(SamplePos);
        if (PlaneFade > 1.0e-4f)
        {
            const float ConeRadius = std::max(0.016f, Travel * ClampedTan);
            const float Ratio      = std::clamp(CapsuleD / ConeRadius, -1.5f, 1.5f) * PlaneFade + 1.5f * (1.0f - PlaneFade);
            MinimumRatio           = SmoothQuadraticMinimum(MinimumRatio, Ratio, 0.12f);
            if (MinimumRatio <= -0.96f)
            {
                return 0.0f;
            }
        }
        Travel += std::clamp(std::fabs(CapsuleD) * 0.55f, 0.025f, 0.24f);
    }

    return SmoothHermiteStep(-0.95f, 0.95f, MinimumRatio);
}

void DeformingCapsuleRig::UpdateHumanoidPose(
    float                      PosePhase,
    const Mathematics::Axis3D& RootWorld) noexcept
{
    const float Phase = std::clamp(PosePhase, 0.0f, 1.0f);
    RootCenterWorld   = RootWorld;
    BoundingRadiusM   = 2.15f;
    Capsules.resize(10u);

    const float LeanX     = 0.06f + 0.16f * Phase;
    const float StrideL   = -0.22f + 0.56f * Phase;
    const float StrideR   =  0.22f - 0.52f * Phase;
    const float ArmWaveZ  = RootWorld.Z + 1.62f + 0.34f * (1.0f - Phase);
    const float ArmReachX = 0.14f + 0.44f * Phase;
    const float CrouchZ   = -0.04f * Phase;

    const Mathematics::Axis3D Pelvis { RootWorld.X + LeanX * 0.2f, RootWorld.Y, RootWorld.Z + 0.92f + CrouchZ };
    const Mathematics::Axis3D Chest  { RootWorld.X + LeanX * 0.7f, RootWorld.Y, RootWorld.Z + 1.38f + CrouchZ };
    const Mathematics::Axis3D Head   { RootWorld.X + LeanX * 1.1f, RootWorld.Y, RootWorld.Z + 1.68f + CrouchZ };

    const Mathematics::Axis3D LeftShoulder  { Chest.X, Chest.Y - 0.24f, Chest.Z - 0.03f };
    const Mathematics::Axis3D LeftElbow     { LeftShoulder.X + ArmReachX * 0.6f, LeftShoulder.Y - 0.26f, RootWorld.Z + 1.38f + (ArmWaveZ - (RootWorld.Z + 1.62f)) * 0.5f };
    const Mathematics::Axis3D LeftHand      { LeftElbow.X + ArmReachX * 0.7f,    LeftElbow.Y - 0.16f,    ArmWaveZ };

    const Mathematics::Axis3D RightShoulder { Chest.X, Chest.Y + 0.24f, Chest.Z - 0.03f };
    const Mathematics::Axis3D RightElbow    { RightShoulder.X - 0.16f * Phase, RightShoulder.Y + 0.16f, RootWorld.Z + 1.18f - 0.10f * Phase };
    const Mathematics::Axis3D RightHand     { RightElbow.X + 0.12f * (1.0f - Phase), RightElbow.Y + 0.08f, RootWorld.Z + 0.96f + 0.14f * Phase };

    const Mathematics::Axis3D LeftHip   { Pelvis.X, Pelvis.Y - 0.12f, Pelvis.Z - 0.04f };
    const Mathematics::Axis3D LeftKnee  { LeftHip.X + StrideL * 0.55f, LeftHip.Y - 0.02f, RootWorld.Z + 0.48f + 0.08f * Phase };
    const Mathematics::Axis3D LeftFoot  { RootWorld.X + StrideL,       LeftHip.Y - 0.02f, RootWorld.Z + 0.06f };

    const Mathematics::Axis3D RightHip  { Pelvis.X, Pelvis.Y + 0.12f, Pelvis.Z - 0.04f };
    const Mathematics::Axis3D RightKnee { RightHip.X + StrideR * 0.62f, RightHip.Y + 0.02f, RootWorld.Z + 0.48f + 0.14f * Phase };
    const Mathematics::Axis3D RightFoot { RootWorld.X + StrideR,        RightHip.Y + 0.02f, RootWorld.Z + 0.06f + 0.12f * Phase };

    Capsules[0u] = TaperedBoneCapsule{ Pelvis,        0.150f, Chest,      0.190f };
    Capsules[1u] = TaperedBoneCapsule{ Chest,         0.110f, Head,       0.130f };
    Capsules[2u] = TaperedBoneCapsule{ LeftShoulder,  0.075f, LeftElbow,  0.065f };
    Capsules[3u] = TaperedBoneCapsule{ LeftElbow,     0.062f, LeftHand,   0.052f };
    Capsules[4u] = TaperedBoneCapsule{ RightShoulder, 0.075f, RightElbow, 0.065f };
    Capsules[5u] = TaperedBoneCapsule{ RightElbow,    0.062f, RightHand,  0.052f };
    Capsules[6u] = TaperedBoneCapsule{ LeftHip,       0.095f, LeftKnee,   0.078f };
    Capsules[7u] = TaperedBoneCapsule{ LeftKnee,      0.074f, LeftFoot,   0.058f };
    Capsules[8u] = TaperedBoneCapsule{ RightHip,      0.095f, RightKnee,  0.078f };
    Capsules[9u] = TaperedBoneCapsule{ RightKnee,     0.074f, RightFoot,  0.058f };
}

void DeformingCapsuleRig::AppendRigPolygonGeometry(
    GeometrySpace::PolygonNetwork& OutNetwork,
    uint16_t                       PrimaryMaterialSlot,
    uint16_t                       AccentMaterialSlot,
    uint32_t                       RadialSides) const
{
    for (std::size_t CapIdx = 0u; CapIdx < Capsules.size(); ++CapIdx)
    {
        const bool     AccentLimb = (CapIdx == 1u || CapIdx == 3u || CapIdx == 5u);
        const uint16_t ActiveSlot = AccentLimb ? AccentMaterialSlot : PrimaryMaterialSlot;
        AppendSingleTaperedLimb(OutNetwork, Capsules[CapIdx], ActiveSlot, RadialSides);
    }
}

} // namespace Frontier::SpatialAcceleration
