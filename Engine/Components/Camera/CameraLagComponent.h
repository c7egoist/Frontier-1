/*============================================================================================================================================
                                                        CAMERALAGCOMPONENT.H
============================================================================================================================================*/
// 🧩 Reusable frame-rate-independent eye/target lag for editor, follow-cam and cinematic camera rigs

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>

namespace Frontier::Components
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct CameraLagConfiguration
{
    bool  Enabled                = true;  // [-] - Master lag toggle (false snaps directly to the desired posture)
    float TranslationLagSeconds  = 0.12f; // [s] - Exponential time constant for eye position lag (0 = no lag)
    float RotationLagSeconds     = 0.07f; // [s] - Exponential time constant for look-at target lag (0 = no lag)
    float TeleportDistanceMeters = 15.0f; // [m] - Eye jump above which the lag is re-primed instead of interpolating
};

struct CameraLagPose
{
    Mathematics::Axis3D Eye    {}; // [m] - Smoothed world-space eye position
    Mathematics::Axis3D Target {}; // [m] - Smoothed world-space look-at target
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         CLASSES
//------------------------------------------------------------------------------------------------------------------------

class CameraLagComponent
{
public:
    void Configure(CameraLagConfiguration NewConfiguration) noexcept;
    void Reset(Mathematics::Axis3D DesiredEye, Mathematics::Axis3D DesiredTarget) noexcept;

    [[nodiscard]] CameraLagPose Advance(
        Mathematics::Axis3D DesiredEye,
        Mathematics::Axis3D DesiredTarget,
        float               DeltaSeconds) noexcept;

    [[nodiscard]] const CameraLagConfiguration& InspectConfiguration() const noexcept { return Configuration; }
    [[nodiscard]] const CameraLagPose&          InspectPose() const noexcept { return CurrentPose; }
    [[nodiscard]] bool                          QuerySettling() const noexcept;

private:
    CameraLagConfiguration Configuration {};
    CameraLagPose          CurrentPose   {};
    CameraLagPose          DesiredPose   {};
    bool                   Primed        = false;
};

} // namespace Frontier::Components
