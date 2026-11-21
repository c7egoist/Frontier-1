/*============================================================================================================================================
                                                        CAMERALAGCOMPONENT.CPP
============================================================================================================================================*/
// 🧩 Frame-rate-independent exponential lag with teleport re-priming; no allocation and no platform dependencies

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraLagComponent.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Components
{
namespace
{

[[nodiscard]] float EvaluateBlendFactor(float LagSeconds, float DeltaSeconds) noexcept
{
    if (LagSeconds <= 0.0f)
    {
        return 1.0f;
    }
    return 1.0f - std::exp(-DeltaSeconds / std::max(0.001f, LagSeconds));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

void CameraLagComponent::Configure(CameraLagConfiguration NewConfiguration) noexcept
{
    Configuration = NewConfiguration;
}

void CameraLagComponent::Reset(Mathematics::Axis3D DesiredEye, Mathematics::Axis3D DesiredTarget) noexcept
{
    CurrentPose.Eye    = DesiredEye;
    CurrentPose.Target = DesiredTarget;
    DesiredPose.Eye    = DesiredEye;
    DesiredPose.Target = DesiredTarget;
    Primed             = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      KINEMATICS
//------------------------------------------------------------------------------------------------------------------------

CameraLagPose CameraLagComponent::Advance(
    Mathematics::Axis3D DesiredEye,
    Mathematics::Axis3D DesiredTarget,
    float               DeltaSeconds) noexcept
{
    const bool Snap =
        !Configuration.Enabled ||
        !Primed ||
        !std::isfinite(DeltaSeconds) ||
        DeltaSeconds <= 0.0f ||
        Mathematics::EvaluateDistance(DesiredEye, CurrentPose.Eye) > Configuration.TeleportDistanceMeters;
    if (Snap)
    {
        Reset(DesiredEye, DesiredTarget);
        return CurrentPose;
    }

    DesiredPose.Eye        = DesiredEye;
    DesiredPose.Target     = DesiredTarget;
    const float EyeBlend   = EvaluateBlendFactor(Configuration.TranslationLagSeconds, DeltaSeconds);
    const float TargetBlend= EvaluateBlendFactor(Configuration.RotationLagSeconds, DeltaSeconds);
    CurrentPose.Eye        = CurrentPose.Eye + (DesiredEye - CurrentPose.Eye) * EyeBlend;
    CurrentPose.Target     = CurrentPose.Target + (DesiredTarget - CurrentPose.Target) * TargetBlend;
    return CurrentPose;
}

bool CameraLagComponent::QuerySettling() const noexcept
{
    return Primed &&
           Configuration.Enabled &&
           (Mathematics::EvaluateDistance(DesiredPose.Eye, CurrentPose.Eye) > 0.001f ||
            Mathematics::EvaluateDistance(DesiredPose.Target, CurrentPose.Target) > 0.001f);
}

} // namespace Frontier::Components
