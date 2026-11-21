#pragma once

#include "Engine/Mathematics/AxisCoordinates.h"
#include <algorithm>
#include <cmath>

namespace Frontier::ComponentClass
{
// Frame-rate independent smoothing of a desired translation velocity. Reset when changing interaction modes.
class CameraMovementLag
{
public:
    explicit CameraMovementLag(float ResponsePerSecond = 12.0f) noexcept : Response(ResponsePerSecond) {}

    Mathematics::Axis3D Advance(Mathematics::Axis3D DesiredVelocity, float DeltaSeconds) noexcept
    {
        const float Dt = std::max(0.0f, DeltaSeconds);
        const float Alpha = 1.0f - std::exp(-std::max(0.0f, Response) * Dt);
        // Exact integral of exponentially approaching velocity, so motion is stable across frame rates.
        const float Integral = Response > 0.0f ? Alpha / Response : Dt;
        const Mathematics::Axis3D Displacement = DesiredVelocity * Dt + (Velocity - DesiredVelocity) * Integral;
        Velocity = Velocity + (DesiredVelocity - Velocity) * Alpha;
        return Displacement;
    }

    void Reset() noexcept { Velocity = { 0.0f, 0.0f, 0.0f }; }

private:
    Mathematics::Axis3D Velocity { 0.0f, 0.0f, 0.0f };
    float Response;
};
}
