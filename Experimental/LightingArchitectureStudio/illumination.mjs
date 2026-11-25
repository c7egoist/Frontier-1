/*============================================================================================================================================
                                                    ILLUMINATION.MJS
============================================================================================================================================*/
// Shared illumination core for the lighting architecture studio: analytic studio scenes, a photometric luminaire set,
// a binary light tree carrying orientation cones, hierarchical importance sampling (stochastic cut descent), deterministic
// polygon irradiance, and a signed-distance visibility tier. Imported by studio.html and by validate.mjs so the browser and
// the headless validation execute byte-identical math.

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

export const LightKind = Object.freeze({
    Rect:   0,
    Disc:   1,
    Sphere: 2,
    Spot:   3,
    Sun:    4,
});

export const Technique = Object.freeze({
    UniformLightPick:   'UniformLightPick',
    LightTreeCut:       'LightTreeCut',
    SharedLightTreeCut: 'SharedLightTreeCut',
    DeterministicPolygon: 'DeterministicPolygon',
});

export const Representation = Object.freeze({
    AnalyticPrism:  'AnalyticPrism',
    SignedDistance: 'SignedDistance',
});

const Pi              = Math.PI;
const TwoPi           = 2.0 * Math.PI;
const MaximumFloat    = 1.0e30;
const ImportanceFloor = 1.0e-9;   // [lin] - Keeps every tree branch reachable, so the source density stays strictly positive
const SurfaceBias     = 1.5e-3;   // [m]   - Shadow-ray origin offset along the shading normal
const DeterministicConeSpread = 0.008; // [rad] - Thin cone for the deterministic tier, whose area coverage comes from its grid
const FireflyClampLuminance = 12.0; // [lin] - Bounded clamp on a single resampled contribution (see README firefly note)

//------------------------------------------------------------------------------------------------------------------------
//                                                      SCALAR HELPERS
//------------------------------------------------------------------------------------------------------------------------

export function Clamp(ScalarReading, LowBound, HighBound)
{
    return ScalarReading < LowBound ? LowBound : (ScalarReading > HighBound ? HighBound : ScalarReading);
}

export function Luminance(Rgb)
{
    return 0.2126 * Rgb[0] + 0.7152 * Rgb[1] + 0.0722 * Rgb[2];
}

export function ConstructXorShiftSeed(Seed)
{
    let ScalarReading = (Seed | 0) === 0 ? 0x9E3779B9 : (Seed | 0) >>> 0;
    return function DrawRandomBit()
    {
        ScalarReading ^= ScalarReading << 13; ScalarReading >>>= 0;
        ScalarReading ^= ScalarReading >>> 17;
        ScalarReading ^= ScalarReading << 5;  ScalarReading >>>= 0;
        return ScalarReading / 4294967296.0;
    };
}

export function SubtractVector(LeftVector, RightVector)
{
    return [ LeftVector[0] - RightVector[0], LeftVector[1] - RightVector[1], LeftVector[2] - RightVector[2] ];
}

export function ScaleVector(SourceVector, ScalarFactor)
{
    return [ SourceVector[0] * ScalarFactor, SourceVector[1] * ScalarFactor, SourceVector[2] * ScalarFactor ];
}

export function DotProduct(LeftVector, RightVector)
{
    return LeftVector[0] * RightVector[0] + LeftVector[1] * RightVector[1] + LeftVector[2] * RightVector[2];
}

export function CrossProduct(LeftVector, RightVector)
{
    return [
        LeftVector[1] * RightVector[2] - LeftVector[2] * RightVector[1],
        LeftVector[2] * RightVector[0] - LeftVector[0] * RightVector[2],
        LeftVector[0] * RightVector[1] - LeftVector[1] * RightVector[0],
    ];
}

export function VectorLength(SourceVector)
{
    return Math.sqrt(DotProduct(SourceVector, SourceVector));
}

export function NormalizeVector(SourceVector)
{
    const Length = VectorLength(SourceVector) || 1.0e-12;
    return [ SourceVector[0] / Length, SourceVector[1] / Length, SourceVector[2] / Length ];
}

