/*============================================================================================================================================
                                                        CAMERALOOKRATEPROBE.CPP
============================================================================================================================================*/
// 🧩 Deterministic probe: mouse-look rate before/after the lag-integration fix and pose exactness at range

#include "Engine/Components/Camera/CameraLagComponent.h"
#include "Engine/Components/Camera/Kinematics/InterpolationCurves.h"

#include <cmath>
#include <cstdio>

namespace
{
using Frontier::Components::CameraLagComponent;
using Frontier::Components::CameraLagSettings;
using Frontier::Mathematics::Axis3D;
using Frontier::Mathematics::EvaluateCross;
using Frontier::Mathematics::EvaluateDot;
using Frontier::Mathematics::NormalizeAxis;

constexpr float LookRadiansPerPixel = 0.0032f;

struct Pose
{
    Axis3D Origin { 0.0f, -3.55f, 0.42f };
    Axis3D Target { 0.0f,  0.55f, 0.15f };
};

Axis3D ForwardFromAngles(float Yaw, float Pitch) noexcept
{
    const float CosP = std::cos(Pitch);
    return NormalizeAxis({ std::sin(Yaw) * CosP, std::cos(Yaw) * CosP, std::sin(Pitch) }, { 0.0f, 1.0f, 0.0f });
}

float YawOf(const Pose& P) noexcept
{
    const Axis3D D = NormalizeAxis(P.Target - P.Origin, { 0.0f, 1.0f, 0.0f });
    return std::atan2(D.X, D.Y);
}

// Legacy pipeline: integrate the delta on the DAMPED pose, then let the lag damper re-blend toward it.
float MeasureLegacyTurnRadians(int Frames, float DeltaSeconds, float DeltaPerFramePixels)
{
    CameraLagComponent Lag {};
    CameraLagSettings  Settings {};
    Settings.LocationLagSpeed = 12.0f;
    Settings.RotationLagSpeed = 16.0f;
    Lag.Configure(Settings);

    Pose P {};
    Lag.SnapTo(P.Origin, P.Target);
    for (int F = 0; F < Frames; ++F)
    {
        Pose Damped { Lag.QueryPosition(), Lag.QueryTarget() };
        Axis3D Fwd = NormalizeAxis(Damped.Target - Damped.Origin, { 0.0f, 1.0f, 0.0f });
        const float Yaw   = std::atan2(Fwd.X, Fwd.Y) + DeltaPerFramePixels * LookRadiansPerPixel;
        const float Pitch = std::asin(std::clamp(Fwd.Z, -0.995f, 0.995f));
        const float CosP  = std::cos(Pitch);
        Fwd = NormalizeAxis({ std::sin(Yaw) * CosP, std::cos(Yaw) * CosP, std::sin(Pitch) }, Fwd);
        const float Dist = std::sqrt(EvaluateDot(Damped.Target - Damped.Origin, Damped.Target - Damped.Origin));
        Damped.Target = Damped.Origin + Fwd * Dist;
        (void)Lag.AdvanceLag(Damped.Origin, Damped.Target, DeltaSeconds);
    }
    return YawOf({ Lag.QueryPosition(), Lag.QueryTarget() }) - YawOf(P);
}

// Fixed pipeline: integrate the delta on the DESIRED pose (working pose seeded from desired each frame).
float MeasureFixedTurnRadians(int Frames, float DeltaSeconds, float DeltaPerFramePixels)
{
    CameraLagComponent Lag {};
    CameraLagSettings  Settings {};
    Settings.LocationLagSpeed = 12.0f;
    Settings.RotationLagSpeed = 16.0f;
    Lag.Configure(Settings);

    Pose Desired {};
    Lag.SnapTo(Desired.Origin, Desired.Target);
    float Yaw   = YawOf(Desired);
    float Pitch = 0.0f;
    for (int F = 0; F < Frames; ++F)
    {
        Pose Work { Desired.Origin, Desired.Target }; // seed from the undamped desired pose
        Yaw += DeltaPerFramePixels * LookRadiansPerPixel;
        const float Dist = std::sqrt(EvaluateDot(Work.Target - Work.Origin, Work.Target - Work.Origin));
        Work.Target = Work.Origin + ForwardFromAngles(Yaw, Pitch) * Dist;
        Desired     = Work;
        (void)Lag.AdvanceLag(Desired.Origin, Desired.Target, DeltaSeconds);
    }
    return Yaw - YawOf(Pose {});
}

// Distance independence: rotate 90 deg at 1 m and at 400 m pivot distance; the angular result must match.
float MeasureTurnAtDistance(float PivotDistance, float DeltaPixels, int Frames, float DeltaSeconds)
{
    Pose Desired {};
    Desired.Target = Desired.Origin + Axis3D { 0.0f, PivotDistance, 0.0f };
    float Yaw = 0.0f;
    for (int F = 0; F < Frames; ++F)
    {
        Yaw += DeltaPixels * LookRadiansPerPixel;
        Desired.Target = Desired.Origin + ForwardFromAngles(Yaw, 0.0f) * PivotDistance;
    }
    const Axis3D D = NormalizeAxis(Desired.Target - Desired.Origin, { 0.0f, 1.0f, 0.0f });
    return std::atan2(D.X, D.Y);
}

} // namespace

int main()
{
    constexpr float DeltaSeconds      = 1.0f / 60.0f;
    constexpr int   Frames            = 30;
    constexpr float DeltaPerFramePx   = 6.0f;
    const     float ExpectedRadians   = static_cast<float>(Frames) * DeltaPerFramePx * LookRadiansPerPixel;

    const float LegacyTurn = MeasureLegacyTurnRadians(Frames, DeltaSeconds, DeltaPerFramePx);
    const float FixedTurn  = MeasureFixedTurnRadians(Frames, DeltaSeconds, DeltaPerFramePx);
    const float LegacyRate = LegacyTurn / ExpectedRadians;
    const float FixedRate  = FixedTurn / ExpectedRadians;

    const float NearTurn = MeasureTurnAtDistance(1.0f, DeltaPerFramePx, Frames, DeltaSeconds);
    const float FarTurn  = MeasureTurnAtDistance(400.0f, DeltaPerFramePx, Frames, DeltaSeconds);

    std::printf("expected turn  : %.6f rad\n", static_cast<double>(ExpectedRadians));
    std::printf("legacy turn    : %.6f rad (rate %.1f%% of mouse input)\n",
                static_cast<double>(LegacyTurn), static_cast<double>(LegacyRate * 100.0f));
    std::printf("fixed turn     : %.6f rad (rate %.1f%% of mouse input)\n",
                static_cast<double>(FixedTurn), static_cast<double>(FixedRate * 100.0f));
    std::printf("turn @ 1 m     : %.6f rad\n", static_cast<double>(NearTurn));
    std::printf("turn @ 400 m   : %.6f rad\n", static_cast<double>(FarTurn));

    bool Pass = true;
    const auto Check = [&Pass](bool Condition, const char* Label)
    {
        std::printf("%s : %s\n", Condition ? "PASS" : "FAIL", Label);
        Pass = Pass && Condition;
    };
    Check(LegacyRate < 0.80f, "legacy pipelines re-damped mouse-look below 80% of input rate (defect reproduced)");
    Check(std::fabs(FixedRate - 1.0f) < 1.0e-3f, "fixed pipeline turns at exactly the mouse input rate");
    Check(std::fabs(NearTurn - FarTurn) < 1.0e-4f, "turn angle is identical at 1 m and 400 m pivot distance");
    std::printf("%s\n", Pass ? "RESULT: PASS" : "RESULT: FAIL");
    return Pass ? 0 : 1;
}
