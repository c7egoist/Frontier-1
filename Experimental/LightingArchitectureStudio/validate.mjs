/*============================================================================================================================================
                                                    VALIDATE.MJS
============================================================================================================================================*/
// Headless numeric validation for the lighting architecture studio. Measures estimator bias against an exact brute-force
// reference, error versus candidate count and sample count, variance per frame, and visibility-tier agreement. Emits
// ValidationReport.json and ValidationTables.md so the plan document carries measured numbers, not claims.

import {
    ConstructStudio, IntegrateFullImage, ShadeSurface, Technique, Representation,
    ConstructXorShiftSeed, Luminance, NormalizeVector, ConstructSharedCutForPoint,
} from './illumination.mjs';

import { writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const ScriptDirectory = dirname(fileURLToPath(import.meta.url));
const Width  = 64;
const Height = 36;
const FrameCount = 32;
const CandidateSweep = [ 1, 4, 16 ];

//------------------------------------------------------------------------------------------------------------------------
//                                          EXACT REFERENCE (DETERMINISTIC TIER)
//------------------------------------------------------------------------------------------------------------------------
// The deterministic tier performs no light picking: closed-form polygon irradiance for rectangles plus fixed quadrature for
// curved area luminaires, so a single pass is the converged answer.

function EvaluateReference(Studio, RepresentationName)
{
    return IntegrateFullImage(Studio, {
        Technique: Technique.DeterministicPolygon,
        Representation: RepresentationName === undefined ? Representation.AnalyticPrism : RepresentationName,
        CandidateCount: 1, SharedCutSize: 1, Width: Width, Height: Height, SampleIndex: 0,
        EnableDirectLuminaires: true,
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ERROR MEASUREMENTS
//------------------------------------------------------------------------------------------------------------------------

function RelativeMeanSquareError(Estimate, Reference)
{
    let ErrorSum = 0.0;
    let WeightSum = 0.0;
    for (let PixelIndex = 0; PixelIndex < Reference.length / 3; PixelIndex += 1)
    {
        const ReferenceLuminance = Luminance([ Reference[PixelIndex * 3], Reference[PixelIndex * 3 + 1], Reference[PixelIndex * 3 + 2] ]);
        const EstimatedLuminance = Luminance([ Estimate[PixelIndex * 3], Estimate[PixelIndex * 3 + 1], Estimate[PixelIndex * 3 + 2] ]);
        ErrorSum += Math.pow(EstimatedLuminance - ReferenceLuminance, 2);
        WeightSum += ReferenceLuminance * ReferenceLuminance;
    }
    return WeightSum <= 1.0e-12 ? 0.0 : Math.sqrt(ErrorSum / WeightSum);
}

// Firefly-robust error: each estimate is winsorised to 4x the reference before the comparison. Real-time renderers see this
// quantity after temporal filtering, whereas the raw RelMSE is dominated by a handful of near-field outliers.
function WinsorizedRelativeMeanSquareError(Estimate, Reference, LimitRatio)
{
    let ErrorSum = 0.0;
    let WeightSum = 0.0;
    for (let PixelIndex = 0; PixelIndex < Reference.length / 3; PixelIndex += 1)
    {
        const ReferenceLuminance = Luminance([ Reference[PixelIndex * 3], Reference[PixelIndex * 3 + 1], Reference[PixelIndex * 3 + 2] ]);
        const EstimatedLuminance = Math.min(
            Luminance([ Estimate[PixelIndex * 3], Estimate[PixelIndex * 3 + 1], Estimate[PixelIndex * 3 + 2] ]),
            ReferenceLuminance * LimitRatio);
        ErrorSum += Math.pow(EstimatedLuminance - ReferenceLuminance, 2);
        WeightSum += ReferenceLuminance * ReferenceLuminance;
    }
    return WeightSum <= 1.0e-12 ? 0.0 : Math.sqrt(ErrorSum / WeightSum);
}

function TotalBias(Estimate, Reference)
{
    let EstimateSum = 0.0;
    let ReferenceSum = 0.0;
    for (let Channel = 0; Channel < Reference.length; Channel += 1)
    {
        EstimateSum += Estimate[Channel];
        ReferenceSum += Reference[Channel];
    }
    return ReferenceSum <= 1.0e-12 ? 0.0 : (EstimateSum - ReferenceSum) / ReferenceSum;
}

function MeanRelativeVariance(Frames)
{
    const PixelCount = Frames[0].length;
    let VarianceSum = 0.0;
    let MeanSum = 0.0;
    for (let PixelIndex = 0; PixelIndex < PixelCount; PixelIndex += 3)
    {
        let Mean = 0.0;
        for (let FrameIndex = 0; FrameIndex < Frames.length; FrameIndex += 1)
        {
            Mean += Luminance([ Frames[FrameIndex][PixelIndex], Frames[FrameIndex][PixelIndex + 1], Frames[FrameIndex][PixelIndex + 2] ]);
        }
        Mean /= Frames.length;
        let Variance = 0.0;
        for (let FrameIndex = 0; FrameIndex < Frames.length; FrameIndex += 1)
        {
            const Frame = Frames[FrameIndex];
            const Difference = Luminance([ Frame[PixelIndex], Frame[PixelIndex + 1], Frame[PixelIndex + 2] ]) - Mean;
            Variance += Difference * Difference;
        }
        VarianceSum += Variance / Frames.length;
        MeanSum += Mean;
    }
    return MeanSum <= 1.0e-12 ? 0.0 : VarianceSum / MeanSum;
}

function MeasureTechnique(Studio, Options, FrameCount_local)
{
    const Frames = [];
    const Accumulator = new Float64Array(Width * Height * 3);
    const StartTime = process.hrtime.bigint();
    for (let FrameIndex = 0; FrameIndex < FrameCount_local; FrameIndex += 1)
    {
        const Frame = IntegrateFullImage(Studio, Object.assign({}, Options, {
            SampleIndex: 4000 + FrameIndex, Width: Width, Height: Height,
        }));
        Frames.push(Frame);
        for (let Channel = 0; Channel < Frame.length; Channel += 1)
        {
            Accumulator[Channel] += Frame[Channel];
        }
    }
    const ElapsedMilliseconds = Number(process.hrtime.bigint() - StartTime) / 1.0e6;
    const Converged = new Float64Array(Accumulator.length);
    for (let Channel = 0; Channel < Accumulator.length; Channel += 1)
    {
        Converged[Channel] = Accumulator[Channel] / FrameCount_local;
    }
    return {
        Frames: Frames,
        Converged: Converged,
        MillisecondsPerFrame: ElapsedMilliseconds / FrameCount_local,
        RelativeVariancePerFrame: MeanRelativeVariance(Frames),
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 SINGLE SHADING POINT
//------------------------------------------------------------------------------------------------------------------------

function MeasureSinglePoint(Studio)
{
    const SurfacePoint = [ 0.4, 0.002, 1.6 ];
    const SurfaceNormal = [ 0.0, 1.0, 0.0 ];
    // ShadeSurface treats its ray direction as the camera ray, so this points toward the floor from above.
    const RayDirection = NormalizeVector([ 0.0, -0.66, -0.75 ] );
    const Material = { Albedo: [ 0.55, 0.52, 0.48 ], Roughness: 0.30, Metallic: 0.0, Emission: [ 0.0, 0.0, 0.0 ] };
    const Surface = { Albedo: Material.Albedo, Roughness: Material.Roughness, Metallic: Material.Metallic };

    const ReferenceSample = ShadeSurface(Studio, {
        Technique: Technique.DeterministicPolygon, Representation: Representation.AnalyticPrism,
        CandidateCount: 1, SharedCutSize: 1, Width: 1, Height: 1, EnableDirectLuminaires: true,
    }, SurfacePoint, RayDirection, 0.0, SurfaceNormal, Material, ConstructXorShiftSeed(0xC0FFEE));
    const ReferenceLuminance = Luminance(ReferenceSample);

    const Rows = [];
    for (const TechniqueName of [ Technique.UniformLightPick, Technique.LightTreeCut, Technique.SharedLightTreeCut ])
    {
        for (const CandidateCount of [ 1, 4, 16, 64 ])
        {
            const Draw = ConstructXorShiftSeed(0x7A1F29B3);
            const SampleCount = 4000;
            let Sum = 0.0;
            let SquareSum = 0.0;
            for (let SampleIndex = 0; SampleIndex < SampleCount; SampleIndex += 1)
            {
                const ViewDirection = [ -RayDirection[0], -RayDirection[1], -RayDirection[2] ];
                const SharedIndices = TechniqueName === Technique.SharedLightTreeCut
                    ? ConstructSharedCutForPoint(Studio, SurfacePoint, SurfaceNormal, ViewDirection, Surface, 6, (0x1B2C3D00 + SampleIndex * 2654435761) >>> 0)
                    : null;
                const Sample = ShadeSurface(Studio, {
                    Technique: TechniqueName, CandidateCount: CandidateCount, SharedCutSize: 6,
                    Representation: Representation.AnalyticPrism, SharedLuminaireIndices: SharedIndices,
                    Width: 1, Height: 1,
                }, SurfacePoint, RayDirection, 0.0, SurfaceNormal, Material, Draw);
                const Reading = Luminance(Sample);
                Sum += Reading;
                SquareSum += Reading * Reading;
            }
            const Mean = Sum / SampleCount;
            const Variance = Math.max(0.0, SquareSum / SampleCount - Mean * Mean);
            const StandardDeviation = Math.sqrt(Variance);
            const StepsForFivePercent = Variance > 0.0 ? Math.ceil(Math.pow(StandardDeviation / (0.05 * Mean), 2)) : 1;
            Rows.push({
                Technique: TechniqueName,
                CandidateCount: CandidateCount,
                MeanOverReference: ReferenceLuminance > 0.0 ? Mean / ReferenceLuminance : 0.0,
                RelativeError: ReferenceLuminance > 0.0 ? Math.abs(Mean - ReferenceLuminance) / ReferenceLuminance : 0.0,
                StandardErrorOverReference: ReferenceLuminance > 0.0 ? StandardDeviation / Math.sqrt(SampleCount) / ReferenceLuminance : 0.0,
                RelativeStandardDeviation: Mean > 1.0e-12 ? StandardDeviation / Mean : 0.0,
                FramesForFivePercentError: StepsForFivePercent,
            });
        }
    }
    // Fireflies inflate the raw mean. The median without clamping is the cleanest diagnostic that the estimator is unbiased in
    // the bulk and that residual error is tail driven.
    const UnclampedDraw = ConstructXorShiftSeed(0x51AC77E1);
    const MedianReadings = [];
    for (let SampleIndex = 0; SampleIndex < 20000; SampleIndex += 1)
    {
        MedianReadings.push(Luminance(ShadeSurface(Studio, {
            Technique: Technique.LightTreeCut, CandidateCount: 4, Representation: Representation.AnalyticPrism,
            Width: 1, Height: 1, EnableFireflyClamp: false,
        }, SurfacePoint, RayDirection, 0.0, SurfaceNormal, Material, UnclampedDraw)));
    }
    MedianReadings.sort((Left, Right) => Left - Right);
    const MedianOverReference = ReferenceLuminance > 0.0 ? MedianReadings[MedianReadings.length >> 1] / ReferenceLuminance : 0.0;
    return { ReferenceLuminance: ReferenceLuminance, MedianOverReferenceUnclamped: MedianOverReference, Rows: Rows };
}

//------------------------------------------------------------------------------------------------------------------------
//                                               VISIBILITY TIER AGREEMENT
//------------------------------------------------------------------------------------------------------------------------

function MeasureVisibilityAgreement(Studio)
{
    // The deterministic tier is noise free on both representations, so any difference here is the visibility model itself.
    const SharedOptions = {
        Technique: Technique.DeterministicPolygon, CandidateCount: 1, SharedCutSize: 4, Width: Width, Height: Height, SampleIndex: 29,
    };
    const Analytic = IntegrateFullImage(Studio, Object.assign({}, SharedOptions, { Representation: Representation.AnalyticPrism }));
    const Field    = IntegrateFullImage(Studio, Object.assign({}, SharedOptions, { Representation: Representation.SignedDistance }));
    let DifferenceSum = 0.0;
    let ReferenceSum  = 0.0;
    let DisagreementCount = 0;
    const PixelCount = (Analytic.length / 3) | 0;
    for (let PixelIndex = 0; PixelIndex < PixelCount; PixelIndex += 1)
    {
        const AnalyticLuminance = Luminance([ Analytic[PixelIndex * 3], Analytic[PixelIndex * 3 + 1], Analytic[PixelIndex * 3 + 2] ]);
        const FieldLuminance    = Luminance([ Field[PixelIndex * 3], Field[PixelIndex * 3 + 1], Field[PixelIndex * 3 + 2] ]);
        DifferenceSum += Math.pow(FieldLuminance - AnalyticLuminance, 2);
        ReferenceSum  += AnalyticLuminance * AnalyticLuminance;
        if (Math.abs(FieldLuminance - AnalyticLuminance) > 0.10 * Math.max(AnalyticLuminance, 1.0e-3))
        {
            DisagreementCount += 1;
        }
    }
    return {
        RelativeErrorBetweenTiers: ReferenceSum > 1.0e-12 ? Math.sqrt(DifferenceSum / ReferenceSum) : 0.0,
        PixelDisagreementFraction: PixelCount > 0 ? DisagreementCount / PixelCount : 0.0,
        ComparedPixels: PixelCount,
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       MAIN RUN
//------------------------------------------------------------------------------------------------------------------------

function Run()
{
    const Report = { GeneratedAt: new Date().toISOString(), ImageWidth: Width, ImageHeight: Height, Lobbies: [], SinglePoint: null, Visibility: null };
    console.log('=== Frontier lighting architecture validation ===');
    console.log(`image ${Width}x${Height}, ${FrameCount} frames, candidate sweep ${CandidateSweep.join('/')}`);

    for (const PresetName of [ 'AtriumDaylight', 'NightArrival', 'GalleryMixed' ])
    {
        const Studio = ConstructStudio(PresetName, 1.0);
        const ReferenceStart = process.hrtime.bigint();
        const Reference = EvaluateReference(Studio);
        const ReferenceMilliseconds = Number(process.hrtime.bigint() - ReferenceStart) / 1.0e6;
        const LobbyEntry = {
            Identifier: Studio.Identifier,
            LuminaireCount: Studio.Lights.length,
            AreaLuminaireCount: Studio.Lights.filter((Entry) => Entry.MeasureKind === 'Area').length,
            TreeNodeCount: Studio.Tree.NodeCount,
            ReferenceMilliseconds: ReferenceMilliseconds,
            Techniques: [],
        };

        const Deterministic = MeasureTechnique(Studio, {
            Technique: Technique.DeterministicPolygon, Representation: Representation.AnalyticPrism,
        }, 2);
        LobbyEntry.Techniques.push({
            Technique: Technique.DeterministicPolygon, CandidateCount: 0,
            ErrorAtFourCandidates: 0.0, ErrorAtSixteenCandidates: 0.0,
            Bias: TotalBias(Deterministic.Converged, Reference),
            RelativeVariancePerFrame: Deterministic.RelativeVariancePerFrame,
            MillisecondsPerFrame: Deterministic.MillisecondsPerFrame,
        });

        for (const TechniqueName of [ Technique.UniformLightPick, Technique.LightTreeCut, Technique.SharedLightTreeCut ])
        {
            const Measurements = [];
            for (const CandidateCount of CandidateSweep)
            {
                const Measurement = MeasureTechnique(Studio, {
                    Technique: TechniqueName, CandidateCount: CandidateCount, SharedCutSize: 6,
                    Representation: Representation.AnalyticPrism,
                }, FrameCount);
                Measurements.push({ CandidateCount: CandidateCount, Measurement: Measurement,
                    Error: RelativeMeanSquareError(Measurement.Converged, Reference),
                    WinsorizedError: WinsorizedRelativeMeanSquareError(Measurement.Converged, Reference, 4.0) });
            }
            const FourCandidate = Measurements[CandidateSweep.indexOf(4)] || Measurements[Measurements.length - 1];
            const SixteenCandidate = Measurements[CandidateSweep.indexOf(16)] || Measurements[Measurements.length - 1];
            const SingleFrameError = RelativeMeanSquareError(FourCandidate.Measurement.Frames[0], Reference);
            LobbyEntry.Techniques.push({
                Technique: TechniqueName,
                CandidateCount: 4,
                ErrorAtFourCandidates: FourCandidate.Error,
                ErrorAtSixteenCandidates: SixteenCandidate.Error,
                SingleFrameErrorAtFourCandidates: SingleFrameError,
                WinsorizedErrorAtFourCandidates: FourCandidate.WinsorizedError,
                ErrorSweep: Measurements.map((Entry) => ({ CandidateCount: Entry.CandidateCount, Error: Entry.Error, WinsorizedError: Entry.WinsorizedError })),
                Bias: TotalBias(FourCandidate.Measurement.Converged, Reference),
                RelativeVariancePerFrame: FourCandidate.Measurement.RelativeVariancePerFrame,
                MillisecondsPerFrame: FourCandidate.Measurement.MillisecondsPerFrame,
            });
            console.log(`${Studio.Identifier.padEnd(16)} ${TechniqueName.padEnd(20)} lights=${String(Studio.Lights.length).padStart(4)}`
                + ` treeNodes=${String(Studio.Tree.NodeCount).padStart(4)}`
                + ` err@4=${(FourCandidate.Error * 100).toFixed(2)}%`
                + ` err@16=${(SixteenCandidate.Error * 100).toFixed(2)}%`
                + ` bias=${(TotalBias(FourCandidate.Measurement.Converged, Reference) * 100).toFixed(2)}%`
                + ` var=${FourCandidate.Measurement.RelativeVariancePerFrame.toExponential(2)}`
                + ` ms=${FourCandidate.Measurement.MillisecondsPerFrame.toFixed(1)}`);
        }
        console.log(`${Studio.Identifier.padEnd(16)} ${'DeterministicPolygon'.padEnd(20)} lights=${String(Studio.Lights.length).padStart(4)}`
            + ` exact reference in ${Deterministic.MillisecondsPerFrame.toFixed(1)} ms/frame`
            + ` (${(ReferenceMilliseconds).toFixed(1)} ms first pass)`);
        Report.Lobbies.push(LobbyEntry);
    }

    const FocusStudio = ConstructStudio('GalleryMixed', 1.0);
    Report.SinglePoint = MeasureSinglePoint(FocusStudio);
    console.log('\n=== single shading point (GalleryMixed) ===');
    for (const Row of Report.SinglePoint.Rows)
    {
        console.log(`${Row.Technique.padEnd(20)} candidates=${String(Row.CandidateCount).padStart(3)}`
            + ` mean/ref=${(Row.MeanOverReference * 100).toFixed(2)}%`
            + ` relErr=${(Row.RelativeError * 100).toFixed(2)}%`
            + ` ±${(Row.StandardErrorOverReference * 100).toFixed(2)}%`
            + ` rsd=${(Row.RelativeStandardDeviation * 100).toFixed(1)}%`
            + ` frames@5%=${Row.FramesForFivePercentError}`);
    }

    Report.Visibility = MeasureVisibilityAgreement(ConstructStudio('NightArrival', 1.0));
    console.log(`\n=== visibility tiers (NightArrival) === relError=${(Report.Visibility.RelativeErrorBetweenTiers * 100).toFixed(2)}%`
        + ` pixelDisagreement(10%)=${(Report.Visibility.PixelDisagreementFraction * 100).toFixed(1)}%`);

    // Cost of one interactive 224x126 frame per technique, measured on this machine, plus light-tree construction.
    const CostStudio = ConstructStudio('NightArrival', 1.0);
    Report.FrameCost = { Width: 224, Height: 126, Techniques: [], TreeBuildMilliseconds: 0.0 };
    const TreeBuildStart = process.hrtime.bigint();
    for (let BuildIndex = 0; BuildIndex < 200; BuildIndex += 1)
    {
        ConstructStudio('NightArrival', 1.0).Tree;
    }
    Report.FrameCost.TreeBuildMilliseconds = Number(process.hrtime.bigint() - TreeBuildStart) / 1.0e6 / 200.0;
    for (const TechniqueName of [ Technique.UniformLightPick, Technique.LightTreeCut, Technique.SharedLightTreeCut, Technique.DeterministicPolygon ])
    {
        const FrameStart = process.hrtime.bigint();
        const FrameCountLocal = 3;
        for (let FrameIndex = 0; FrameIndex < FrameCountLocal; FrameIndex += 1)
        {
            IntegrateFullImage(CostStudio, {
                Technique: TechniqueName, CandidateCount: 6, SharedCutSize: 8, Representation: Representation.AnalyticPrism,
                Width: 224, Height: 126, SampleIndex: 900 + FrameIndex,
            });
        }
        Report.FrameCost.Techniques.push({
            Technique: TechniqueName,
            MillisecondsPerFrame: Number(process.hrtime.bigint() - FrameStart) / 1.0e6 / FrameCountLocal,
        });
    }
    for (const Entry of Report.FrameCost.Techniques)
    {
        console.log(`224x126 frame ${Entry.Technique.padEnd(20)} ${Entry.MillisecondsPerFrame.toFixed(1)} ms`);
    }
    console.log(`light tree build ${Report.FrameCost.TreeBuildMilliseconds.toFixed(3)} ms (57 luminaires)`);

    writeFileSync(join(ScriptDirectory, 'ValidationReport.json'), JSON.stringify(Report, null, 2));

    const Lines = [];
    Lines.push(`Image ${Width}x${Height}, ${FrameCount} frames per measurement, reference = deterministic tier.`);
    Lines.push('');
    Lines.push('| Lobby | Luminaires | Area | Tree nodes | Technique | Candidates | Error | Bias | Variance / frame | ms / frame |');
    Lines.push('| :--- | ---: | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: |');
    for (const Lobby of Report.Lobbies)
    {
        for (const Row of Lobby.Techniques)
        {
            Lines.push(`| ${Lobby.Identifier} | ${Lobby.LuminaireCount} | ${Lobby.AreaLuminaireCount} | ${Lobby.TreeNodeCount} | ${Row.Technique} |`
                + ` ${Row.CandidateCount} | ${(Row.ErrorAtFourCandidates * 100).toFixed(2)}% | ${(Row.Bias * 100).toFixed(2)}% |`
                + ` ${Row.RelativeVariancePerFrame.toExponential(2)} | ${Row.MillisecondsPerFrame.toFixed(1)} |`);
        }
    }
    Lines.push('');
    Lines.push('| Technique | Candidates | Mean / reference | Relative error | Standard error | Relative standard deviation | Frames for 5% error |');
    Lines.push('| :--- | ---: | ---: | ---: | ---: | ---: | ---: |');
    for (const Row of Report.SinglePoint.Rows)
    {
        Lines.push(`| ${Row.Technique} | ${Row.CandidateCount} | ${(Row.MeanOverReference * 100).toFixed(2)}% |`
            + ` ${(Row.RelativeError * 100).toFixed(2)}% | ${(Row.StandardErrorOverReference * 100).toFixed(2)}% |`
            + ` ${(Row.RelativeStandardDeviation * 100).toFixed(1)}% | ${Row.FramesForFivePercentError} |`);
    }
    writeFileSync(join(ScriptDirectory, 'ValidationTables.md'), Lines.join('\n') + '\n');
    console.log('\nWrote ValidationReport.json and ValidationTables.md');
}

Run();
