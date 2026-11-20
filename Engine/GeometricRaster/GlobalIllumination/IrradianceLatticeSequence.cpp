/*============================================================================================================================================
                                                        IRRADIANCELATTICESEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Dynamic Octahedral Irradiance Cache Compatibility Facade (8x8 Octahedral + 16x16 Chebyshev Depth, Zero 3D SDF)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/IrradianceLatticeSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/DynamicIrradianceCacheSequence.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] Mathematics::Axis2D EncodeUnitOctahedralUv(Mathematics::Axis3D Dir) noexcept
{
    const float InvL1 = 1.0f / std::max(1.0e-6f, std::fabs(Dir.X) + std::fabs(Dir.Y) + std::fabs(Dir.Z));
    float OX = Dir.X * InvL1;
    float OY = Dir.Y * InvL1;
    if (Dir.Z < 0.0f)
    {
        const float WrapX = (1.0f - std::fabs(OY)) * ((OX >= 0.0f) ? 1.0f : -1.0f);
        const float WrapY = (1.0f - std::fabs(OX)) * ((OY >= 0.0f) ? 1.0f : -1.0f);
        OX = WrapX;
        OY = WrapY;
    }
    return { OX * 0.5f + 0.5f, OY * 0.5f + 0.5f };
}

template <typename ElemType, std::size_t GridW>
[[nodiscard]] ElemType SampleOctahedralBilinear(
    const std::array<ElemType, GridW * GridW>& Sheet,
    Mathematics::Axis3D                        Direction) noexcept
{
    const Mathematics::Axis2D Uv = EncodeUnitOctahedralUv(Direction);
    const float FX = std::clamp(Uv.X * static_cast<float>(GridW) - 0.5f, 0.0f, static_cast<float>(GridW - 1u));
    const float FY = std::clamp(Uv.Y * static_cast<float>(GridW) - 0.5f, 0.0f, static_cast<float>(GridW - 1u));
    const auto  X0 = static_cast<uint32_t>(std::floor(FX));
    const auto  Y0 = static_cast<uint32_t>(std::floor(FY));
    const uint32_t X1 = std::min(static_cast<uint32_t>(GridW - 1u), X0 + 1u);
    const uint32_t Y1 = std::min(static_cast<uint32_t>(GridW - 1u), Y0 + 1u);
    const float TX = FX - static_cast<float>(X0);
    const float TY = FY - static_cast<float>(Y0);

    const ElemType& C00 = Sheet[Y0 * GridW + X0];
    const ElemType& C10 = Sheet[Y0 * GridW + X1];
    const ElemType& C01 = Sheet[Y1 * GridW + X0];
    const ElemType& C11 = Sheet[Y1 * GridW + X1];

    return (C00 * (1.0f - TX) + C10 * TX) * (1.0f - TY) +
           (C01 * (1.0f - TX) + C11 * TX) * TY;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              IRRADIANCE LATTICE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

void IrradianceLatticeSequence::PropagateRtxIrradianceLattice(
    const IrradianceLatticeConfiguration&           Config,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
    const SolarDirectionalSource&                   Sun,
    Mathematics::LinearRGB                          SkyRadiance)
{
    ActiveConfig        = Config;
    ActiveConfig.CountX = std::max(2u, Config.CountX);
    ActiveConfig.CountY = std::max(2u, Config.CountY);
    ActiveConfig.CountZ = std::max(2u, Config.CountZ);

    DynamicIrradianceCacheConfiguration IrradianceCfg {};
    IrradianceCfg.GridMinimumWorld  = ActiveConfig.GridMinimumWorld;
    IrradianceCfg.GridMaximumWorld  = ActiveConfig.GridMaximumWorld;
    IrradianceCfg.CountX            = ActiveConfig.CountX;
    IrradianceCfg.CountY            = ActiveConfig.CountY;
    IrradianceCfg.CountZ            = ActiveConfig.CountZ;
    IrradianceCfg.FibonacciRayCount = std::clamp(ActiveConfig.FibonacciRayCount, 32u, 256u);
    IrradianceCfg.BouncePasses      = std::clamp(ActiveConfig.BouncePasses, 1u, 4u);

    DynamicIrradianceCacheSequence IrradianceSolver {};
    IrradianceSolver.UpdateIrradianceCache(
        IrradianceCfg,
        SceneTree,
        BounceSummaries,
        Sun,
        std::span<const PolygonalAreaQuadLight>{},
        SkyRadiance);

    AdoptIrradianceCacheCells(IrradianceSolver);
}

void IrradianceLatticeSequence::AdoptIrradianceCacheCells(const DynamicIrradianceCacheSequence& IrradianceCache)
{
    const auto& SrcCfg             = IrradianceCache.InspectConfig();
    ActiveConfig.GridMinimumWorld  = SrcCfg.GridMinimumWorld;
    ActiveConfig.GridMaximumWorld  = SrcCfg.GridMaximumWorld;
    ActiveConfig.CountX            = std::max(2u, SrcCfg.CountX);
    ActiveConfig.CountY            = std::max(2u, SrcCfg.CountY);
    ActiveConfig.CountZ            = std::max(2u, SrcCfg.CountZ);
    ActiveConfig.FibonacciRayCount = SrcCfg.FibonacciRayCount;
    ActiveConfig.BouncePasses      = SrcCfg.BouncePasses;
    CellSpacing                    = IrradianceCache.InspectCellSpacing();

    const auto SrcCells = IrradianceCache.InspectCells();
    Cells.resize(SrcCells.size());
    for (std::size_t Idx = 0u; Idx < SrcCells.size(); ++Idx)
    {
        Cells[Idx].WorldPosition    = SrcCells[Idx].WorldPosition;
        Cells[Idx].IrradianceOverPi = SrcCells[Idx].IrradianceOverPi;
        Cells[Idx].DepthMoments     = SrcCells[Idx].DepthMoments;
        Cells[Idx].BackfaceRatio    = SrcCells[Idx].BackfaceRatio;
    }
}

Mathematics::LinearRGB IrradianceLatticeSequence::SampleChebyshevIrradianceOverPi(
    Mathematics::Axis3D WorldPosition,
    Mathematics::Axis3D SurfaceNormal,
    Mathematics::Axis3D ViewDirection) const noexcept
{
    if (Cells.empty() || ActiveConfig.CountX < 2u || ActiveConfig.CountY < 2u || ActiveConfig.CountZ < 2u)
    {
        return {};
    }

    const float BiasMag = std::min(0.18f, 0.25f * std::min({ CellSpacing.X, CellSpacing.Y, CellSpacing.Z }));
    const Mathematics::Axis3D BiasedPos = WorldPosition + (SurfaceNormal * 0.70f + ViewDirection * 0.30f) * BiasMag;

    const float FX = std::clamp((BiasedPos.X - ActiveConfig.GridMinimumWorld.X) / std::max(1.0e-4f, CellSpacing.X),
                                0.0f, static_cast<float>(ActiveConfig.CountX - 1u));
    const float FY = std::clamp((BiasedPos.Y - ActiveConfig.GridMinimumWorld.Y) / std::max(1.0e-4f, CellSpacing.Y),
                                0.0f, static_cast<float>(ActiveConfig.CountY - 1u));
    const float FZ = std::clamp((BiasedPos.Z - ActiveConfig.GridMinimumWorld.Z) / std::max(1.0e-4f, CellSpacing.Z),
                                0.0f, static_cast<float>(ActiveConfig.CountZ - 1u));

    const uint32_t IX0 = std::min(ActiveConfig.CountX - 2u, static_cast<uint32_t>(std::floor(FX)));
    const uint32_t IY0 = std::min(ActiveConfig.CountY - 2u, static_cast<uint32_t>(std::floor(FY)));
    const uint32_t IZ0 = std::min(ActiveConfig.CountZ - 2u, static_cast<uint32_t>(std::floor(FZ)));
    const float    TX  = FX - static_cast<float>(IX0);
    const float    TY  = FY - static_cast<float>(IY0);
    const float    TZ  = FZ - static_cast<float>(IZ0);

    Mathematics::LinearRGB WeightedIrradiance {};
    float                  TotalWeight        = 0.0f;

    for (uint32_t Corner = 0u; Corner < 8u; ++Corner)
    {
        const uint32_t CX = IX0 + ((Corner & 1u) != 0u ? 1u : 0u);
        const uint32_t CY = IY0 + ((Corner & 2u) != 0u ? 1u : 0u);
        const uint32_t CZ = IZ0 + ((Corner & 4u) != 0u ? 1u : 0u);
        const float TriW  = ((Corner & 1u) != 0u ? TX : (1.0f - TX)) *
                            ((Corner & 2u) != 0u ? TY : (1.0f - TY)) *
                            ((Corner & 4u) != 0u ? TZ : (1.0f - TZ));

        const std::size_t CellIdx = (static_cast<std::size_t>(CZ) * ActiveConfig.CountY + CY) * ActiveConfig.CountX + CX;
        const auto&       Cell    = Cells[CellIdx];
        const float ValidityW     = (Cell.BackfaceRatio > 0.25f) ? 0.01f : 1.0f;

        const Mathematics::Axis3D Delta = BiasedPos - Cell.WorldPosition;
        const float Dist = Mathematics::EvaluateLength(Delta);
        const Mathematics::Axis3D DirCellToSurf = (Dist > 1.0e-5f) ? (Delta * (1.0f / Dist)) : (SurfaceNormal * -1.0f);
        const float WrapCos = 0.5f * (Mathematics::EvaluateDot(DirCellToSurf * -1.0f, SurfaceNormal) + 1.0f);
        const float SmoothBack = std::max(0.02f, WrapCos * WrapCos);

        const Mathematics::Axis2D Moments = SampleOctahedralBilinear<Mathematics::Axis2D, ChebyshevDepthOctaW>(
            Cell.DepthMoments, DirCellToSurf);
        const float MeanR    = Moments.X;
        const float Variance = std::max(0.05f, std::fabs(Moments.Y - MeanR * MeanR));
        float ChebyshevW     = 1.0f;
        if (Dist > MeanR)
        {
            const float Excess = Dist - MeanR;
            const float Ratio  = Variance / (Variance + Excess * Excess);
            ChebyshevW         = std::max(1.0e-3f, Ratio * Ratio * Ratio);
        }

        const float CombinedW = std::max(1.0e-7f, TriW * SmoothBack * ValidityW * ChebyshevW);
        const Mathematics::LinearRGB CellIrr = SampleOctahedralBilinear<Mathematics::LinearRGB, IrradianceOctaWidth>(
            Cell.IrradianceOverPi, SurfaceNormal);
        WeightedIrradiance = WeightedIrradiance + CellIrr * CombinedW;
        TotalWeight       += CombinedW;
    }

    return (TotalWeight > 0.0f) ? (WeightedIrradiance * (1.0f / TotalWeight)) : Mathematics::LinearRGB{};
}

} // namespace Frontier::GeometricRaster