function ReflectVector(IncidentVector, SurfaceNormal)
{
    const Projection = 2.0 * DotProduct(IncidentVector, SurfaceNormal);
    return [
        IncidentVector[0] - Projection * SurfaceNormal[0],
        IncidentVector[1] - Projection * SurfaceNormal[1],
        IncidentVector[2] - Projection * SurfaceNormal[2],
    ];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

function ConstructMaterial(Albedo, Roughness, Metallic, Emission)
{
    const EmissionRadiance = Emission || [ 0.0, 0.0, 0.0 ];
    return {
        Albedo: Albedo, Roughness: Roughness, Metallic: Metallic, Emission: EmissionRadiance,
        Emissive: Luminance(EmissionRadiance) > 0.0,
    };
}

function ConstructPrism(LowerBounds, UpperBounds, Material)
{
    return { LowerBounds: LowerBounds, UpperBounds: UpperBounds, Material: Material, PrimitiveKind: 'Prism' };
}

function ConstructOrb(Center, Radius, Material)
{
    return { Center: Center, Radius: Radius, Material: Material, PrimitiveKind: 'Orb' };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LUMINAIRE CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------
// Every luminaire exposes Power (luminance weighted flux), a bounding cone for the light tree, a MeasureKind telling the
// sampler whether it integrates over area or over a discrete point mass, and a MeasureSize used to normalise proposals.

function ConstructRectLuminaire(Center, HalfAxisU, HalfAxisV, Radiance)
{
    const Area   = 4.0 * VectorLength(HalfAxisU) * VectorLength(HalfAxisV);
    const Normal = NormalizeVector(CrossProduct(HalfAxisU, HalfAxisV));
    return {
        Kind: LightKind.Rect, Center: Center, HalfAxisU: HalfAxisU, HalfAxisV: HalfAxisV, Normal: Normal,
        Radiance: Radiance, Area: Area, MeasureSize: Area, Power: Luminance(Radiance) * Area * Pi,
        ConeAxis: Normal, ConeSpread: Pi * 0.5, MeasureKind: 'Area',
    };
}

function ConstructDiscLuminaire(Center, Normal, Radius, Radiance)
{
    const UnitNormal = NormalizeVector(Normal);
    const Area       = Pi * Radius * Radius;
    return {
        Kind: LightKind.Disc, Center: Center, Normal: UnitNormal, Radius: Radius, Radiance: Radiance,
        Area: Area, MeasureSize: Area, Power: Luminance(Radiance) * Area * Pi,
        ConeAxis: UnitNormal, ConeSpread: Pi * 0.5, MeasureKind: 'Area',
    };
}

function ConstructOrbLuminaire(Center, Radius, Radiance)
{
    const Area = 4.0 * Pi * Radius * Radius;
    return {
        Kind: LightKind.Sphere, Center: Center, Radius: Radius, Radiance: Radiance, Area: Area, MeasureSize: Area,
        Power: Luminance(Radiance) * Area * Pi, ConeAxis: [ 0.0, 1.0, 0.0 ], ConeSpread: Pi, MeasureKind: 'Area',
    };
}

function ConstructUnitarySolidAngle(CutoffHalfAngle)
{
    return 2.0 * Pi * (1.0 - Math.cos(Math.min(CutoffHalfAngle, Pi)));
}

function ConstructPracticalLuminaire(Center, Axis, Profile, PeakIntensity, CutoffHalfAngle)
{
    const UnitAxis = NormalizeVector(Axis);
    return {
        Kind: LightKind.Spot, Center: Center, Axis: UnitAxis, Profile: Profile,
        PeakIntensity: PeakIntensity, CutoffHalfAngle: CutoffHalfAngle, MeasureSize: 1.0,
        Power: Luminance(PeakIntensity) * ConstructUnitarySolidAngle(CutoffHalfAngle),
        ConeAxis: UnitAxis, ConeSpread: CutoffHalfAngle, MeasureKind: 'Discrete',
    };
}

function ConstructSolarLuminaire(DirectionTowardSun, Irradiance)
{
    const UnitDirection = NormalizeVector(DirectionTowardSun);
    return {
        Kind: LightKind.Sun, Direction: UnitDirection, PeakIntensity: Irradiance, MeasureSize: 1.0,
        Power: Luminance(Irradiance) * 1.0e4, ConeAxis: UnitDirection, ConeSpread: 0.00465, MeasureKind: 'Discrete',
        Center: ScaleVector(UnitDirection, 4.0e3),
    };
}

// A 64-bin resampled photometric distribution stands in for a measured .ies profile.
export function ConstructPhotometricProfile(PeakHalfAngle, ShoulderRatio)
{
    const BinCount = 64;
    const Profile  = new Float32Array(BinCount);
    for (let BinIndex = 0; BinIndex < BinCount; BinIndex += 1)
    {
        const ThetaRatio = (BinIndex + 0.5) / BinCount;
        const Shoulder   = PeakHalfAngle * (0.35 + 0.65 * ShoulderRatio);
        const Falloff    = Math.exp(-Math.pow(ThetaRatio / Math.max(Shoulder, 0.02), 2.2) * 2.4);
        Profile[BinIndex] = Math.max(0.0, Falloff * (1.0 - 0.15 * ThetaRatio));
    }
    return Profile;
}

function SampleProfileRadiance(Luminaire, CosineTheta)
{
    const Profile  = Luminaire.Profile;
    const BinCount = Profile.length;
    const Theta    = Math.acos(Clamp(CosineTheta, -1.0, 1.0));
    if (Theta >= Luminaire.CutoffHalfAngle)
    {
        return 0.0;
    }
    const Position = (Theta / Luminaire.CutoffHalfAngle) * (BinCount - 1);
    const Lower    = Math.floor(Position);
    const Upper    = Math.min(BinCount - 1, Lower + 1);
    const Fraction = Position - Lower;
    return Profile[Lower] * (1.0 - Fraction) + Profile[Upper] * Fraction;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       STUDIO SCENES
//------------------------------------------------------------------------------------------------------------------------

function ConstructAtriumDaylightStudio(LampScale)
{
    const Plaster  = ConstructMaterial([ 0.72, 0.70, 0.67 ], 0.62, 0.0);
    const Stone    = ConstructMaterial([ 0.48, 0.46, 0.44 ], 0.48, 0.0);
    const Oak      = ConstructMaterial([ 0.42, 0.28, 0.17 ], 0.42, 0.0);
    const Steel    = ConstructMaterial([ 0.62, 0.63, 0.66 ], 0.24, 1.0);
    const Glow     = ConstructMaterial([ 0.0, 0.0, 0.0 ], 1.0, 0.0, [ 3.1, 2.85, 2.55 ]);
    const SkyPanel = ConstructMaterial([ 0.0, 0.0, 0.0 ], 1.0, 0.0, [ 4.6, 6.2, 8.6 ]);

    const Prisms = [
        ConstructPrism([ -14.0, -0.25, -11.0 ], [ 14.0, 0.0, 11.0 ], Stone),
        ConstructPrism([ -14.0, 7.2, -11.0 ], [ 14.0, 7.5, 11.0 ], Plaster),
        ConstructPrism([ -14.0, 0.0, -11.0 ], [ -13.7, 7.2, 11.0 ], Plaster),
        ConstructPrism([ 13.7, 0.0, -11.0 ], [ 14.0, 7.2, 11.0 ], Plaster),
        ConstructPrism([ -14.0, 0.0, -11.0 ], [ 14.0, 7.2, -10.7 ], Plaster),
        ConstructPrism([ -14.0, 0.0, 10.7 ], [ 14.0, 7.2, 11.0 ], Plaster),
        ConstructPrism([ -8.0, 0.0, -6.0 ], [ -6.6, 7.2, -4.6 ], Plaster),
        ConstructPrism([ 6.6, 0.0, -6.0 ], [ 8.0, 7.2, -4.6 ], Plaster),
        ConstructPrism([ -8.0, 0.0, 4.6 ], [ -6.6, 7.2, 6.0 ], Plaster),
        ConstructPrism([ 6.6, 0.0, 4.6 ], [ 8.0, 7.2, 6.0 ], Plaster),
        ConstructPrism([ -2.6, 0.0, -1.4 ], [ 2.6, 1.05, 0.6 ], Oak),
        ConstructPrism([ -2.7, 1.05, -1.5 ], [ 2.7, 1.12, 0.7 ], Steel),
        ConstructPrism([ -9.5, 0.0, 7.4 ], [ -3.5, 0.9, 8.2 ], Oak),
        ConstructPrism([ 3.5, 0.0, 7.4 ], [ 9.5, 0.9, 8.2 ], Oak),
        ConstructPrism([ -13.9, 4.6, -3.0 ], [ -13.75, 5.0, 3.0 ], Glow),
        ConstructPrism([ 13.75, 4.6, -3.0 ], [ 13.9, 5.0, 3.0 ], Glow),
        ConstructPrism([ -4.0, 7.14, -2.0 ], [ 4.0, 7.2, 2.0 ], SkyPanel),
    ];

    const Orbs = [ ConstructOrb([ 0.0, 1.75, -4.2 ], 0.62, Steel) ];

    const Lights = [
        ConstructSolarLuminaire([ -0.42, 0.78, 0.46 ], [ 7.4, 7.1, 6.6 ]),
        ConstructRectLuminaire([ 0.0, 7.16, 0.0 ], [ 4.0, 0.0, 0.0 ], [ 0.0, 0.0, 2.0 ], [ 4.6, 6.2, 8.6 ]),
        ConstructRectLuminaire([ -13.82, 4.8, 0.0 ], [ 0.0, 0.0, 3.0 ], [ 0.0, 0.2, 0.0 ], [ 3.1, 2.85, 2.55 ]),
        ConstructRectLuminaire([ 13.82, 4.8, 0.0 ], [ 0.0, 0.0, 3.0 ], [ 0.0, 0.2, 0.0 ], [ 3.1, 2.85, 2.55 ]),
    ];

    const CoveProfile = ConstructPhotometricProfile(0.42, 0.55);
    const DownlightCount = Math.max(4, Math.round(16 * LampScale));
    const GridSide = Math.ceil(Math.sqrt(DownlightCount));
    for (let LampIndex = 0; LampIndex < DownlightCount; LampIndex += 1)
    {
        const PositionX = -9.0 + 18.0 * ((LampIndex % GridSide) / Math.max(1, GridSide - 1));
        const PositionZ = -7.0 + 14.0 * (Math.floor(LampIndex / GridSide) / Math.max(1, GridSide - 1));
        Lights.push(ConstructPracticalLuminaire([ PositionX, 7.1, PositionZ ], [ 0.0, -1.0, 0.0 ], CoveProfile, [ 34.0, 31.0, 26.0 ], 1.05));
    }

    return {
        Prisms: Prisms,
        Orbs: Orbs,
        Lights: Lights,
        Environment: { Zenith: [ 0.30, 0.46, 0.78 ], Horizon: [ 0.44, 0.49, 0.55 ], Ground: [ 0.16, 0.14, 0.12 ] },
        Camera: { Eye: [ 0.0, 2.35, 12.6 ], Target: [ 0.0, 2.0, -1.0 ], FieldOfView: 46.0 },
    };
}

function ConstructNightArrivalStudio(LampScale)
{
    const Concrete = ConstructMaterial([ 0.20, 0.21, 0.23 ], 0.55, 0.0);
    const Asphalt  = ConstructMaterial([ 0.11, 0.11, 0.12 ], 0.68, 0.0);
    const Paint    = ConstructMaterial([ 0.58, 0.58, 0.60 ], 0.35, 0.35);
    const Strip    = ConstructMaterial([ 0.0, 0.0, 0.0 ], 1.0, 0.0, [ 1.5, 2.4, 3.4 ]);

    const Prisms = [
        ConstructPrism([ -20.0, -0.3, -16.0 ], [ 20.0, 0.0, 16.0 ], Asphalt),
        ConstructPrism([ -20.0, 4.4, -16.0 ], [ 20.0, 4.7, 16.0 ], Concrete),
        ConstructPrism([ -20.0, 0.0, -16.0 ], [ -19.7, 4.4, 16.0 ], Concrete),
        ConstructPrism([ 19.7, 0.0, -16.0 ], [ 20.0, 4.4, 16.0 ], Concrete),
        ConstructPrism([ -20.0, 0.0, -16.0 ], [ 20.0, 4.4, -15.7 ], Concrete),
        ConstructPrism([ -6.0, 0.0, -6.0 ], [ -4.4, 4.4, -4.4 ], Concrete),
        ConstructPrism([ 4.4, 0.0, -6.0 ], [ 6.0, 4.4, -4.4 ], Concrete),
        ConstructPrism([ -6.0, 0.0, 4.4 ], [ -4.4, 4.4, 6.0 ], Concrete),
        ConstructPrism([ 4.4, 0.0, 4.4 ], [ 6.0, 4.4, 6.0 ], Concrete),
        ConstructPrism([ -1.2, 0.0, -12.0 ], [ 1.2, 0.85, -9.6 ], Paint),
        ConstructPrism([ -8.4, 0.0, -1.0 ], [ -5.6, 0.62, 1.0 ], Paint),
        ConstructPrism([ 5.6, 0.0, -1.0 ], [ 8.4, 0.62, 1.0 ], Paint),
        ConstructPrism([ -13.0, 0.0, 13.2 ], [ 13.0, 0.42, 13.6 ], Paint),
        ConstructPrism([ -19.9, 3.9, -14.0 ], [ -19.75, 4.1, 14.0 ], Strip),
        ConstructPrism([ 19.75, 3.9, -14.0 ], [ 19.9, 4.1, 14.0 ], Strip),
    ];

    const Orbs = [ ConstructOrb([ 0.0, 1.5, 0.0 ], 1.35, Paint) ];

    const Lights = [
        ConstructRectLuminaire([ -19.82, 4.0, 0.0 ], [ 0.0, 0.0, 14.0 ], [ 0.0, 0.1, 0.0 ], [ 1.5, 2.4, 3.4 ]),
        ConstructRectLuminaire([ 19.82, 4.0, 0.0 ], [ 0.0, 0.0, 14.0 ], [ 0.0, 0.1, 0.0 ], [ 1.5, 2.4, 3.4 ]),
        ConstructRectLuminaire([ 0.0, 4.34, 12.0 ], [ 6.0, 0.0, 0.0 ], [ 0.0, 0.0, 1.6 ], [ 2.6, 2.35, 1.9 ]),
        ConstructRectLuminaire([ 0.0, 4.34, -12.0 ], [ 6.0, 0.0, 0.0 ], [ 0.0, 0.0, 1.6 ], [ 2.2, 2.0, 1.7 ]),
    ];

    const DownlightProfile = ConstructPhotometricProfile(0.36, 0.42);
    const BayLampCount = Math.max(8, Math.round(40 * LampScale));
    const BaySide = Math.ceil(Math.sqrt(BayLampCount));
    for (let LampIndex = 0; LampIndex < BayLampCount; LampIndex += 1)
    {
        const PositionX = -17.0 + 34.0 * ((LampIndex % BaySide) / Math.max(1, BaySide - 1));
        const PositionZ = -13.0 + 26.0 * (Math.floor(LampIndex / BaySide) / Math.max(1, BaySide - 1));
        Lights.push(ConstructPracticalLuminaire([ PositionX, 4.3, PositionZ ], [ 0.0, -1.0, 0.0 ], DownlightProfile, [ 22.0, 21.0, 18.5 ], 0.95));
    }

    const BeamProfile = ConstructPhotometricProfile(0.22, 0.30);
    const VehicleLampCount = Math.max(4, Math.round(12 * LampScale));
    for (let LampIndex = 0; LampIndex < VehicleLampCount; LampIndex += 1)
    {
        const OffsetX = -10.0 + 20.0 * (LampIndex / Math.max(1, VehicleLampCount - 1));
        Lights.push(ConstructPracticalLuminaire([ OffsetX, 0.62, -6.4 ], [ OffsetX < 0.0 ? 0.10 : -0.10, -0.16, -1.0 ], BeamProfile, [ 90.0, 86.0, 70.0 ], 0.85));
    }

    Lights.push(ConstructSolarLuminaire([ 0.30, 0.62, -0.72 ], [ 0.045, 0.040, 0.075 ]));

    return {
        Prisms: Prisms,
        Orbs: Orbs,
        Lights: Lights,
        Environment: { Zenith: [ 0.012, 0.018, 0.036 ], Horizon: [ 0.020, 0.026, 0.044 ], Ground: [ 0.010, 0.010, 0.011 ] },
        Camera: { Eye: [ 1.5, 2.1, 14.0 ], Target: [ 0.0, 1.6, -2.0 ], FieldOfView: 48.0 },
    };
}

function ConstructGalleryMixedStudio(LampScale)
{
    const Gallery = ConstructMaterial([ 0.78, 0.77, 0.74 ], 0.72, 0.0);
    const Floor   = ConstructMaterial([ 0.24, 0.23, 0.22 ], 0.30, 0.0);
    const Brushed = ConstructMaterial([ 0.68, 0.52, 0.24 ], 0.22, 1.0);
    const Canvas  = ConstructMaterial([ 0.30, 0.26, 0.22 ], 0.66, 0.0);
    const Warm    = ConstructMaterial([ 0.0, 0.0, 0.0 ], 1.0, 0.0, [ 3.6, 2.6, 1.5 ]);
    const Cool    = ConstructMaterial([ 0.0, 0.0, 0.0 ], 1.0, 0.0, [ 1.7, 2.4, 3.2 ]);

    const Prisms = [
        ConstructPrism([ -10.0, -0.2, -14.0 ], [ 10.0, 0.0, 14.0 ], Floor),
        ConstructPrism([ -10.0, 6.0, -14.0 ], [ 10.0, 6.2, 14.0 ], Gallery),
        ConstructPrism([ -10.0, 0.0, -14.0 ], [ -9.8, 6.0, 14.0 ], Gallery),
        ConstructPrism([ 9.8, 0.0, -14.0 ], [ 10.0, 6.0, 14.0 ], Gallery),
        ConstructPrism([ -10.0, 0.0, -14.0 ], [ 10.0, 6.0, -13.8 ], Gallery),
        ConstructPrism([ -10.0, 0.0, 13.8 ], [ 10.0, 6.0, 14.0 ], Gallery),
        ConstructPrism([ -9.72, 1.4, -9.0 ], [ -9.68, 3.4, -3.0 ], Canvas),
        ConstructPrism([ 9.68, 1.4, -9.0 ], [ 9.72, 3.4, -3.0 ], Canvas),
        ConstructPrism([ -4.0, 1.5, -13.62 ], [ 4.0, 3.6, -13.58 ], Canvas),
        ConstructPrism([ -0.9, 0.0, -0.9 ], [ 0.9, 1.1, 0.9 ], Brushed),
        ConstructPrism([ -1.1, 1.1, -1.1 ], [ 1.1, 1.2, 1.1 ], Brushed),
        ConstructPrism([ -6.6, 2.1, 3.4 ], [ -2.4, 2.3, 4.0 ], Warm),
        ConstructPrism([ 2.4, 2.1, 3.4 ], [ 6.6, 2.3, 4.0 ], Cool),
        ConstructPrism([ -3.0, 6.05, -6.0 ], [ 3.0, 6.2, 0.0 ], Cool),
    ];

    const Orbs = [
        ConstructOrb([ -6.0, 1.65, -1.0 ], 0.75, Brushed),
        ConstructOrb([ 6.0, 1.35, -1.0 ], 0.55, Brushed),
    ];

    const Lights = [
        ConstructSolarLuminaire([ -0.30, 0.86, 0.41 ], [ 2.2, 2.05, 1.85 ]),
        ConstructRectLuminaire([ 0.0, 6.13, -3.0 ], [ 3.0, 0.0, 0.0 ], [ 0.0, 0.0, 3.0 ], [ 1.7, 2.4, 3.2 ]),
        ConstructRectLuminaire([ -4.5, 2.2, 3.7 ], [ 2.1, 0.0, 0.0 ], [ 0.0, 0.1, 0.3 ], [ 3.6, 2.6, 1.5 ]),
        ConstructRectLuminaire([ 4.5, 2.2, 3.7 ], [ 2.1, 0.0, 0.0 ], [ 0.0, 0.1, 0.3 ], [ 1.7, 2.4, 3.2 ]),
        ConstructDiscLuminaire([ 0.0, 5.7, 6.0 ], [ 0.0, -1.0, 0.0 ], 1.4, [ 5.4, 4.9, 4.1 ]),
        ConstructOrbLuminaire([ 0.0, 2.9, 6.6 ], 0.42, [ 2.4, 2.2, 1.9 ]),
    ];

    const AccentProfile = ConstructPhotometricProfile(0.20, 0.26);
    const AccentCount = Math.max(8, Math.round(40 * LampScale));
    for (let LampIndex = 0; LampIndex < AccentCount; LampIndex += 1)
    {
        const RowIndex = Math.floor(LampIndex / 10);
        const Along = -10.0 + 20.0 * ((LampIndex % 10) / 9.0);
        const Track = RowIndex % 2 === 0 ? -12.0 : 12.0;
        Lights.push(ConstructPracticalLuminaire([ Along, 5.6, Track ], [ 0.0, -0.62, Track < 0.0 ? 1.0 : -1.0 ], AccentProfile, [ 44.0, 40.0, 34.0 ], 0.75));
    }

    const WashProfile = ConstructPhotometricProfile(0.52, 0.72);
    const WashCount = Math.max(8, Math.round(32 * LampScale));
    for (let LampIndex = 0; LampIndex < WashCount; LampIndex += 1)
    {
        const RowIndex = Math.floor(LampIndex / 8);
        const Along = -8.0 + 16.0 * ((LampIndex % 8) / 7.0);
        const Track = RowIndex % 2 === 0 ? -10.6 : 10.6;
        Lights.push(ConstructPracticalLuminaire([ Along, 5.2, Track ], [ 0.0, -0.94, Track < 0.0 ? 0.34 : -0.34 ], WashProfile, [ 26.0, 25.0, 23.0 ], 1.15));
    }

    return {
        Prisms: Prisms,
        Orbs: Orbs,
        Lights: Lights,
        Environment: { Zenith: [ 0.05, 0.06, 0.09 ], Horizon: [ 0.10, 0.10, 0.11 ], Ground: [ 0.05, 0.048, 0.045 ] },
        Camera: { Eye: [ 0.0, 2.6, 12.0 ], Target: [ 0.0, 2.0, 0.0 ], FieldOfView: 50.0 },
    };
}

export const StudioPresets = Object.freeze([
    { Identifier: 'AtriumDaylight', Caption: 'Lobby 1 - Atrium Daylight', Construct: ConstructAtriumDaylightStudio },
    { Identifier: 'NightArrival',   Caption: 'Lobby 2 - Night Arrival',   Construct: ConstructNightArrivalStudio },
    { Identifier: 'GalleryMixed',   Caption: 'Lobby 3 - Gallery Mixed',   Construct: ConstructGalleryMixedStudio },
]);

//------------------------------------------------------------------------------------------------------------------------
//                                              ENVIRONMENT HARMONICS
//------------------------------------------------------------------------------------------------------------------------

export function EvaluateEnvironmentRgb(Environment, Direction)
{
    const Upward = Clamp(Direction[1], -1.0, 1.0);
    if (Upward >= 0.0)
    {
        const Blend = Math.pow(Upward, 0.55);
        return [
            Environment.Horizon[0] + (Environment.Zenith[0] - Environment.Horizon[0]) * Blend,
            Environment.Horizon[1] + (Environment.Zenith[1] - Environment.Horizon[1]) * Blend,
            Environment.Horizon[2] + (Environment.Zenith[2] - Environment.Horizon[2]) * Blend,
        ];
    }
    const Blend = Math.pow(-Upward, 0.7);
    return [
        Environment.Horizon[0] + (Environment.Ground[0] - Environment.Horizon[0]) * Blend,
        Environment.Horizon[1] + (Environment.Ground[1] - Environment.Horizon[1]) * Blend,
        Environment.Horizon[2] + (Environment.Ground[2] - Environment.Horizon[2]) * Blend,
    ];
}

function ConstructEnvironmentHarmonics(Environment)
{
    const HarmonicCount = 9;
    const Radiance = new Float64Array(HarmonicCount * 3);
    const SampleCount = 4096;
    const Draw = ConstructXorShiftSeed(0x51ED270B);
    for (let SampleIndex = 0; SampleIndex < SampleCount; SampleIndex += 1)
    {
        const Z = 2.0 * Draw() - 1.0;
        const Azimuth = TwoPi * Draw();
        const Radial = Math.sqrt(Math.max(0.0, 1.0 - Z * Z));
        const Direction = [ Radial * Math.cos(Azimuth), Z, Radial * Math.sin(Azimuth) ];
        const Rgb = EvaluateEnvironmentRgb(Environment, Direction);
        const Basis = [
            0.282095,
            0.488603 * Direction[1], 0.488603 * Direction[2], 0.488603 * Direction[0],
            1.092548 * Direction[0] * Direction[1], 1.092548 * Direction[1] * Direction[2],
            0.315392 * (3.0 * Direction[2] * Direction[2] - 1.0),
            1.092548 * Direction[0] * Direction[2],
            0.546274 * (Direction[0] * Direction[0] - Direction[1] * Direction[1]),
        ];
        for (let HarmonicIndex = 0; HarmonicIndex < HarmonicCount; HarmonicIndex += 1)
        {
            Radiance[HarmonicIndex * 3 + 0] += Rgb[0] * Basis[HarmonicIndex];
            Radiance[HarmonicIndex * 3 + 1] += Rgb[1] * Basis[HarmonicIndex];
            Radiance[HarmonicIndex * 3 + 2] += Rgb[2] * Basis[HarmonicIndex];
        }
    }
    const SolidAngleWeight = (4.0 * Pi) / SampleCount;
    for (let Channel = 0; Channel < HarmonicCount * 3; Channel += 1)
    {
        Radiance[Channel] *= SolidAngleWeight;
    }
    return { Coefficients: Radiance };
}

export function EvaluateIrradianceHarmonics(Harmonics, SurfaceNormal)
{
    const Projected = [
        3.141593,
        2.094395 * SurfaceNormal[1], 2.094395 * SurfaceNormal[2], 2.094395 * SurfaceNormal[0],
        0.785398 * SurfaceNormal[0] * SurfaceNormal[1], 0.785398 * SurfaceNormal[1] * SurfaceNormal[2],
        0.785398 * (3.0 * SurfaceNormal[2] * SurfaceNormal[2] - 1.0),
        0.785398 * SurfaceNormal[0] * SurfaceNormal[2],
        0.392699 * (SurfaceNormal[0] * SurfaceNormal[0] - SurfaceNormal[1] * SurfaceNormal[1]),
    ];
    const Coefficients = Harmonics.Coefficients;
    const Result = [ 0.0, 0.0, 0.0 ];
    for (let HarmonicIndex = 0; HarmonicIndex < 9; HarmonicIndex += 1)
    {
        Result[0] += Coefficients[HarmonicIndex * 3 + 0] * Projected[HarmonicIndex];
        Result[1] += Coefficients[HarmonicIndex * 3 + 1] * Projected[HarmonicIndex];
        Result[2] += Coefficients[HarmonicIndex * 3 + 2] * Projected[HarmonicIndex];
    }
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LIGHT TREE CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

function ConstructLightBounds(Luminaire)
{
    if (Luminaire.Kind === LightKind.Rect)
    {
        const Extent = [
            Math.abs(Luminaire.HalfAxisU[0]) + Math.abs(Luminaire.HalfAxisV[0]),
            Math.abs(Luminaire.HalfAxisU[1]) + Math.abs(Luminaire.HalfAxisV[1]),
            Math.abs(Luminaire.HalfAxisU[2]) + Math.abs(Luminaire.HalfAxisV[2]),
        ];
        return {
            Lower: [ Luminaire.Center[0] - Extent[0], Luminaire.Center[1] - Extent[1], Luminaire.Center[2] - Extent[2] ],
            Upper: [ Luminaire.Center[0] + Extent[0], Luminaire.Center[1] + Extent[1], Luminaire.Center[2] + Extent[2] ],
        };
    }
    if (Luminaire.Kind === LightKind.Disc)
    {
        const Radius = Luminaire.Radius;
        return {
            Lower: [ Luminaire.Center[0] - Radius, Luminaire.Center[1] - Radius, Luminaire.Center[2] - Radius ],
            Upper: [ Luminaire.Center[0] + Radius, Luminaire.Center[1] + Radius, Luminaire.Center[2] + Radius ],
        };
    }
    if (Luminaire.Kind === LightKind.Sphere)
    {
        const Radius = Luminaire.Radius;
        return {
            Lower: [ Luminaire.Center[0] - Radius, Luminaire.Center[1] - Radius, Luminaire.Center[2] - Radius ],
            Upper: [ Luminaire.Center[0] + Radius, Luminaire.Center[1] + Radius, Luminaire.Center[2] + Radius ],
        };
    }
    if (Luminaire.Kind === LightKind.Sun)
    {
        return { Lower: [ -1.0e4, -1.0e4, -1.0e4 ], Upper: [ 1.0e4, 1.0e4, 1.0e4 ] };
    }
    const Padding = 0.04;
    return {
        Lower: [ Luminaire.Center[0] - Padding, Luminaire.Center[1] - Padding, Luminaire.Center[2] - Padding ],
        Upper: [ Luminaire.Center[0] + Padding, Luminaire.Center[1] + Padding, Luminaire.Center[2] + Padding ],
    };
}

function MergeOrientationCones(AxisLeft, SpreadLeft, AxisRight, SpreadRight)
{
    const CosineAngle = Clamp(DotProduct(AxisLeft, AxisRight), -1.0, 1.0);
    const Angle = Math.acos(CosineAngle);
    if (Angle + SpreadRight <= SpreadLeft)
    {
        return { Axis: AxisLeft, Spread: SpreadLeft };
    }
    if (Angle + SpreadLeft <= SpreadRight)
    {
        return { Axis: AxisRight, Spread: SpreadRight };
    }
    const MergedSpread = 0.5 * (Angle + SpreadLeft + SpreadRight);
    if (Angle < 1.0e-5)
    {
        return { Axis: AxisLeft, Spread: MergedSpread };
    }
    const Interpolation = Clamp((MergedSpread - SpreadLeft) / Angle, 0.0, 1.0);
    const Axis = NormalizeVector([
        AxisLeft[0] + (AxisRight[0] - AxisLeft[0]) * Interpolation,
        AxisLeft[1] + (AxisRight[1] - AxisLeft[1]) * Interpolation,
        AxisLeft[2] + (AxisRight[2] - AxisLeft[2]) * Interpolation,
    ]);
    return { Axis: Axis, Spread: Math.min(MergedSpread, Pi) };
}

// Array-of-structure node layout mirrors the 32 B GPU register pack used by PhotometricLightTree: bounds, cone, power, links.
export function ConstructLightTree(Luminaires)
{
    const LeafCount = Math.max(1, Luminaires.length);
    const Capacity  = LeafCount * 2 - 1;
    const Tree = {
        LowerBounds: new Float64Array(Capacity * 3),
        UpperBounds: new Float64Array(Capacity * 3),
        TotalPower:  new Float64Array(Capacity),
        ConeAxis:    new Float64Array(Capacity * 3),
        ConeSpread:  new Float64Array(Capacity),
        LeftIndex:   new Int32Array(Capacity).fill(-1),
        RightIndex:  new Int32Array(Capacity).fill(-1),
        LeafLuminaire: new Int32Array(Capacity).fill(-1),
        Luminaires: Luminaires,
        RootIndex: 0,
        NodeCount: 0,
        LeafStride: LeafCount,
    };

    const CentroidX = new Float64Array(LeafCount);
    const CentroidY = new Float64Array(LeafCount);
    const CentroidZ = new Float64Array(LeafCount);
    const Order = new Int32Array(LeafCount);

    for (let LeafIndex = 0; LeafIndex < LeafCount; LeafIndex += 1)
    {
        const Luminaire = Luminaires[LeafIndex];
        const Bounds = ConstructLightBounds(Luminaire);
        Tree.LowerBounds[LeafIndex * 3 + 0] = Bounds.Lower[0];
        Tree.LowerBounds[LeafIndex * 3 + 1] = Bounds.Lower[1];
        Tree.LowerBounds[LeafIndex * 3 + 2] = Bounds.Lower[2];
        Tree.UpperBounds[LeafIndex * 3 + 0] = Bounds.Upper[0];
        Tree.UpperBounds[LeafIndex * 3 + 1] = Bounds.Upper[1];
        Tree.UpperBounds[LeafIndex * 3 + 2] = Bounds.Upper[2];
        Tree.TotalPower[LeafIndex] = Math.max(Luminaire.Power, 1.0e-12);
        Tree.ConeAxis[LeafIndex * 3 + 0] = Luminaire.ConeAxis[0];
        Tree.ConeAxis[LeafIndex * 3 + 1] = Luminaire.ConeAxis[1];
        Tree.ConeAxis[LeafIndex * 3 + 2] = Luminaire.ConeAxis[2];
        Tree.ConeSpread[LeafIndex] = Luminaire.ConeSpread;
        Tree.LeafLuminaire[LeafIndex] = LeafIndex;
        CentroidX[LeafIndex] = 0.5 * (Bounds.Lower[0] + Bounds.Upper[0]);
        CentroidY[LeafIndex] = 0.5 * (Bounds.Lower[1] + Bounds.Upper[1]);
        CentroidZ[LeafIndex] = 0.5 * (Bounds.Lower[2] + Bounds.Upper[2]);
        Order[LeafIndex] = LeafIndex;
    }

    let NextFreeIndex = LeafCount;

    const BuildRange = (RangeStart, RangeEnd) =>
    {
        if (RangeStart === RangeEnd)
        {
            return Order[RangeStart];
        }

        let MinimumX = MaximumFloat, MinimumY = MaximumFloat, MinimumZ = MaximumFloat;
        let MaximumX = -MaximumFloat, MaximumY = -MaximumFloat, MaximumZ = -MaximumFloat;
        for (let EntryIndex = RangeStart; EntryIndex <= RangeEnd; EntryIndex += 1)
        {
            const Leaf = Order[EntryIndex];
            MinimumX = Math.min(MinimumX, CentroidX[Leaf]); MaximumX = Math.max(MaximumX, CentroidX[Leaf]);
            MinimumY = Math.min(MinimumY, CentroidY[Leaf]); MaximumY = Math.max(MaximumY, CentroidY[Leaf]);
            MinimumZ = Math.min(MinimumZ, CentroidZ[Leaf]); MaximumZ = Math.max(MaximumZ, CentroidZ[Leaf]);
        }
        const SpanX = MaximumX - MinimumX;
        const SpanY = MaximumY - MinimumY;
        const SpanZ = MaximumZ - MinimumZ;
        const SplitAxis = (SpanX >= SpanY && SpanX >= SpanZ) ? 0 : (SpanY >= SpanZ ? 1 : 2);
        const SortKey = (LeafIndex) => SplitAxis === 0 ? CentroidX[LeafIndex] : (SplitAxis === 1 ? CentroidY[LeafIndex] : CentroidZ[LeafIndex]);

        const Sorted = Array.from(Order.subarray(RangeStart, RangeEnd + 1)).sort((LeftLeaf, RightLeaf) => SortKey(LeftLeaf) - SortKey(RightLeaf));
        for (let EntryIndex = 0; EntryIndex < Sorted.length; EntryIndex += 1)
        {
            Order[RangeStart + EntryIndex] = Sorted[EntryIndex];
        }

        const Middle = (RangeStart + RangeEnd) >> 1;
        const LeftChild  = BuildRange(RangeStart, Middle);
        const RightChild = BuildRange(Middle + 1, RangeEnd);

        const InternalIndex = NextFreeIndex;
        NextFreeIndex += 1;
        Tree.LeftIndex[InternalIndex]  = LeftChild;
        Tree.RightIndex[InternalIndex] = RightChild;

        for (let Channel = 0; Channel < 3; Channel += 1)
        {
            Tree.LowerBounds[InternalIndex * 3 + Channel] = Math.min(Tree.LowerBounds[LeftChild * 3 + Channel], Tree.LowerBounds[RightChild * 3 + Channel]);
            Tree.UpperBounds[InternalIndex * 3 + Channel] = Math.max(Tree.UpperBounds[LeftChild * 3 + Channel], Tree.UpperBounds[RightChild * 3 + Channel]);
        }
        Tree.TotalPower[InternalIndex] = Tree.TotalPower[LeftChild] + Tree.TotalPower[RightChild];

        const Cone = MergeOrientationCones(
            [ Tree.ConeAxis[LeftChild * 3 + 0], Tree.ConeAxis[LeftChild * 3 + 1], Tree.ConeAxis[LeftChild * 3 + 2] ], Tree.ConeSpread[LeftChild],
            [ Tree.ConeAxis[RightChild * 3 + 0], Tree.ConeAxis[RightChild * 3 + 1], Tree.ConeAxis[RightChild * 3 + 2] ], Tree.ConeSpread[RightChild]);
        Tree.ConeAxis[InternalIndex * 3 + 0] = Cone.Axis[0];
        Tree.ConeAxis[InternalIndex * 3 + 1] = Cone.Axis[1];
        Tree.ConeAxis[InternalIndex * 3 + 2] = Cone.Axis[2];
        Tree.ConeSpread[InternalIndex] = Cone.Spread;
        return InternalIndex;
    };

    Tree.RootIndex = BuildRange(0, LeafCount - 1);
    Tree.NodeCount = NextFreeIndex;
    return Tree;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 TREE IMPORTANCE BOUND
//------------------------------------------------------------------------------------------------------------------------

function ComputeNodeImportance(Tree, NodeIndex, SurfacePoint, SurfaceNormal, ViewDirection, LobeWidth)
{
    // Scalar, allocation-free form of the node importance bound. This runs once per tree level per candidate, so every
    // temporary array here shows up directly in the frame time.
    const LowerX = Tree.LowerBounds[NodeIndex * 3 + 0];
    const LowerY = Tree.LowerBounds[NodeIndex * 3 + 1];
    const LowerZ = Tree.LowerBounds[NodeIndex * 3 + 2];
    const UpperX = Tree.UpperBounds[NodeIndex * 3 + 0];
    const UpperY = Tree.UpperBounds[NodeIndex * 3 + 1];
    const UpperZ = Tree.UpperBounds[NodeIndex * 3 + 2];

    const DeltaX = (SurfacePoint[0] < LowerX ? LowerX : (SurfacePoint[0] > UpperX ? UpperX : SurfacePoint[0])) - SurfacePoint[0];
    const DeltaY = (SurfacePoint[1] < LowerY ? LowerY : (SurfacePoint[1] > UpperY ? UpperY : SurfacePoint[1])) - SurfacePoint[1];
    const DeltaZ = (SurfacePoint[2] < LowerZ ? LowerZ : (SurfacePoint[2] > UpperZ ? UpperZ : SurfacePoint[2])) - SurfacePoint[2];
    const DistanceSquared = Math.max(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ, 1.0e-4);
    const InverseDistance = 1.0 / Math.sqrt(DistanceSquared);
    const DirectionX = DeltaX * InverseDistance;
    const DirectionY = DeltaY * InverseDistance;
    const DirectionZ = DeltaZ * InverseDistance;

    const CenterX = 0.5 * (LowerX + UpperX) - SurfacePoint[0];
    const CenterY = 0.5 * (LowerY + UpperY) - SurfacePoint[1];
    const CenterZ = 0.5 * (LowerZ + UpperZ) - SurfacePoint[2];
    const CenterLength = Math.max(Math.sqrt(CenterX * CenterX + CenterY * CenterY + CenterZ * CenterZ), 1.0e-4);
    const CenterDirectionX = CenterX / CenterLength;
    const CenterDirectionY = CenterY / CenterLength;
    const CenterDirectionZ = CenterZ / CenterLength;

    const CosineNormal = Math.max(SurfaceNormal[0] * DirectionX + SurfaceNormal[1] * DirectionY + SurfaceNormal[2] * DirectionZ, 0.0);
    const CosineEmission = CenterDirectionX * Tree.ConeAxis[NodeIndex * 3 + 0]
        + CenterDirectionY * Tree.ConeAxis[NodeIndex * 3 + 1]
        + CenterDirectionZ * Tree.ConeAxis[NodeIndex * 3 + 2];
    const ConeOuterCosine = Math.cos(Math.min(Pi, Tree.ConeSpread[NodeIndex] + 1.35));
    const EmissionBound = 0.10 + 0.90 * Clamp((CosineEmission - ConeOuterCosine) / Math.max(0.05, 1.0 - ConeOuterCosine), 0.0, 1.0);

    const HalfX = ViewDirection[0] + CenterDirectionX;
    const HalfY = ViewDirection[1] + CenterDirectionY;
    const HalfZ = ViewDirection[2] + CenterDirectionZ;
    const HalfLength = Math.max(Math.sqrt(HalfX * HalfX + HalfY * HalfY + HalfZ * HalfZ), 1.0e-6);
    const HalfNormalisedX = HalfX / HalfLength;
    const HalfNormalisedY = HalfY / HalfLength;
    const HalfNormalisedZ = HalfZ / HalfLength;
    const CosineHalf = Math.max(SurfaceNormal[0] * HalfNormalisedX + SurfaceNormal[1] * HalfNormalisedY + SurfaceNormal[2] * HalfNormalisedZ, 0.0);

    const AngularCosine = Clamp(HalfNormalisedX * CenterDirectionX + HalfNormalisedY * CenterDirectionY + HalfNormalisedZ * CenterDirectionZ, -1.0, 1.0);
    const AngularOffset = Math.acos(AngularCosine);
    const LobeAlignment = Math.exp(-(AngularOffset * AngularOffset) / Math.max(LobeWidth, 0.01));

    const IrradianceBound = Tree.TotalPower[NodeIndex] * CosineNormal * EmissionBound / DistanceSquared;
    const BoundScalar = IrradianceBound + IrradianceBound * CosineHalf * CosineHalf * LobeAlignment;
    return BoundScalar > ImportanceFloor ? BoundScalar : ImportanceFloor;
}

function DescentProportion(Tree, NodeIndex, SurfacePoint, SurfaceNormal, ViewDirection, LobeWidth)
{
    const LeftShare  = ComputeNodeImportance(Tree, Tree.LeftIndex[NodeIndex], SurfacePoint, SurfaceNormal, ViewDirection, LobeWidth);
    const RightShare = ComputeNodeImportance(Tree, Tree.RightIndex[NodeIndex], SurfacePoint, SurfaceNormal, ViewDirection, LobeWidth);
    return Clamp(LeftShare / (LeftShare + RightShare), 1.0e-4, 1.0 - 1.0e-4);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    BRDF EVALUATION
//------------------------------------------------------------------------------------------------------------------------

// Returns diffuse and specular BRDF components separately (units 1/sr, cosine NOT folded in) so the deterministic tier can
// pair the diffuse component with a closed-form polygon irradiance and the specular component with area quadrature.
function ComputeBrdfComponents(Surface, SurfaceNormal, ViewDirection, LightDirection, DiffuseOut, SpecularOut)
{
    for (let Channel = 0; Channel < 3; Channel += 1)
    {
        DiffuseOut[Channel] = 0.0;
        SpecularOut[Channel] = 0.0;
    }
    const CosineLight = DotProduct(SurfaceNormal, LightDirection);
    if (CosineLight <= 0.0)
    {
        return false;
    }

    const Roughness = Clamp(Surface.Roughness, 0.03, 1.0);
    const Alpha = Roughness * Roughness;
    const AlphaSquared = Alpha * Alpha;
    const HalfVector = NormalizeVector([
        ViewDirection[0] + LightDirection[0], ViewDirection[1] + LightDirection[1], ViewDirection[2] + LightDirection[2] ]);
    const CosineHalf = Math.max(DotProduct(SurfaceNormal, HalfVector), 0.0);
    const CosineView = Math.max(DotProduct(SurfaceNormal, ViewDirection), 1.0e-4);
    const HalfView  = Math.max(DotProduct(HalfVector, ViewDirection), 0.0);

    const Denominator = CosineHalf * CosineHalf * (AlphaSquared - 1.0) + 1.0;
    const Distribution = AlphaSquared / (Pi * Denominator * Denominator + 1.0e-12);
    const Visibility = 0.5 / (CosineLight * Math.sqrt(CosineView * CosineView * (1.0 - AlphaSquared) + AlphaSquared)
        + CosineView * Math.sqrt(CosineLight * CosineLight * (1.0 - AlphaSquared) + AlphaSquared) + 1.0e-12);
    // Strict single-scatter GGX: D * V * F. No dot(H,L) or multiple-scatter fudge, both of which silently shift energy.
    const SpecularLobe = Distribution * Visibility;

    const Metallic = Surface.Metallic;
    const FresnelPower = Math.pow(1.0 - HalfView, 5.0);

    for (let Channel = 0; Channel < 3; Channel += 1)
    {
        const BaseReflectance = Surface.Albedo[Channel];
        const SpecularReflectance = (1.0 - Metallic) * 0.04 + Metallic * BaseReflectance;
        const Fresnel = SpecularReflectance + (1.0 - SpecularReflectance) * FresnelPower;
        const DiffuseTerm = (1.0 - Metallic) * BaseReflectance * (1.0 - Fresnel) / Pi;
        DiffuseOut[Channel]  = DiffuseTerm > 0.0 && DiffuseTerm < MaximumFloat ? DiffuseTerm : 0.0;
        SpecularOut[Channel] = SpecularLobe * Fresnel;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SCENE INTERSECTION ROUTINES
//------------------------------------------------------------------------------------------------------------------------

function IntersectPrisms(Prisms, RayOrigin, RayDirection, MaximumDistance)
{
    let NearestDistance = MaximumDistance;
    let NearestNormal = null;
    let NearestMaterial = null;

    for (let PrimitiveIndex = 0; PrimitiveIndex < Prisms.length; PrimitiveIndex += 1)
    {
        const Primitive = Prisms[PrimitiveIndex];
        const Lower = Primitive.LowerBounds;
        const Upper = Primitive.UpperBounds;
        const InverseX = 1.0 / (Math.abs(RayDirection[0]) < 1.0e-12 ? 1.0e-12 : RayDirection[0]);
        const InverseY = 1.0 / (Math.abs(RayDirection[1]) < 1.0e-12 ? 1.0e-12 : RayDirection[1]);
        const InverseZ = 1.0 / (Math.abs(RayDirection[2]) < 1.0e-12 ? 1.0e-12 : RayDirection[2]);
        let NearX = (Lower[0] - RayOrigin[0]) * InverseX;
        let FarX  = (Upper[0] - RayOrigin[0]) * InverseX;
        if (NearX > FarX) { const Swap = NearX; NearX = FarX; FarX = Swap; }
        let NearY = (Lower[1] - RayOrigin[1]) * InverseY;
        let FarY  = (Upper[1] - RayOrigin[1]) * InverseY;
        if (NearY > FarY) { const Swap = NearY; NearY = FarY; FarY = Swap; }
        let NearZ = (Lower[2] - RayOrigin[2]) * InverseZ;
        let FarZ  = (Upper[2] - RayOrigin[2]) * InverseZ;
        if (NearZ > FarZ) { const Swap = NearZ; NearZ = FarZ; FarZ = Swap; }

        const NearDistance = Math.max(NearX, NearY, NearZ);
        const FarDistance  = Math.min(FarX, FarY, FarZ);
        if (NearDistance > FarDistance || FarDistance <= 1.0e-4)
        {
            continue;
        }
        const HitDistance = NearDistance > 0.0 ? NearDistance : FarDistance;
        if (HitDistance <= 1.0e-4 || HitDistance >= NearestDistance)
        {
            continue;
        }
        const HitPoint = [
            RayOrigin[0] + RayDirection[0] * HitDistance,
            RayOrigin[1] + RayDirection[1] * HitDistance,
            RayOrigin[2] + RayDirection[2] * HitDistance,
        ];
        const HalfX = 0.5 * (Lower[0] + Upper[0]);
        const HalfY = 0.5 * (Lower[1] + Upper[1]);
        const HalfZ = 0.5 * (Lower[2] + Upper[2]);
        const ScaleX = Math.abs(HitPoint[0] - HalfX) / Math.max(1.0e-5, 0.5 * (Upper[0] - Lower[0]));
        const ScaleY = Math.abs(HitPoint[1] - HalfY) / Math.max(1.0e-5, 0.5 * (Upper[1] - Lower[1]));
        const ScaleZ = Math.abs(HitPoint[2] - HalfZ) / Math.max(1.0e-5, 0.5 * (Upper[2] - Lower[2]));
        let Normal;
        if (ScaleX >= ScaleY && ScaleX >= ScaleZ)
        {
            Normal = [ HitPoint[0] > HalfX ? 1.0 : -1.0, 0.0, 0.0 ];
        }
        else if (ScaleY >= ScaleZ)
        {
            Normal = [ 0.0, HitPoint[1] > HalfY ? 1.0 : -1.0, 0.0 ];
        }
        else
        {
            Normal = [ 0.0, 0.0, HitPoint[2] > HalfZ ? 1.0 : -1.0 ];
        }
        NearestDistance = HitDistance;
        NearestNormal = Normal;
        NearestMaterial = Primitive.Material;
    }

    return NearestMaterial === null ? null : { Distance: NearestDistance, Normal: NearestNormal, Material: NearestMaterial };
}

function IntersectOrbs(Orbs, RayOrigin, RayDirection, MaximumDistance, CurrentHit)
{
    let NearestHit = CurrentHit;
    for (let PrimitiveIndex = 0; PrimitiveIndex < Orbs.length; PrimitiveIndex += 1)
    {
        const Primitive = Orbs[PrimitiveIndex];
        const OffsetX = RayOrigin[0] - Primitive.Center[0];
        const OffsetY = RayOrigin[1] - Primitive.Center[1];
        const OffsetZ = RayOrigin[2] - Primitive.Center[2];
        const QuadraticB = OffsetX * RayDirection[0] + OffsetY * RayDirection[1] + OffsetZ * RayDirection[2];
        const QuadraticC = OffsetX * OffsetX + OffsetY * OffsetY + OffsetZ * OffsetZ - Primitive.Radius * Primitive.Radius;
        const Discriminant = QuadraticB * QuadraticB - QuadraticC;
        if (Discriminant < 0.0)
        {
            continue;
        }
        const SquareRoot = Math.sqrt(Discriminant);
        let HitDistance = -QuadraticB - SquareRoot;
        if (HitDistance <= 1.0e-4)
        {
            HitDistance = -QuadraticB + SquareRoot;
        }
        if (HitDistance <= 1.0e-4 || HitDistance >= MaximumDistance)
        {
            continue;
        }
        if (NearestHit !== null && HitDistance >= NearestHit.Distance)
        {
            continue;
        }
        const HitPoint = [
            RayOrigin[0] + RayDirection[0] * HitDistance,
            RayOrigin[1] + RayDirection[1] * HitDistance,
            RayOrigin[2] + RayDirection[2] * HitDistance,
        ];
        NearestHit = {
            Distance: HitDistance,
            Normal: NormalizeVector(SubtractVector(HitPoint, Primitive.Center)),
            Material: Primitive.Material,
        };
    }
    return NearestHit;
}

export function TraceScene(Studio, RayOrigin, RayDirection)
{
    const PrismHit = IntersectPrisms(Studio.Prisms, RayOrigin, RayDirection, MaximumFloat);
    return IntersectOrbs(Studio.Orbs, RayOrigin, RayDirection, MaximumFloat, PrismHit);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   BINARY VISIBILITY
//------------------------------------------------------------------------------------------------------------------------

export function TraceOcclusion(Studio, RayOrigin, RayDirection, MaximumDistance)
{
    if (IntersectPrisms(Studio.OcclusionPrisms, RayOrigin, RayDirection, MaximumDistance) !== null)
    {
        return 1.0;
    }
    return IntersectOrbs(Studio.OcclusionOrbs, RayOrigin, RayDirection, MaximumDistance, null) !== null ? 1.0 : 0.0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          SIGNED-DISTANCE EVALUATION TIER
//------------------------------------------------------------------------------------------------------------------------

export function EvaluateDistanceField(Studio, Point)
{
    let Distance = MaximumFloat;
    const OcclusionPrisms = Studio.OcclusionPrisms === undefined ? Studio.Prisms : Studio.OcclusionPrisms;
    const OcclusionOrbs = Studio.OcclusionOrbs === undefined ? Studio.Orbs : Studio.OcclusionOrbs;
    for (let PrimitiveIndex = 0; PrimitiveIndex < OcclusionPrisms.length; PrimitiveIndex += 1)
    {
        const Primitive = OcclusionPrisms[PrimitiveIndex];
        const HalfExtentX = 0.5 * (Primitive.UpperBounds[0] - Primitive.LowerBounds[0]);
        const HalfExtentY = 0.5 * (Primitive.UpperBounds[1] - Primitive.LowerBounds[1]);
        const HalfExtentZ = 0.5 * (Primitive.UpperBounds[2] - Primitive.LowerBounds[2]);
        const OffsetX = Point[0] - 0.5 * (Primitive.LowerBounds[0] + Primitive.UpperBounds[0]);
        const OffsetY = Point[1] - 0.5 * (Primitive.LowerBounds[1] + Primitive.UpperBounds[1]);
        const OffsetZ = Point[2] - 0.5 * (Primitive.LowerBounds[2] + Primitive.UpperBounds[2]);
        const OutsideX = OffsetX - Clamp(OffsetX, -HalfExtentX, HalfExtentX);
        const OutsideY = OffsetY - Clamp(OffsetY, -HalfExtentY, HalfExtentY);
        const OutsideZ = OffsetZ - Clamp(OffsetZ, -HalfExtentZ, HalfExtentZ);
        const OutsideDistance = Math.sqrt(OutsideX * OutsideX + OutsideY * OutsideY + OutsideZ * OutsideZ);
        const InsideDistance = Math.min(HalfExtentX - Math.abs(OffsetX), HalfExtentY - Math.abs(OffsetY), HalfExtentZ - Math.abs(OffsetZ));
        Distance = Math.min(Distance, InsideDistance > 0.0 ? -InsideDistance : OutsideDistance);
    }
    for (let PrimitiveIndex = 0; PrimitiveIndex < OcclusionOrbs.length; PrimitiveIndex += 1)
    {
        const Primitive = OcclusionOrbs[PrimitiveIndex];
        Distance = Math.min(Distance, VectorLength(SubtractVector(Point, Primitive.Center)) - Primitive.Radius);
    }
    return Distance;
}

// Cone-traced penumbra estimate: marches the signed distance while tracking the angular cone of the luminaire and returns
// the fraction of the cone that stays clear, i.e. 0 for unshadowed and 1 for fully occluded.
export function TraceDistanceFieldOcclusion(Studio, RayOrigin, RayDirection, MaximumDistance, TangentSpread)
{
    let Travel = 0.05;
    let Visibility = 1.0;
    for (let MarchStep = 0; MarchStep < 192 && Travel < MaximumDistance; MarchStep += 1)
    {
        const FieldDistance = EvaluateDistanceField(Studio, [
            RayOrigin[0] + RayDirection[0] * Travel,
            RayOrigin[1] + RayDirection[1] * Travel,
            RayOrigin[2] + RayDirection[2] * Travel,
        ]);
        if (FieldDistance < 0.002)
        {
            return 1.0;
        }
        const ConeRadius = Math.max(TangentSpread * Travel, 1.0e-4);
        Visibility = Math.min(Visibility, Clamp(FieldDistance / ConeRadius, 0.0, 1.0));
        if (Visibility <= 0.0)
        {
            return 1.0;
        }
        Travel += Math.max(FieldDistance * 0.9, 0.05);
    }
    return 1.0 - Visibility;
}

// Angular radius of the luminaire as seen from the shading point. This, not the emission cone, sets the penumbra width.
export function ComputeTangentSpread(Luminaire, Distance)
{
    if (Luminaire.Kind === LightKind.Sun)
    {
        return 0.00465;
    }
    const ApertureRadius = Luminaire.Kind === LightKind.Spot
        ? 0.06
        : (Luminaire.MeasureKind === 'Area' ? Math.sqrt(Math.max(Luminaire.Area, 1.0e-6)) * 0.25 : 0.02);
    return Clamp(ApertureRadius / Math.max(Distance, 0.05), 0.002, 1.0);
}

export function SignedDistanceVisibility(Studio, RepresentationName, RayOrigin, RayDirection, MaximumDistance, TangentSpread)
{
    if (RepresentationName === Representation.SignedDistance)
    {
        return 1.0 - TraceDistanceFieldOcclusion(Studio, RayOrigin, RayDirection, MaximumDistance, TangentSpread);
    }
    return 1.0 - TraceOcclusion(Studio, RayOrigin, RayDirection, MaximumDistance);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              LUMINAIRE SAMPLE ROUTINES
//------------------------------------------------------------------------------------------------------------------------

function DrawUnitVector(Draw)
{
    const Z = 2.0 * Draw() - 1.0;
    const Azimuth = TwoPi * Draw();
    const Radial = Math.sqrt(Math.max(0.0, 1.0 - Z * Z));
    return [ Radial * Math.cos(Azimuth), Z, Radial * Math.sin(Azimuth) ];
}

function ConstructBasisFromNormal(UnitNormal)
{
    const Reference = Math.abs(UnitNormal[1]) < 0.9 ? [ 0.0, 1.0, 0.0 ] : [ 1.0, 0.0, 0.0 ];
    const Tangent = NormalizeVector(CrossProduct(Reference, UnitNormal));
    return { Tangent: Tangent, Bitangent: CrossProduct(UnitNormal, Tangent) };
}

// Returns the density of the point draw inside the luminaire's own measure (1/A for area luminaires).
function DrawLuminairePoint(Luminaire, Draw, PointResult)
{
    if (Luminaire.Kind === LightKind.Rect)
    {
        const UniformU = 2.0 * Draw() - 1.0;
        const UniformV = 2.0 * Draw() - 1.0;
        PointResult[0] = Luminaire.Center[0] + Luminaire.HalfAxisU[0] * UniformU + Luminaire.HalfAxisV[0] * UniformV;
        PointResult[1] = Luminaire.Center[1] + Luminaire.HalfAxisU[1] * UniformU + Luminaire.HalfAxisV[1] * UniformV;
        PointResult[2] = Luminaire.Center[2] + Luminaire.HalfAxisU[2] * UniformU + Luminaire.HalfAxisV[2] * UniformV;
        return 1.0 / Luminaire.Area;
    }
    if (Luminaire.Kind === LightKind.Disc)
    {
        const Basis = ConstructBasisFromNormal(Luminaire.Normal);
        const Radius = Luminaire.Radius * Math.sqrt(Draw());
        const Azimuth = TwoPi * Draw();
        const OffsetU = Radius * Math.cos(Azimuth);
        const OffsetV = Radius * Math.sin(Azimuth);
        PointResult[0] = Luminaire.Center[0] + Basis.Tangent[0] * OffsetU + Basis.Bitangent[0] * OffsetV;
        PointResult[1] = Luminaire.Center[1] + Basis.Tangent[1] * OffsetU + Basis.Bitangent[1] * OffsetV;
        PointResult[2] = Luminaire.Center[2] + Basis.Tangent[2] * OffsetU + Basis.Bitangent[2] * OffsetV;
        return 1.0 / Luminaire.Area;
    }
    const Direction = DrawUnitVector(Draw);
    PointResult[0] = Luminaire.Center[0] + Direction[0] * Luminaire.Radius;
    PointResult[1] = Luminaire.Center[1] + Direction[1] * Luminaire.Radius;
    PointResult[2] = Luminaire.Center[2] + Direction[2] * Luminaire.Radius;
    return 1.0 / Luminaire.Area;
}

// Unshadowed contribution density. Area luminaires return radiance * geometry * brdf; the caller divides by the proposal.
function EvaluateLuminaireContribution(Luminaire, SurfacePoint, SurfaceNormal, ViewDirection, Surface, SamplePoint, FillResult)
{
    FillResult[0] = 0.0; FillResult[1] = 0.0; FillResult[2] = 0.0;

    if (SamplePoint === null)
    {
        if (Luminaire.Kind === LightKind.Sun)
        {
            const CosineLight = DotProduct(SurfaceNormal, Luminaire.Direction);
            if (CosineLight <= 0.0)
            {
                return FillResult;
            }
            ComputeBrdfComponents(Surface, SurfaceNormal, ViewDirection, Luminaire.Direction, ScratchDiffuse, ScratchSpecular);
            const SunCosine = Math.max(DotProduct(SurfaceNormal, Luminaire.Direction), 0.0);
            for (let Channel = 0; Channel < 3; Channel += 1)
            {
                FillResult[Channel] = Luminaire.PeakIntensity[Channel] * (ScratchDiffuse[Channel] + ScratchSpecular[Channel]) * SunCosine;
            }
            return FillResult;
        }
        const Offset = SubtractVector(Luminaire.Center, SurfacePoint);
        const DistanceSquared = Math.max(DotProduct(Offset, Offset), 1.0e-4);
        const InverseDistance = 1.0 / Math.sqrt(DistanceSquared);
        const Direction = ScaleVector(Offset, InverseDistance);
        const CosineLight = DotProduct(SurfaceNormal, Direction);
        if (CosineLight <= 0.0)
        {
            return FillResult;
        }
        let Emitted = 1.0;
        if (Luminaire.Kind === LightKind.Spot)
        {
            Emitted = SampleProfileRadiance(Luminaire, -DotProduct(Direction, Luminaire.Axis));
            if (Emitted <= 0.0)
            {
                return FillResult;
            }
            ComputeBrdfComponents(Surface, SurfaceNormal, ViewDirection, Direction, ScratchDiffuse, ScratchSpecular);
            for (let Channel = 0; Channel < 3; Channel += 1)
            {
                FillResult[Channel] = Luminaire.PeakIntensity[Channel] * Emitted * CosineLight
                    * (ScratchDiffuse[Channel] + ScratchSpecular[Channel]) / DistanceSquared;
            }
            return FillResult;
        }
        // Disc and sphere requested at their centre (deterministic tier point approximation).
        const CentreLuminance = Luminaire.Radiance === undefined ? 1.0 : Luminance(Luminaire.Radiance);
        ComputeBrdfComponents(Surface, SurfaceNormal, ViewDirection, Direction, ScratchDiffuse, ScratchSpecular);
        for (let Channel = 0; Channel < 3; Channel += 1)
        {
            const EmittedRadiance = Luminaire.Radiance === undefined ? Luminaire.PeakIntensity[Channel] : Luminaire.Radiance[Channel];
            FillResult[Channel] = EmittedRadiance * Math.max(Luminaire.Area, 0.0) * CosineLight
                * (ScratchDiffuse[Channel] + ScratchSpecular[Channel]) / DistanceSquared;
        }
        return FillResult;
    }

    const Offset = SubtractVector(SamplePoint, SurfacePoint);
    const DistanceSquared = Math.max(DotProduct(Offset, Offset), 1.0e-4);
    const InverseDistance = 1.0 / Math.sqrt(DistanceSquared);
    const Direction = ScaleVector(Offset, InverseDistance);
    const CosineLight = DotProduct(SurfaceNormal, Direction);
    const CosineLuminaire = Luminaire.Normal === undefined ? 1.0 : -DotProduct(Luminaire.Normal, Direction);
    if (CosineLight <= 0.0 || CosineLuminaire <= 0.0)
    {
        return FillResult;
    }
    ComputeBrdfComponents(Surface, SurfaceNormal, ViewDirection, Direction, ScratchDiffuse, ScratchSpecular);
    const Geometry = CosineLight * CosineLuminaire / DistanceSquared;
    for (let Channel = 0; Channel < 3; Channel += 1)
    {
        FillResult[Channel] = Luminaire.Radiance[Channel] * Geometry * (ScratchDiffuse[Channel] + ScratchSpecular[Channel]);
    }
    return FillResult;
}

const ScratchDiffuse = new Float64Array(3);

export function EstimateLobeWidth(Surface)
{
    const Roughness = Clamp(Surface.Roughness, 0.03, 1.0);
    return Math.max(0.02, Roughness * Roughness * 2.2);
}

// Shadow-edge softness: the emitter's angular radius plus the width of the receiver's own reflection lobe. A polished floor
// turns a compact downlight into a sharp-edged occluder, so the lobe width must be in the spread or penumbrae are wrong.
export function ComputeShadowTangentSpread(Luminaire, Distance, Surface)
{
    return Clamp(ComputeTangentSpread(Luminaire, Distance) + EstimateLobeWidth(Surface) * 0.35, 0.002, 1.0);
}
const ScratchSpecular = new Float64Array(3);

//------------------------------------------------------------------------------------------------------------------------
//                                              HIERARCHICAL CANDIDATE DRAW
//------------------------------------------------------------------------------------------------------------------------

export function DebugShadeTrace(Studio, Options, RayOrigin, RayDirection, HitNormal, Material)
{
    const Draw = ConstructXorShiftSeed(9);
    const SurfacePoint = [ RayOrigin[0], RayOrigin[1], RayOrigin[2] ];
    const OffsetOrigin = [ SurfacePoint[0] + HitNormal[0] * SurfaceBias, SurfacePoint[1] + HitNormal[1] * SurfaceBias, SurfacePoint[2] + HitNormal[2] * SurfaceBias ];
    const ViewDirection = ScaleVector(RayDirection, -1.0);
    const Surface = { Albedo: Material.Albedo, Roughness: Material.Roughness, Metallic: Material.Metallic };
    const Resampled = ResampleCandidates(Studio.Tree, SurfacePoint, HitNormal, ViewDirection, Surface, Options.CandidateCount, Draw, Options.Technique, null);
    if (Resampled.Selected === null) { return { Stage: 'null-selection' }; }
    const Selected = Resampled.Selected;
    const Luminaire = Studio.Lights[Selected.LuminaireIndex];
    const Offset = SubtractVector(Selected.SamplePoint === null ? Luminaire.Center : Selected.SamplePoint, SurfacePoint);
    const Distance = VectorLength(Offset);
    const Direction = ScaleVector(Offset, 1.0 / Distance);
    const Spread = ComputeTangentSpread(Luminaire, Distance);
    const Penumbra = TraceDistanceFieldOcclusion(Studio, OffsetOrigin, Direction, Math.max(Distance - 1.0e-3, 1.0e-4), Spread);
    const Contribution = DebugEvaluateContribution(Luminaire, SurfacePoint, HitNormal, ViewDirection, Surface, Selected.SamplePoint);
    return {
        Stage: 'evaluated', TargetDensity: Selected.TargetDensity,
        ResamplingWeight: Resampled.WeightSum / (Resampled.SampleCount * Selected.TargetDensity),
        Distance: Distance, TangentSpread: Spread, Penumbra: Penumbra,
        Contribution: [ Contribution[0], Contribution[1], Contribution[2] ],
        Final: [
            Contribution[0] * (1.0 - Penumbra) * Resampled.WeightSum / (Resampled.SampleCount * Selected.TargetDensity),
            Contribution[1] * (1.0 - Penumbra) * Resampled.WeightSum / (Resampled.SampleCount * Selected.TargetDensity),
            Contribution[2] * (1.0 - Penumbra) * Resampled.WeightSum / (Resampled.SampleCount * Selected.TargetDensity),
        ],
    };
}

export function DebugResample(LuminaireList, SurfacePoint, SurfaceNormal, ViewDirection, Surface, CandidateCount, Seed, ProposalKind)
{
    const Studio = { Lights: LuminaireList };
    Studio.Tree = ConstructLightTree(LuminaireList);
    const Draw = ConstructXorShiftSeed(Seed);
    return ResampleCandidates(Studio.Tree, SurfacePoint, SurfaceNormal, ViewDirection, Surface, CandidateCount, Draw, ProposalKind || Technique.LightTreeCut, null);
}

export function DebugEvaluateContribution(Luminaire, SurfacePoint, SurfaceNormal, ViewDirection, Surface, SamplePoint)
{
    const Out = new Float64Array(3);
    EvaluateLuminaireContribution(Luminaire, SurfacePoint, SurfaceNormal, ViewDirection, Surface, SamplePoint, Out);
    return [ Out[0], Out[1], Out[2] ];
}

function EvaluateCandidate(Luminaire, SurfacePoint, SurfaceNormal, ViewDirection, Surface, Draw, CandidateResult, SourceDensity)
{
    let SamplePoint = null;
    let MeasureDensity = 1.0;
    if (Luminaire.MeasureKind === 'Area')
    {
        SamplePoint = [ 0.0, 0.0, 0.0 ];
        MeasureDensity = DrawLuminairePoint(Luminaire, Draw, SamplePoint);
    }
    EvaluateLuminaireContribution(Luminaire, SurfacePoint, SurfaceNormal, ViewDirection, Surface, SamplePoint, CandidateResult.Unshadowed);
    CandidateResult.SamplePoint = SamplePoint;
    CandidateResult.SourceDensity = Math.max(SourceDensity * MeasureDensity, 1.0e-16);
    CandidateResult.TargetDensity = Math.max(Luminance(CandidateResult.Unshadowed), 0.0);
    return CandidateResult;
}

function DrawHierarchicalCandidate(Tree, SurfacePoint, SurfaceNormal, ViewDirection, Surface, LobeWidth, Draw, CandidateResult)
{
    let NodeIndex = Tree.RootIndex;
    let BranchProbability = 1.0;
    while (Tree.LeftIndex[NodeIndex] !== -1)
    {
        const LeftShare = DescentProportion(Tree, NodeIndex, SurfacePoint, SurfaceNormal, ViewDirection, LobeWidth);
        if (Draw() < LeftShare)
        {
            BranchProbability *= LeftShare;
            NodeIndex = Tree.LeftIndex[NodeIndex];
        }
        else
        {
            BranchProbability *= 1.0 - LeftShare;
            NodeIndex = Tree.RightIndex[NodeIndex];
        }
    }
    const LuminaireIndex = Tree.LeafLuminaire[NodeIndex];
    CandidateResult.LuminaireIndex = LuminaireIndex;
    return EvaluateCandidate(Tree.Luminaires[LuminaireIndex], SurfacePoint, SurfaceNormal, ViewDirection, Surface, Draw, CandidateResult, BranchProbability);
}

function DrawSubtreeCandidate(Tree, StartNodeIndex, StartProbability, SurfacePoint, SurfaceNormal, ViewDirection, Surface, LobeWidth, Draw, CandidateResult)
{
    let NodeIndex = StartNodeIndex;
    let BranchProbability = StartProbability;
    while (Tree.LeftIndex[NodeIndex] !== -1)
    {
        const LeftShare = DescentProportion(Tree, NodeIndex, SurfacePoint, SurfaceNormal, ViewDirection, LobeWidth);
        if (Draw() < LeftShare)
        {
            BranchProbability *= LeftShare;
            NodeIndex = Tree.LeftIndex[NodeIndex];
        }
        else
        {
            BranchProbability *= 1.0 - LeftShare;
            NodeIndex = Tree.RightIndex[NodeIndex];
        }
    }
    const LuminaireIndex = Tree.LeafLuminaire[NodeIndex];
    CandidateResult.LuminaireIndex = LuminaireIndex;
    return EvaluateCandidate(Tree.Luminaires[LuminaireIndex], SurfacePoint, SurfaceNormal, ViewDirection, Surface, Draw, CandidateResult, BranchProbability);
}

function DrawUniformCandidate(Scratch, LuminaireCount, SurfacePoint, SurfaceNormal, ViewDirection, Surface, Draw, CandidateResult)
{
    const LuminaireIndex = Math.min(LuminaireCount - 1, Math.floor(Draw() * LuminaireCount));
    CandidateResult.LuminaireIndex = LuminaireIndex;
    return EvaluateCandidate(Scratch[LuminaireIndex], SurfacePoint, SurfaceNormal, ViewDirection, Surface, Draw, CandidateResult, 1.0 / LuminaireCount);
}

function ConstructCandidateScratch()
{
    return { LuminaireIndex: -1, SamplePoint: null, SourceDensity: 1.0, TargetDensity: 0.0, Unshadowed: new Float64Array(3) };
}

//------------------------------------------------------------------------------------------------------------------------
//                                            WEIGHTED RESERVOIR SELECTION
//------------------------------------------------------------------------------------------------------------------------
// Streaming weighted reservoir sampling over M candidates drawn from the proposal q, targeting p-hat ∝ unshadowed
// luminance. Returns the selected candidate with W = (1/M) Σ p-hat/q, so C_selected * W / p-hat_selected is unbiased.

export function ResampleCandidates(Tree, SurfacePoint, SurfaceNormal, ViewDirection, Surface, CandidateCount, Draw, ProposalKind, SharedLuminaireIndices)
{
    const LobeWidth = EstimateLobeWidth(Surface);
    const Candidate = ConstructCandidateScratch();
    const LuminaireScratch = Tree.Luminaires;
    let WeightSum = 0.0;
    let Selected = null;
    let SampledCount = 0;

    for (let CandidateIndex = 0; CandidateIndex < CandidateCount; CandidateIndex += 1)
    {
        if (ProposalKind === Technique.UniformLightPick)
        {
            DrawUniformCandidate(LuminaireScratch, Tree.Luminaires.length, SurfacePoint, SurfaceNormal, ViewDirection, Surface, Draw, Candidate);
        }
        else if (SharedLuminaireIndices !== null && SharedLuminaireIndices !== undefined && SharedLuminaireIndices.length > 0)
        {
            const SharedSlot = Math.min(SharedLuminaireIndices.length - 1, Math.floor(Draw() * SharedLuminaireIndices.length));
            DrawSubtreeCandidate(Tree, SharedLuminaireIndices[SharedSlot], 1.0 / SharedLuminaireIndices.length,
                SurfacePoint, SurfaceNormal, ViewDirection, Surface, LobeWidth, Draw, Candidate);
        }
        else
        {
            DrawHierarchicalCandidate(Tree, SurfacePoint, SurfaceNormal, ViewDirection, Surface, LobeWidth, Draw, Candidate);
        }

        SampledCount += 1;
        const CandidateWeight = Candidate.TargetDensity / Candidate.SourceDensity;
        WeightSum += CandidateWeight;
        if (CandidateWeight > 0.0 && Draw() * WeightSum < CandidateWeight)
        {
            Selected = {
                LuminaireIndex: Candidate.LuminaireIndex,
                SamplePoint: Candidate.SamplePoint === null ? null : [ Candidate.SamplePoint[0], Candidate.SamplePoint[1], Candidate.SamplePoint[2] ],
                SourceDensity: Candidate.SourceDensity,
                TargetDensity: Candidate.TargetDensity,
                ClampScale: undefined,
            };
        }
    }

    if (Selected === null || Selected.TargetDensity <= 0.0 || WeightSum <= 0.0)
    {
        return { Selected: null, SampleCount: SampledCount, WeightSum: WeightSum };
    }
    return { Selected: Selected, SampleCount: SampledCount, WeightSum: WeightSum };
}

// A shared cut is a set of DISTINCT light-tree subtrees selected once per screen block. Every pixel in the block then draws
// uniformly among the cut nodes and descends inside the chosen subtree by importance. Distinct subtrees make the proposal a
// clean mixture with density 1/K per node, so the estimator stays unbiased; drawing K leaves independently with replacement
// (the obvious implementation) creates duplicate luminaires, an unknown empirical proposal, and a measurable brightening bias.
export function ConstructSharedCut(Tree, SurfacePoint, SurfaceNormal, ViewDirection, Surface, CutSize, Draw)
{
    void Draw;
    const LobeWidth = EstimateLobeWidth(Surface);
    const CutNodes = [ Tree.RootIndex ];
    const Terminal = [ Tree.LeftIndex[Tree.RootIndex] === -1 ];

    while (CutNodes.length < CutSize)
    {
        let SplitSlot = -1;
        let SplitImportance = -1.0;
        for (let SlotIndex = 0; SlotIndex < CutNodes.length; SlotIndex += 1)
        {
            if (Terminal[SlotIndex])
            {
                continue;
            }
            const Importance = ComputeNodeImportance(Tree, CutNodes[SlotIndex], SurfacePoint, SurfaceNormal, ViewDirection, LobeWidth);
            if (Importance > SplitImportance)
            {
                SplitImportance = Importance;
                SplitSlot = SlotIndex;
            }
        }
        if (SplitSlot < 0)
        {
            break;
        }
        const NodeIndex = CutNodes[SplitSlot];
        CutNodes[SplitSlot] = Tree.LeftIndex[NodeIndex];
        CutNodes.push(Tree.RightIndex[NodeIndex]);
        Terminal[SplitSlot] = Tree.LeftIndex[Tree.LeftIndex[NodeIndex]] === -1;
        Terminal.push(Tree.LeftIndex[Tree.RightIndex[NodeIndex]] === -1);
    }
    return CutNodes;
}

export function ConstructSharedCutForPoint(Studio, SurfacePoint, SurfaceNormal, ViewDirection, Surface, CutSize, Seed)
{
    return ConstructSharedCut(Studio.Tree, SurfacePoint, SurfaceNormal, ViewDirection, Surface, CutSize, ConstructXorShiftSeed(Seed >>> 0));
}

//------------------------------------------------------------------------------------------------------------------------
//                                        DETERMINISTIC POLYGON IRRADIANCE TIER
//------------------------------------------------------------------------------------------------------------------------

// Closed-form Lambert irradiance of a polygon seen from a point (Arvo / Lambert's boundary integral form).
export function EvaluatePolygonIrradiance(CornerVectors, SurfaceNormal)
{
    let CosineSum = 0.0;
    const CornerCount = CornerVectors.length;
    for (let CornerIndex = 0; CornerIndex < CornerCount; CornerIndex += 1)
    {
        const Current = NormalizeVector(CornerVectors[CornerIndex]);
        const Next = NormalizeVector(CornerVectors[(CornerIndex + 1) % CornerCount]);
        const EdgeCross = CrossProduct(Current, Next);
        const EdgeLength = VectorLength(EdgeCross);
        if (EdgeLength < 1.0e-9)
        {
            continue;
        }
        const EdgeNormal = ScaleVector(EdgeCross, 1.0 / EdgeLength);
        const Angle = Math.acos(Clamp(DotProduct(Current, Next), -1.0, 1.0));
        CosineSum += DotProduct(SurfaceNormal, EdgeNormal) * Angle;
    }
    // The sign of the boundary sum follows the polygon winding; irradiance is symmetric under reversal.
    return Math.abs(0.5 * CosineSum);
}

function CollectRectCorners(Luminaire, SurfacePoint)
{
    const Corners = [];
    const SignSequence = [ [ -1.0, -1.0 ], [ 1.0, -1.0 ], [ 1.0, 1.0 ], [ -1.0, 1.0 ] ];
    for (let CornerIndex = 0; CornerIndex < 4; CornerIndex += 1)
    {
        const SignU = SignSequence[CornerIndex][0];
        const SignV = SignSequence[CornerIndex][1];
        Corners.push([
            Luminaire.Center[0] + Luminaire.HalfAxisU[0] * SignU + Luminaire.HalfAxisV[0] * SignV - SurfacePoint[0],
            Luminaire.Center[1] + Luminaire.HalfAxisU[1] * SignU + Luminaire.HalfAxisV[1] * SignV - SurfacePoint[1],
            Luminaire.Center[2] + Luminaire.HalfAxisU[2] * SignU + Luminaire.HalfAxisV[2] * SignV - SurfacePoint[2],
        ]);
    }
    return Corners;
}

// Visibility of a polygonal luminaire: the emitter is sampled on a fixed grid and each sub-sample gets its own cone-traced
// shadow ray. A single centre ray cannot represent the visible fraction of a large panel and under-estimates it measurably.
function EvaluatePolygonVisibility(Studio, RepresentationName, OffsetOrigin, Luminaire, SurfacePoint, GridSide)
{
    const Tangent = Luminaire.HalfAxisU;
    const Bitangent = Luminaire.HalfAxisV;
    let VisibilitySum = 0.0;
    for (let GridX = 0; GridX < GridSide; GridX += 1)
    {
        for (let GridY = 0; GridY < GridSide; GridY += 1)
        {
            const UniformU = -1.0 + 2.0 * ((GridX + 0.5) / GridSide);
            const UniformV = -1.0 + 2.0 * ((GridY + 0.5) / GridSide);
            const SamplePoint = [
                Luminaire.Center[0] + Tangent[0] * UniformU + Bitangent[0] * UniformV,
                Luminaire.Center[1] + Tangent[1] * UniformU + Bitangent[1] * UniformV,
                Luminaire.Center[2] + Tangent[2] * UniformU + Bitangent[2] * UniformV,
            ];
            const ShadowOffset = SubtractVector(SamplePoint, SurfacePoint);
            const ShadowDistance = Math.max(VectorLength(ShadowOffset), 1.0e-4);
            // Coverage of a large emitter comes from the grid, so each sub-ray uses a thin cone. A fat cone here would
            // count the penumbra twice and systematically darken the panel's near field.
            VisibilitySum += SignedDistanceVisibility(Studio, RepresentationName, OffsetOrigin,
                ScaleVector(ShadowOffset, 1.0 / ShadowDistance), ShadowDistance - 1.0e-3, DeterministicConeSpread);
        }
    }
    return VisibilitySum / (GridSide * GridSide);
}

// Visibility of a curved area luminaire using the same distributed-sampling rule on a fixed angular grid.
function EvaluateCurvedVisibility(Studio, RepresentationName, OffsetOrigin, Luminaire, SurfacePoint, SampleCount)
{
    const Draw = ConstructXorShiftSeed(0x6D2E91AF);
    const SamplePoint = [ 0.0, 0.0, 0.0 ];
    DrawLuminairePoint(Luminaire, Draw, SamplePoint);
    let VisibilitySum = 0.0;
    for (let SampleIndex = 0; SampleIndex < SampleCount; SampleIndex += 1)
    {
        DrawLuminairePoint(Luminaire, Draw, SamplePoint);
        const ShadowOffset = SubtractVector(SamplePoint, SurfacePoint);
        const ShadowDistance = Math.max(VectorLength(ShadowOffset), 1.0e-4);
        VisibilitySum += SignedDistanceVisibility(Studio, RepresentationName, OffsetOrigin,
            ScaleVector(ShadowOffset, 1.0 / ShadowDistance), ShadowDistance - 1.0e-3, DeterministicConeSpread);
    }
    return VisibilitySum / SampleCount;
}

function EvaluateDeterministicGlossyQuadrature(Luminaire, SurfacePoint, SurfaceNormal, ViewDirection, Surface, FillResult)
{
    const GridSide = 4;
    const Tangent = Luminaire.HalfAxisU;
    const Bitangent = Luminaire.HalfAxisV;
    const QuadratureWeight = Luminaire.Area / (GridSide * GridSide);
    const SamplePoint = [ 0.0, 0.0, 0.0 ];
    FillResult[0] = 0.0; FillResult[1] = 0.0; FillResult[2] = 0.0;

    for (let GridX = 0; GridX < GridSide; GridX += 1)
    {
        for (let GridY = 0; GridY < GridSide; GridY += 1)
        {
            const UniformU = -1.0 + 2.0 * ((GridX + 0.5) / GridSide);
            const UniformV = -1.0 + 2.0 * ((GridY + 0.5) / GridSide);
            SamplePoint[0] = Luminaire.Center[0] + Tangent[0] * UniformU + Bitangent[0] * UniformV;
            SamplePoint[1] = Luminaire.Center[1] + Tangent[1] * UniformU + Bitangent[1] * UniformV;
            SamplePoint[2] = Luminaire.Center[2] + Tangent[2] * UniformU + Bitangent[2] * UniformV;
            const Offset = SubtractVector(SamplePoint, SurfacePoint);
            const DistanceSquared = Math.max(DotProduct(Offset, Offset), 1.0e-4);
            const InverseDistance = 1.0 / Math.sqrt(DistanceSquared);
            const Direction = ScaleVector(Offset, InverseDistance);
            const CosineLight = DotProduct(SurfaceNormal, Direction);
            const CosineLuminaire = -DotProduct(Luminaire.Normal, Direction);
            if (CosineLight <= 0.0 || CosineLuminaire <= 0.0)
            {
                continue;
            }
            ComputeBrdfComponents(Surface, SurfaceNormal, ViewDirection, Direction, ScratchDiffuse, ScratchSpecular);
            const GeometryWeight = QuadratureWeight * CosineLight * CosineLuminaire / DistanceSquared;
            for (let Channel = 0; Channel < 3; Channel += 1)
            {
                FillResult[Channel] += Luminaire.Radiance[Channel] * ScratchSpecular[Channel] * GeometryWeight;
            }
        }
    }
    return FillResult;
}

function EvaluateCurvedAreaQuadrature(Luminaire, SurfacePoint, SurfaceNormal, ViewDirection, Surface, SampleCount, FillResult)
{
    FillResult[0] = 0.0; FillResult[1] = 0.0; FillResult[2] = 0.0;
    const Draw = ConstructXorShiftSeed(0x4C1B2D77);
    const SamplePoint = [ 0.0, 0.0, 0.0 ];
    const MeasureDensity = DrawLuminairePoint(Luminaire, Draw, SamplePoint);
    const QuadratureWeight = 1.0 / (MeasureDensity * SampleCount);
    for (let SampleIndex = 0; SampleIndex < SampleCount; SampleIndex += 1)
    {
        DrawLuminairePoint(Luminaire, Draw, SamplePoint);
        const Offset = SubtractVector(SamplePoint, SurfacePoint);
        const DistanceSquared = Math.max(DotProduct(Offset, Offset), 1.0e-4);
        const InverseDistance = 1.0 / Math.sqrt(DistanceSquared);
        const Direction = ScaleVector(Offset, InverseDistance);
        const CosineLight = DotProduct(SurfaceNormal, Direction);
        const CosineLuminaire = -DotProduct(Luminaire.Normal === undefined ? Direction : Luminaire.Normal, Direction);
        if (CosineLight <= 0.0 || CosineLuminaire <= 0.0)
        {
            continue;
        }
        ComputeBrdfComponents(Surface, SurfaceNormal, ViewDirection, Direction, ScratchDiffuse, ScratchSpecular);
        const GeometryWeight = QuadratureWeight * CosineLight * CosineLuminaire / DistanceSquared;
        for (let Channel = 0; Channel < 3; Channel += 1)
        {
            FillResult[Channel] += Luminaire.Radiance[Channel] * (ScratchDiffuse[Channel] + ScratchSpecular[Channel]) * GeometryWeight;
        }
    }
    return FillResult;
}

// Radiance of the environment behind the environment: zero here, the analytic sky is added once outside the loop.

//------------------------------------------------------------------------------------------------------------------------
//                                                       STUDIO BUILD
//------------------------------------------------------------------------------------------------------------------------

export function ConstructStudio(PresetName, LampScale)
{
    const Preset = StudioPresets.find((Entry) => Entry.Identifier === PresetName) || StudioPresets[0];
    const Scene = Preset.Construct(LampScale || 1.0);
    const Studio = {
        Identifier: Preset.Identifier,
        Caption: Preset.Caption,
        Prisms: Scene.Prisms,
        Orbs: Scene.Orbs,
        Lights: Scene.Lights,
        Environment: Scene.Environment,
        Camera: Scene.Camera,
        Harmonics: ConstructEnvironmentHarmonics(Scene.Environment),
    };
    // Emissive solids are light sources first: they must not block the illumination they produce. Visibility therefore
    // runs against the non-emissive subset, while camera and reflection rays still intersect every prism.
    Studio.OcclusionPrisms = Scene.Prisms.filter((Primitive) => !Primitive.Material.Emissive);
    Studio.OcclusionOrbs = Scene.Orbs.filter((Primitive) => !Primitive.Material.Emissive);
    Studio.Tree = ConstructLightTree(Scene.Lights);
    return Studio;
}

export function ConstructCameraRay(Studio, PixelX, PixelY, Width, Height)
{
    const Camera = Studio.Camera;
    const Forward = NormalizeVector(SubtractVector(Camera.Target, Camera.Eye));
    const Right = NormalizeVector(CrossProduct(Forward, [ 0.0, 1.0, 0.0 ]));
    const Up = CrossProduct(Right, Forward);
    const Aspect = Width / Height;
    const TanHalf = Math.tan((Camera.FieldOfView * Pi / 180.0) * 0.5);
    const ScreenX = ((PixelX + 0.5) / Width * 2.0 - 1.0) * TanHalf * Aspect;
    const ScreenY = (1.0 - (PixelY + 0.5) / Height * 2.0) * TanHalf;
    return {
        Origin: Camera.Eye,
        Direction: NormalizeVector([
            Forward[0] + Right[0] * ScreenX + Up[0] * ScreenY,
            Forward[1] + Right[1] * ScreenX + Up[1] * ScreenY,
            Forward[2] + Right[2] * ScreenX + Up[2] * ScreenY,
        ]),
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHADING INTERFACE
//------------------------------------------------------------------------------------------------------------------------

const ScratchShadowContribution = new Float64Array(3);
const ScratchGlossy = new Float64Array(3);

export function ShadeSurface(Studio, Options, RayOrigin, RayDirection, HitDistance, HitNormal, Material, Draw)
{
    const SurfacePoint = [
        RayOrigin[0] + RayDirection[0] * HitDistance,
        RayOrigin[1] + RayDirection[1] * HitDistance,
        RayOrigin[2] + RayDirection[2] * HitDistance,
    ];
    const OffsetOrigin = [
        SurfacePoint[0] + HitNormal[0] * SurfaceBias,
        SurfacePoint[1] + HitNormal[1] * SurfaceBias,
        SurfacePoint[2] + HitNormal[2] * SurfaceBias,
    ];
    const ViewDirection = ScaleVector(RayDirection, -1.0);
    const Surface = { Albedo: Material.Albedo, Roughness: Material.Roughness, Metallic: Material.Metallic };
    const Result = [ Material.Emission[0], Material.Emission[1], Material.Emission[2] ];

    const Ambient = EvaluateIrradianceHarmonics(Studio.Harmonics, HitNormal);
    const AmbientScale = (1.0 - Material.Metallic) / Pi;
    Result[0] += Ambient[0] * Material.Albedo[0] * AmbientScale;
    Result[1] += Ambient[1] * Material.Albedo[1] * AmbientScale;
    Result[2] += Ambient[2] * Material.Albedo[2] * AmbientScale;

    const ReflectionDirection = ReflectVector(RayDirection, HitNormal);
    const EnvironmentReflection = EvaluateEnvironmentRgb(Studio.Environment, ReflectionDirection);
    const SchlickFresnel = 0.04 + 0.96 * Math.pow(1.0 - Math.max(DotProduct(HitNormal, ViewDirection), 0.0), 5.0);
    const ReflectionWeight = SchlickFresnel * (1.0 - Material.Roughness * 0.75);
    Result[0] += EnvironmentReflection[0] * ReflectionWeight;
    Result[1] += EnvironmentReflection[1] * ReflectionWeight;
    Result[2] += EnvironmentReflection[2] * ReflectionWeight;

    if (Options.Technique === Technique.DeterministicPolygon && Options.EnableDirectLuminaires !== false)
    {
        for (let LuminaireIndex = 0; LuminaireIndex < Studio.Lights.length; LuminaireIndex += 1)
        {
            const Luminaire = Studio.Lights[LuminaireIndex];
            let Visibility;
            if (Luminaire.Kind === LightKind.Rect)
            {
                Visibility = EvaluatePolygonVisibility(Studio, Options.Representation, OffsetOrigin, Luminaire, SurfacePoint, 3);
            }
            else if (Luminaire.Kind === LightKind.Disc || Luminaire.Kind === LightKind.Sphere)
            {
                Visibility = EvaluateCurvedVisibility(Studio, Options.Representation, OffsetOrigin, Luminaire, SurfacePoint, 4);
            }
            else
            {
                const LuminaireOffset = SubtractVector(Luminaire.Center, SurfacePoint);
                const LuminaireDistance = Math.max(VectorLength(LuminaireOffset), 1.0e-4);
                const LuminaireDirection = Luminaire.Kind === LightKind.Sun
                    ? Luminaire.Direction : ScaleVector(LuminaireOffset, 1.0 / LuminaireDistance);
                Visibility = SignedDistanceVisibility(Studio, Options.Representation, OffsetOrigin, LuminaireDirection,
                    Math.max(LuminaireDistance - 1.0e-3, 1.0e-4), ComputeTangentSpread(Luminaire, LuminaireDistance));
            }

            if (Luminaire.Kind === LightKind.Rect)
            {
                const Irradiance = EvaluatePolygonIrradiance(CollectRectCorners(Luminaire, SurfacePoint), HitNormal);
                EvaluateDeterministicGlossyQuadrature(Luminaire, SurfacePoint, HitNormal, ViewDirection, Surface, ScratchGlossy);
                // The diffuse response is evaluated at the emitter centroid so the closed-form irradiance and the stochastic
                // path share one BRDF convention instead of drifting apart by a Fresnel factor.
                const CentroidDirection = NormalizeVector(SubtractVector(Luminaire.Center, SurfacePoint));
                ComputeBrdfComponents(Surface, HitNormal, ViewDirection, CentroidDirection, ScratchDiffuse, ScratchSpecular);
                for (let Channel = 0; Channel < 3; Channel += 1)
                {
                    Result[Channel] += Visibility * (Luminaire.Radiance[Channel] * Irradiance * ScratchDiffuse[Channel] + ScratchGlossy[Channel]);
                }
                continue;
            }

            if (Luminaire.Kind === LightKind.Disc || Luminaire.Kind === LightKind.Sphere)
            {
                EvaluateCurvedAreaQuadrature(Luminaire, SurfacePoint, HitNormal, ViewDirection, Surface, 64, ScratchGlossy);
                Result[0] += ScratchGlossy[0] * Visibility;
                Result[1] += ScratchGlossy[1] * Visibility;
                Result[2] += ScratchGlossy[2] * Visibility;
                continue;
            }

            EvaluateLuminaireContribution(Luminaire, SurfacePoint, HitNormal, ViewDirection, Surface, null, ScratchShadowContribution);
            Result[0] += ScratchShadowContribution[0] * Visibility;
            Result[1] += ScratchShadowContribution[1] * Visibility;
            Result[2] += ScratchShadowContribution[2] * Visibility;
        }
        return Result;
    }

    const Resampled = ResampleCandidates(Studio.Tree, SurfacePoint, HitNormal, ViewDirection, Surface,
        Math.max(1, Options.CandidateCount | 0), Draw, Options.Technique, Options.SharedLuminaireIndices);

    if (Resampled.Selected === null)
    {
        return Result;
    }

    const Selected = Resampled.Selected;
    const Luminaire = Studio.Lights[Selected.LuminaireIndex];
    const ResamplingWeight = Resampled.WeightSum / (Resampled.SampleCount * Selected.TargetDensity);
    if (Options.EnableFireflyClamp !== false)
    {
        // Clamping the final contribution bounds rare near-field outliers at the cost of a tiny, documented bias. It is the
        // standard production guard when the emitter can come within centimetres of the shaded point.
        const ClampScale = FireflyClampLuminance / Math.max(Selected.TargetDensity * ResamplingWeight, 1.0e-9);
        if (ClampScale < 1.0)
        {
            Resampled.ClampScale = ClampScale;
        }
    }

    const ShadowOffset = SubtractVector(Selected.SamplePoint === null ? Luminaire.Center : Selected.SamplePoint, SurfacePoint);
    const ShadowDistance = VectorLength(ShadowOffset);
    const TangentSpread = ComputeShadowTangentSpread(Luminaire, ShadowDistance, Surface);
    const ShadowDirection = NormalizeVector(ShadowOffset);
    const Visibility = SignedDistanceVisibility(Studio, Options.Representation, OffsetOrigin, ShadowDirection,
        Math.max(ShadowDistance - 1.0e-3, 1.0e-4), TangentSpread);

    EvaluateLuminaireContribution(Luminaire, SurfacePoint, HitNormal, ViewDirection, Surface, Selected.SamplePoint, ScratchShadowContribution);
    const ScaleFactor = Visibility * ResamplingWeight * (Resampled.ClampScale === undefined ? 1.0 : Resampled.ClampScale);
    Result[0] += ScratchShadowContribution[0] * ScaleFactor;
    Result[1] += ScratchShadowContribution[1] * ScaleFactor;
    Result[2] += ScratchShadowContribution[2] * ScaleFactor;
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         INDEPENDENT UNIFORM-SAMPLING REFERENCE
//------------------------------------------------------------------------------------------------------------------------
// Deliberately shares no machinery with the deterministic tier: area luminaires are integrated by uniform area sampling with
// binary visibility per sample, punctual luminaires are evaluated exactly once. Returns direct luminaire lighting only.

export function IntegrateUniformReferencePoint(Studio, SurfacePoint, SurfaceNormal, ViewDirection, Surface, SampleCount)
{
    const Draw = ConstructXorShiftSeed(0x9C4E2A11);
    const OffsetOrigin = [
        SurfacePoint[0] + SurfaceNormal[0] * SurfaceBias,
        SurfacePoint[1] + SurfaceNormal[1] * SurfaceBias,
        SurfacePoint[2] + SurfaceNormal[2] * SurfaceBias,
    ];
    const Sum = [ 0.0, 0.0, 0.0 ];
    const SamplePoint = [ 0.0, 0.0, 0.0 ];
    const Contribution = new Float64Array(3);

    for (let LuminaireIndex = 0; LuminaireIndex < Studio.Lights.length; LuminaireIndex += 1)
    {
        const Luminaire = Studio.Lights[LuminaireIndex];
        if (Luminaire.MeasureKind !== 'Area')
        {
            EvaluateLuminaireContribution(Luminaire, SurfacePoint, SurfaceNormal, ViewDirection, Surface, null, Contribution);
            const Offset = SubtractVector(Luminaire.Center, SurfacePoint);
            const Distance = VectorLength(Offset);
            const Direction = Luminaire.Kind === LightKind.Sun ? Luminaire.Direction : ScaleVector(Offset, 1.0 / Math.max(Distance, 1.0e-6));
            const Visibility = 1.0 - TraceOcclusion(Studio, OffsetOrigin, Direction, Math.max(Distance - 1.0e-3, 1.0e-4));
            Sum[0] += Contribution[0] * Visibility;
            Sum[1] += Contribution[1] * Visibility;
            Sum[2] += Contribution[2] * Visibility;
            continue;
        }

        let Accumulated = 0.0;
        for (let SampleIndex = 0; SampleIndex < SampleCount; SampleIndex += 1)
        {
            DrawLuminairePoint(Luminaire, Draw, SamplePoint);
            EvaluateLuminaireContribution(Luminaire, SurfacePoint, SurfaceNormal, ViewDirection, Surface, SamplePoint, Contribution);
            const Offset = SubtractVector(SamplePoint, SurfacePoint);
            const Distance = VectorLength(Offset);
            const Visibility = 1.0 - TraceOcclusion(Studio, OffsetOrigin, ScaleVector(Offset, 1.0 / Math.max(Distance, 1.0e-6)),
                Math.max(Distance - 1.0e-3, 1.0e-4));
            if (Visibility <= 0.0)
            {
                continue;
            }
            Sum[0] += Contribution[0] * Luminaire.Area / SampleCount;
            Sum[1] += Contribution[1] * Luminaire.Area / SampleCount;
            Sum[2] += Contribution[2] * Luminaire.Area / SampleCount;
            Accumulated += 1.0;
        }
        void Accumulated;
    }
    return Sum;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    IMAGE INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

export function IntegrateRegion(Studio, Options)
{
    const StartX = Options.StartX | 0;
    const StartY = Options.StartY | 0;
    const EndX = Options.EndX === undefined ? (Options.Width | 0) : Math.min(Options.EndX | 0, Options.Width | 0);
    const EndY = Options.EndY === undefined ? (Options.Height | 0) : Math.min(Options.EndY | 0, Options.Height | 0);
    const Width = Options.Width | 0;
    const Height = Options.Height | 0;
    const SampleIndex = Options.SampleIndex | 0;
    const PixelCount = Math.max(0, (EndX - StartX) * (EndY - StartY));
    const Pixels = new Float32Array(PixelCount * 3);
    const SharedCutSize = Math.max(1, (Options.SharedCutSize | 0) || 6);
    const SharedBlockSide = Math.max(1, (Options.SharedBlockSide | 0) || 8);
    const SharedCutStore = new Map();
    const SharedIndices = Options.Technique === Technique.SharedLightTreeCut ? [] : null;

    let WriteIndex = 0;
    for (let PixelY = StartY; PixelY < EndY; PixelY += 1)
    {
        for (let PixelX = StartX; PixelX < EndX; PixelX += 1)
        {
            const Draw = ConstructXorShiftSeed(((PixelY * Width + PixelX) * 1973 + SampleIndex * 9277 + 12345) >>> 0);
            const CameraRay = ConstructCameraRay(Studio, PixelX, PixelY, Width, Height);
            const SceneHit = TraceScene(Studio, CameraRay.Origin, CameraRay.Direction);

            if (SceneHit === null)
            {
                const EnvironmentColor = EvaluateEnvironmentRgb(Studio.Environment, CameraRay.Direction);
                Pixels[WriteIndex + 0] = EnvironmentColor[0];
                Pixels[WriteIndex + 1] = EnvironmentColor[1];
                Pixels[WriteIndex + 2] = EnvironmentColor[2];
                WriteIndex += 3;
                continue;
            }

            Options.SharedLuminaireIndices = null;
            if (SharedIndices !== null)
            {
                const BlockKey = Math.floor(PixelY / SharedBlockSide) * 8192 + Math.floor(PixelX / SharedBlockSide);
                let BlockCut = SharedCutStore.get(BlockKey);
                if (BlockCut === undefined)
                {
                    // The cut is built from the block-centre hit so the whole block shares one adaptive luminaire set.
                    const BlockRay = ConstructCameraRay(Studio,
                        (Math.floor(PixelX / SharedBlockSide) + 0.5) * SharedBlockSide - 0.5,
                        (Math.floor(PixelY / SharedBlockSide) + 0.5) * SharedBlockSide - 0.5, Width, Height);
                    const BlockHit = TraceScene(Studio, BlockRay.Origin, BlockRay.Direction);
                    if (BlockHit === null)
                    {
                        BlockCut = [ 0 ];
                    }
                    else
                    {
                        const BlockPoint = [
                            BlockRay.Origin[0] + BlockRay.Direction[0] * BlockHit.Distance,
                            BlockRay.Origin[1] + BlockRay.Direction[1] * BlockHit.Distance,
                            BlockRay.Origin[2] + BlockRay.Direction[2] * BlockHit.Distance,
                        ];
                        const CutDraw = ConstructXorShiftSeed(((BlockKey * 7919 + SampleIndex * 104729 + 17) >>> 0));
                        BlockCut = ConstructSharedCut(Studio.Tree, BlockPoint, BlockHit.Normal, ScaleVector(BlockRay.Direction, -1.0),
                            { Albedo: BlockHit.Material.Albedo, Roughness: BlockHit.Material.Roughness, Metallic: BlockHit.Material.Metallic },
                            SharedCutSize, CutDraw);
                    }
                    SharedCutStore.set(BlockKey, BlockCut);
                }
                Options.SharedLuminaireIndices = BlockCut;
            }

            const Shaded = ShadeSurface(Studio, Options, CameraRay.Origin, CameraRay.Direction,
                SceneHit.Distance, SceneHit.Normal, SceneHit.Material, Draw);
            Pixels[WriteIndex + 0] = Shaded[0];
            Pixels[WriteIndex + 1] = Shaded[1];
            Pixels[WriteIndex + 2] = Shaded[2];
            WriteIndex += 3;
        }
    }
    return Pixels;
}

export function IntegrateFullImage(Studio, Options)
{
    const Merged = Object.assign({}, Options, { StartX: 0, StartY: 0, EndX: Options.Width, EndY: Options.Height });
    Merged.SharedLuminaireIndices = null;
    return IntegrateRegion(Studio, Merged);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TONE TRANSFORM
//------------------------------------------------------------------------------------------------------------------------

export function ToneMapToBytes(LinearPixels, Exposure)
{
    const ByteCount = (LinearPixels.length / 3) | 0;
    const Bytes = new Uint8ClampedArray(ByteCount * 3);
    const ExposureScale = Math.pow(2.0, Exposure);
    for (let PixelIndex = 0; PixelIndex < ByteCount; PixelIndex += 1)
    {
        for (let Channel = 0; Channel < 3; Channel += 1)
        {
            const ScalarReading = LinearPixels[PixelIndex * 3 + Channel] * ExposureScale;
            const Mapped = ScalarReading / (1.0 + ScalarReading);
            Bytes[PixelIndex * 3 + Channel] = Math.round(255.0 * Math.pow(Clamp(Mapped, 0.0, 1.0), 1.0 / 2.2));
        }
    }
    return Bytes;
}
