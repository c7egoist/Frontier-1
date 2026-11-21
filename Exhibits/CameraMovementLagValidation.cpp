#include "Engine/ComponentClass/CameraMovementLag.h"
#include <cmath>

int main()
{
    Frontier::ComponentClass::CameraMovementLag SixtyHz, ThirtyHz;
    float A = 0.0f, B = 0.0f;
    for (int I = 0; I < 60; ++I) A += SixtyHz.Advance({ 1.0f, 0.0f, 0.0f }, 1.0f / 60.0f).X;
    for (int I = 0; I < 30; ++I) B += ThirtyHz.Advance({ 1.0f, 0.0f, 0.0f }, 1.0f / 30.0f).X;
    if (std::fabs(A - B) > 0.0001f || A <= 0.9f || A >= 1.0f) return 1;
    SixtyHz.Reset();
    return SixtyHz.Advance({ 0.0f, 0.0f, 0.0f }, 0.1f).X == 0.0f ? 0 : 1;
}
