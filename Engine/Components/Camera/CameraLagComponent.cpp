/*============================================================================================================================================
                                                       CAMERALAGCOMPONENT.CPP
============================================================================================================================================*/
// 🧩 Implementation of reusable exponential eye and orientation lag with teleport cut detection

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraLagComponent.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Components
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   LIFECYCLE & INPUT
//------------------------------------------------------------------------------------------------------------------------

void CameraLagComponent::Configure(
    float EyeLagSecondsValue,
    float TargetLagSecondsValue,
    float TeleportSpanMetersValue) noexcept
{
    EyeLagSeconds      = std::clamp(EyeLagSecondsValue, 0.0f, 1.0f);
    TargetLagSeconds   = std::clamp(TargetLagSecondsValue, 0.0f, 1.0f);
    TeleportSpanMeters = std::clamp(TeleportSpanMetersValue, 0.05f, 100.0f);
}

void CameraLagComponent::Initialize(Mathematics::Axis3D Eye, Mathematics::Axis3D Target) noexcept
{
    DesiredEye          = Eye;
    DesiredTarget       = Target;
    SmoothedEye         = Eye;
    SmoothedTarget      = Target;
    Initialized         = true;
    TeleportedLastAdvance = false;
}

void CameraLagComponent::AssignTargets(Mathematics::Axis3D Eye, Mathematics::Axis3D Target) noexcept
{
    if (!Initialized)
    {
        Initialize(Eye, Target);
        return;
    }
    DesiredEye    = Eye;
    DesiredTarget = Target;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    KINEMATIC ADVANCE
//------------------------------------------------------------------------------------------------------------------------

void CameraLagComponent::Advance(double Δt) noexcept
{
    if (!Initialized || !(Δt > 0.0))
    {
        return;
    }

    // 💡 A large eye jump is a camera cut (frame selection, project load); smoothing it would produce a visible fly-through.
    TeleportedLastAdvance =
        Mathematics::EvaluateLength(DesiredEye - SmoothedEye) > TeleportSpanMeters;
    if (TeleportedLastAdvance)
    {
        Snap();
        return;
    }

    // 📝 Exponential time constants are frame-rate independent and stable for variable authoring ticks.
    const float StepSeconds = static_cast<float>(std::min(Δt, 0.25));
    const float EyeAlpha = (EyeLagSeconds <= 0.0f)
        ? 1.0f
        : 1.0f - std::exp(-StepSeconds / EyeLagSeconds);
    const float TargetAlpha = (TargetLagSeconds <= 0.0f)
        ? 1.0f
        : 1.0f - std::exp(-StepSeconds / TargetLagSeconds);

    SmoothedEye    = Mathematics::InterpolateLinear(SmoothedEye, DesiredEye, EyeAlpha);
    SmoothedTarget = Mathematics::InterpolateLinear(SmoothedTarget, DesiredTarget, TargetAlpha);
}

void CameraLagComponent::Snap() noexcept
{
    SmoothedEye    = DesiredEye;
    SmoothedTarget = DesiredTarget;
}

} // namespace Frontier::Components
