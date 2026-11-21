/*============================================================================================================================================
                                                        CAMERALAGCOMPONENT.H
============================================================================================================================================*/
// 🧩 Reusable frame-rate-independent eye/target lag for editor and runtime cameras with teleport-safe convergence

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>

namespace Frontier::Components
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         CLASSES
//------------------------------------------------------------------------------------------------------------------------

class CameraLagComponent
{
public:
    void Configure(
        float EyeLagSeconds,
        float TargetLagSeconds,
        float TeleportSpanMeters = 2.50f) noexcept;
    void Initialize(Mathematics::Axis3D Eye, Mathematics::Axis3D Target) noexcept;
    void AssignTargets(Mathematics::Axis3D Eye, Mathematics::Axis3D Target) noexcept;
    void Advance(double Δt) noexcept;
    void Snap() noexcept;

    [[nodiscard]] Mathematics::Axis3D QuerySmoothedEye() const noexcept { return SmoothedEye; }
    [[nodiscard]] Mathematics::Axis3D QuerySmoothedTarget() const noexcept { return SmoothedTarget; }
    [[nodiscard]] bool QueryTeleported() const noexcept { return TeleportedLastAdvance; }
    [[nodiscard]] float QueryEyeLagSeconds() const noexcept { return EyeLagSeconds; }
    [[nodiscard]] float QueryTargetLagSeconds() const noexcept { return TargetLagSeconds; }

private:
    Mathematics::Axis3D DesiredEye      {}; // [m] - Latest authoritative eye position from navigation input
    Mathematics::Axis3D DesiredTarget   {}; // [m] - Latest authoritative look-at position from navigation input
    Mathematics::Axis3D SmoothedEye     {}; // [m] - Eye position presented to the view transform this frame
    Mathematics::Axis3D SmoothedTarget  {}; // [m] - Look-at position presented to the view transform this frame
    float               EyeLagSeconds     = 0.0f; // [s] - Exponential eye pursuit time constant (0 disables lag)
    float               TargetLagSeconds  = 0.0f; // [s] - Exponential orientation pursuit time constant (0 disables lag)
    float               TeleportSpanMeters = 2.50f; // [m] - Eye jump treated as a camera cut instead of a fly step
    bool                Initialized     = false;
    bool                TeleportedLastAdvance = false;
};

} // namespace Frontier::Components
