import React, { useId, useRef, useState } from 'react';
import { SunGizmo } from './environment-graphics.jsx';
import {
  SunTrajectoryIcon,
  SunIlluminanceIcon,
  SunTemperatureIcon,
  SunCycleIcon,
  SunDiscIcon,
  SunOpticsIcon,
  SunBakeIcon,
  SunCornerArrowIcon,
  SunChevronLeftIcon,
  SunChevronRightIcon,
  SunIconBadge,
  kelvinToCss,
  compassLabel,
  solarPhaseName,
  formatClockHours,
  skyPaletteForElevation,
  KELVIN_PRESETS,
  DIURNAL_PRESETS,
} from './sun-icons.jsx';

const CATEGORIES = [
  { id: 'trajectory', label: 'Trajectory', count: 2, icon: SunTrajectoryIcon, accent: '#eab965' },
  { id: 'photometry', label: 'Photometry', count: 3, icon: SunIlluminanceIcon, accent: '#f0ca72' },
  { id: 'subsystems', label: 'Subsystems', count: 4, icon: SunOpticsIcon, accent: '#e39e74' },
  { id: 'baking', label: 'Baking', count: 2, icon: SunBakeIcon, accent: '#9cc4b2' },
];

function SplitMetric({ value, decimals = 0, unit = '' }) {
  const num = Number(value) || 0;
  const formatted = num.toFixed(decimals);
  const [whole, frac] = formatted.split('.');
  const localeWhole =
    whole.startsWith('-')
      ? '−' + Math.abs(Number(whole)).toLocaleString('en-US')
      : Number(whole).toLocaleString('en-US');
  return (
    <div className="sun-bento-metric">
      <span className="sun-bento-metric__int">{localeWhole}</span>
      {decimals > 0 && <span className="sun-bento-metric__frac">.{frac}</span>}
      {unit && <small className="sun-bento-metric__unit">{unit}</small>}
    </div>
  );
}

function CapsuleToggle({ on, label, onToggle, disabled = false }) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={!!on}
      aria-label={label}
      disabled={disabled}
      className={`sun-capsule-toggle ${on ? 'is-on' : 'is-off'}`}
      onClick={onToggle}
    >
      <span className="sun-capsule-toggle__thumb" />
    </button>
  );
}

function SolarSkyBanner({ azimuth, elevation, temperature, time, onScrubTime }) {
  const uid = useId().replace(/:/g, '');
  const dragRef = useRef(null);
  const { zenith, horizon } = skyPaletteForElevation(elevation);
  const apparentKelvin =
    elevation < 12
      ? temperature + (1850 - temperature) * Math.pow(Math.max(0, Math.min(1, (12 - elevation) / 18)), 0.72)
      : temperature;
  const discColor = kelvinToCss(apparentKelvin);
  const discGlow = kelvinToCss(apparentKelvin, elevation < 0 ? 0.2 : 0.48);
  const sunX = 48 + ((((Number(azimuth) || 0) % 360) + 360) % 360) / 360 * 524;
  const normAlt = Math.max(-0.32, Math.min(1, (Number(elevation) || 0) / 68));
  const horizonY = 104;
  const sunY = horizonY - normAlt * 72;
  const clock = formatClockHours(time);
  const phase = solarPhaseName(elevation);

  const scrub = (clientX, rect) => {
    const t = Math.max(0, Math.min(24, ((clientX - rect.left) / Math.max(1, rect.width)) * 24));
    const rounded = Math.round(t * 2) / 2;
    const solvedElev = Math.round(62 * Math.sin(((rounded - 6) / 12) * Math.PI));
    const solvedAz = Math.round((((90 + (rounded - 6) * 15) % 360) + 360) % 360);
    onScrubTime(rounded, solvedAz, Math.max(-90, Math.min(90, solvedElev)));
  };

  return (
    <div className="sun-sky-banner">
      <div
        className="sun-sky-banner__stage"
        title="Drag horizontally across the sky to scrub solar time"
        onPointerDown={(e) => {
          e.currentTarget.setPointerCapture(e.pointerId);
          dragRef.current = true;
          scrub(e.clientX, e.currentTarget.getBoundingClientRect());
        }}
        onPointerMove={(e) => {
          if (dragRef.current) scrub(e.clientX, e.currentTarget.getBoundingClientRect());
        }}
        onPointerUp={() => {
          dragRef.current = false;
        }}
        onPointerCancel={() => {
          dragRef.current = false;
        }}
      >
        <svg viewBox="0 0 620 132" preserveAspectRatio="none" aria-hidden="true">
          <defs>
            <linearGradient id={`${uid}-sky`} x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor={zenith} />
              <stop offset="100%" stopColor={horizon} />
            </linearGradient>
            <radialGradient id={`${uid}-bloom`} cx="50%" cy="50%" r="50%">
              <stop offset="0%" stopColor={discGlow} />
              <stop offset="100%" stopColor="rgba(0,0,0,0)" />
            </radialGradient>
            <linearGradient id={`${uid}-ground`} x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor={kelvinToCss(apparentKelvin, elevation > 0 ? 0.16 : 0.04)} />
              <stop offset="100%" stopColor="rgba(9,10,13,0.96)" />
            </linearGradient>
          </defs>
          <rect x="0" y="0" width="620" height={horizonY} fill={`url(#${uid}-sky)`} />
          {elevation < -2 &&
            Array.from({ length: 28 }, (_, i) => (
              <circle
                key={i}
                cx={18 + ((i * 173) % 584)}
                cy={10 + ((i * 67) % 82)}
                r={i % 3 === 0 ? 1.25 : 0.85}
                fill="#dbe6ff"
                opacity={Math.min(0.6, (-elevation / 24) * (0.25 + (i % 4) * 0.12))}
              />
            ))}
          <circle cx={sunX} cy={sunY} r="54" fill={`url(#${uid}-bloom)`} />
          {sunY < horizonY + 12 && (
            <>
              <circle cx={sunX} cy={sunY} r="9.5" fill={discColor} />
              <circle cx={sunX} cy={sunY} r="5.2" fill="#fffdf6" opacity="0.65" />
            </>
          )}
          <rect x="0" y={horizonY} width="620" height={132 - horizonY} fill="#0a0b0e" />
          <rect x="0" y={horizonY} width="620" height={132 - horizonY} fill={`url(#${uid}-ground)`} />
          <line x1="0" y1={horizonY} x2="620" y2={horizonY} stroke="rgba(255,255,255,0.14)" strokeWidth="1" />
        </svg>
        <div className="sun-sky-banner__hud">
          <div>
            <span className="sun-sky-banner__phase">{phase}</span>
            <span className="sun-sky-banner__meta">
              {clock.text} · {Number(temperature).toLocaleString('en-US')} K
            </span>
          </div>
          <span className="sun-sky-banner__bearing">
            {elevation >= 0 ? `+${elevation}°` : `−${Math.abs(elevation)}°`} · {compassLabel(azimuth)} {azimuth}°
          </span>
        </div>
      </div>

      <div className="sun-preset-carousel" role="group" aria-label="Solar time presets">
        {DIURNAL_PRESETS.map((preset) => {
          const active = Math.abs((Number(time) || 0) - preset.time) <= 0.75;
          return (
            <button
              key={preset.id}
              type="button"
              className={`sun-preset-pill ${active ? 'is-active' : ''}`}
              aria-pressed={active}
              onClick={() => onScrubTime(preset.time, preset.azimuth, preset.elevation)}
            >
              <i
                style={{
                  background:
                    preset.elevation < 0
                      ? '#7f93c8'
                      : preset.elevation < 12
                        ? '#f09e54'
                        : '#f2cf7d',
                }}
              />
              <span>{preset.label}</span>
              <small>{formatClockHours(preset.time).text}</small>
            </button>
          );
        })}
      </div>
    </div>
  );
}

function DiurnalChart({ time }) {
  const uid = useId().replace(/:/g, '');
  const heightAt = (t) => 64 - 44 * Math.sin(((t - 6) / 24) * Math.PI * 2);
  const cx = 20 + (time / 24) * 460;
  const cy = heightAt(time);
  const fullPath = Array.from(
    { length: 97 },
    (_, i) => `${i ? 'L' : 'M'}${(20 + (i / 96) * 460).toFixed(1)} ${heightAt(i / 4).toFixed(1)}`,
  ).join(' ');
  const dayPath = Array.from(
    { length: 49 },
    (_, i) => `${i ? 'L' : 'M'}${(135 + (i / 48) * 230).toFixed(1)} ${heightAt(6 + i / 4).toFixed(1)}`,
  ).join(' ');
  const clock = formatClockHours(time);
  const isNight = time < 6 || time >= 18;
  const pillX = Math.max(46, Math.min(454, cx));

  return (
    <svg
      className="sun-telemetry-svg"
      viewBox="0 0 500 138"
      role="img"
      aria-label={`24-hour daylight cycle, selected time ${clock.text}`}
    >
      <defs>
        <pattern id={`${uid}-hatch`} width="6" height="6" patternTransform="rotate(45)" patternUnits="userSpaceOnUse">
          <line x1="0" y1="0" x2="0" y2="6" stroke="rgba(255,255,255,0.06)" strokeWidth="1.5" />
        </pattern>
        <linearGradient id={`${uid}-dayfill`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="rgba(236,192,107,0.22)" />
          <stop offset="100%" stopColor="rgba(236,192,107,0)" />
        </linearGradient>
      </defs>
      <rect x="20" y="12" width="115" height="94" rx="6" fill={`url(#${uid}-hatch)`} />
      <rect x="365" y="12" width="115" height="94" rx="6" fill={`url(#${uid}-hatch)`} />
      <line x1="20" y1="64" x2="480" y2="64" stroke="rgba(255,255,255,0.16)" strokeDasharray="4 4" />
      <path d={`${dayPath} L365 64 L135 64 Z`} fill={`url(#${uid}-dayfill)`} />
      <path d={fullPath} stroke="rgba(255,255,255,0.28)" fill="none" strokeWidth="1.35" />
      <path d={dayPath} stroke="#ecc371" fill="none" strokeWidth="2" strokeLinecap="round" />
      <line x1={cx} y1="12" x2={cx} y2="108" stroke="rgba(255,255,255,0.38)" strokeWidth="1" />
      <circle cx={cx} cy={cy} r="10" fill={isNight ? '#8ba2dc' : '#f2c86c'} opacity="0.18" />
      <circle cx={cx} cy={cy} r="4.2" fill={isNight ? '#b9c9f0' : '#fff3d1'} stroke="#121316" strokeWidth="1.4" />
      <g fill="rgba(255,255,255,0.36)" fontSize="9.5">
        <text x="20" y="126">00:00</text>
        <text x="135" y="126" textAnchor="middle">06:00</text>
        <text x="250" y="126" textAnchor="middle">12:00</text>
        <text x="365" y="126" textAnchor="middle">18:00</text>
        <text x="480" y="126" textAnchor="end">24:00</text>
      </g>
      <g transform={`translate(${pillX}, 122)`}>
        <rect x="-24" y="-10" width="48" height="18" rx="9" fill="#f3f4f6" />
        <text x="0" y="2.5" textAnchor="middle" fill="#101215" fontSize="9.5" fontWeight="600">
          {clock.text}
        </text>
      </g>
    </svg>
  );
}

function PhotometryChart({ intensity, temperature }) {
  const uid = useId().replace(/:/g, '');
  const t = Math.max(0, Math.min(1, (Number(intensity) || 0) / 150));
  const yAt = (u) => 84 - 62 * (1 - Math.cos((u * Math.PI) / 2));
  const pts = (end) =>
    Array.from({ length: 61 }, (_, i) => {
      const u = (end * i) / 60;
      return `${i ? 'L' : 'M'}${(16 + u * 328).toFixed(1)},${yAt(u).toFixed(1)}`;
    }).join(' ');
  const px = 16 + t * 328;
  const py = yAt(t);
  const targetY = yAt(110 / 150);
  const stroke = kelvinToCss(temperature);

  return (
    <svg
      className="sun-telemetry-svg"
      viewBox="0 0 360 108"
      role="img"
      aria-label={`Illuminance ${intensity} of 150 kilolux`}
    >
      <defs>
        <pattern id={`${uid}-hatch`} width="6" height="6" patternTransform="rotate(-45)" patternUnits="userSpaceOnUse">
          <line x1="0" y1="0" x2="0" y2="6" stroke="rgba(236,196,112,0.16)" strokeWidth="1.6" />
        </pattern>
      </defs>
      <line x1="16" y1={targetY} x2="344" y2={targetY} stroke="rgba(255,255,255,0.2)" strokeDasharray="4 4" />
      <text x="342" y={targetY - 5} textAnchor="end" fill="rgba(255,255,255,0.42)" fontSize="8.5">
        CLEAR SKY · 110 klux
      </text>
      <path d={pts(1)} fill="none" stroke="rgba(255,255,255,0.14)" strokeWidth="1.4" />
      {t > 0.01 && (
        <>
          <path d={`${pts(t)} L${px.toFixed(1)} 92 L16 92 Z`} fill={`url(#${uid}-hatch)`} />
          <path d={pts(t)} fill="none" stroke={stroke} strokeWidth="2.1" strokeLinecap="round" />
        </>
      )}
      <line x1={px} y1={py} x2={px} y2="92" stroke="rgba(255,255,255,0.32)" strokeDasharray="2 3" />
      <circle cx={px} cy={py} r="9" fill={stroke} opacity="0.2" />
      <circle cx={px} cy={py} r="4" fill="#ffffff" stroke="#121316" strokeWidth="1.4" />
    </svg>
  );
}

export default function SunInspector({
  v,
  set,
  slider,
  featureOn,
  lensFlareChild,
  onSelectChild,
  onCategoryChange,
}) {
  const [categoryIndex, setCategoryIndex] = useState(0);
  const [viewMode, setViewMode] = useState('carousel');
  const [photometrySlide, setPhotometrySlide] = useState(0);
  const swipeStart = useRef(null);

  const elevation = v('elevation');
  const azimuth = v('azimuth');
  const intensity = v('intensity');
  const temperature = v('temperature');
  const time = v('time');
  const discSize = v('discSize');

  const selectCategory = (idx) => {
    const wrapped = ((idx % CATEGORIES.length) + CATEGORIES.length) % CATEGORIES.length;
    setCategoryIndex(wrapped);
    onCategoryChange?.(viewMode === 'all' ? 'all' : CATEGORIES[wrapped].id);
  };

  const toggleViewMode = (mode) => {
    setViewMode(mode);
    onCategoryChange?.(mode === 'all' ? 'all' : CATEGORIES[categoryIndex].id);
  };

  const stepValue = (key, delta, min, max, decimals = 0) => {
    const next = Math.max(min, Math.min(max, Number(v(key)) + delta));
    set(key, Number(next.toFixed(decimals)));
  };

  const activeSubsystems = ['Illuminance', 'Sun disc', 'Daylight cycle'].filter((k) =>
    featureOn(k),
  ).length;
  const warmthRatio = Math.round(
    Math.max(8, Math.min(92, (1 - (temperature - 2000) / 8000) * 100)),
  );
  const clock = formatClockHours(time);

  const trajectorySection = (
    <div className="sun-slide-grid sun-slide-grid--two">
      {/* Card 1: Sun Direction */}
      <article className="sun-bento-card sun-bento-card--glow">
        <header className="sun-bento-card__head">
          <div className="sun-bento-card__title-group">
            <SunIconBadge icon={SunTrajectoryIcon} accent="#eab965" />
            <div>
              <h3>Sun direction</h3>
              <span className="sun-bento-card__sub">Azimuth & horizon altitude</span>
            </div>
          </div>
          <span className="sun-pill-tag">
            {compassLabel(azimuth)} · {azimuth}°
          </span>
        </header>

        <div className="sun-bento-card__kpi-row">
          <SplitMetric value={elevation} decimals={0} unit="°" />
          <span className={`sun-status-chip ${elevation < 0 ? 'is-muted' : 'is-positive'}`}>
            {elevation < 0 ? '▼ Below horizon' : '▲ Above horizon'}
          </span>
        </div>

        <div className="sun-gizmo-well">
          <SunGizmo
            azimuth={azimuth}
            elevation={elevation}
            onChange={(a, e) => {
              set('azimuth', a);
              set('elevation', e);
            }}
          />
        </div>

        <div className="sun-dual-controls">
          <div className="sun-axis-box">
            <div className="sun-axis-box__top">
              <span>Azimuth</span>
              <strong>
                {azimuth}
                <small>°</small>
              </strong>
            </div>
            {slider('azimuth', 0, 360)}
            <div className="sun-stepper-row">
              <button
                type="button"
                aria-label="Decrease azimuth"
                onClick={() => stepValue('azimuth', -5, 0, 360)}
              >
                −
              </button>
              <span>{compassLabel(azimuth)}</span>
              <button
                type="button"
                aria-label="Increase azimuth"
                onClick={() => stepValue('azimuth', 5, 0, 360)}
              >
                +
              </button>
            </div>
          </div>

          <div className="sun-axis-box">
            <div className="sun-axis-box__top">
              <span>Elevation</span>
              <strong>
                {elevation}
                <small>°</small>
              </strong>
            </div>
            {slider('elevation', -90, 90)}
            <div className="sun-stepper-row">
              <button
                type="button"
                aria-label="Decrease elevation"
                onClick={() => stepValue('elevation', -5, -90, 90)}
              >
                −
              </button>
              <span>{solarPhaseName(elevation)}</span>
              <button
                type="button"
                aria-label="Increase elevation"
                onClick={() => stepValue('elevation', 5, -90, 90)}
              >
                +
              </button>
            </div>
          </div>
        </div>
      </article>

      {/* Card 2: Daylight Cycle */}
      <article
        className={`sun-bento-card ${!featureOn('Daylight cycle') ? 'is-disabled-card' : ''}`}
      >
        <header className="sun-bento-card__head">
          <div className="sun-bento-card__title-group">
            <SunIconBadge icon={SunCycleIcon} accent="#d7be74" />
            <div>
              <h3>Daylight cycle</h3>
              <span className="sun-bento-card__sub">24-hour diurnal progression</span>
            </div>
          </div>
          <CapsuleToggle
            on={featureOn('Daylight cycle')}
            label="Toggle Day cycle"
            onToggle={() => set('feature:Daylight cycle', !featureOn('Daylight cycle'))}
          />
        </header>

        <fieldset
          className="sun-bento-fieldset"
          disabled={!featureOn('Daylight cycle')}
          inert={!featureOn('Daylight cycle') ? '' : undefined}
        >
          <div className="sun-bento-card__kpi-row">
            <div className="sun-bento-metric">
              <span className="sun-bento-metric__int">{clock.hh}</span>
              <span className="sun-bento-metric__frac">:{clock.mm}</span>
            </div>
            <span className="sun-pill-tag">
              {time < 6 || time >= 18
                ? 'Night'
                : time < 12
                  ? 'Morning'
                  : time < 17
                    ? 'Afternoon'
                    : 'Evening'}
            </span>
          </div>

          <DiurnalChart time={time} />

          {slider('time', 0, 24, 0.5)}

          <div className="sun-card-bottom-actions">
            <span className="sun-card-hint">Solar noon at 12:00 · 30m steps</span>
            <div className="sun-round-steppers">
              <button
                type="button"
                aria-label="Step time back 30 minutes"
                onClick={() => stepValue('time', -0.5, 0, 24, 1)}
              >
                −
              </button>
              <button
                type="button"
                aria-label="Step time forward 30 minutes"
                onClick={() => stepValue('time', 0.5, 0, 24, 1)}
              >
                +
              </button>
            </div>
          </div>
        </fieldset>
      </article>
    </div>
  );

  const photometryCards = [
    /* Photometry 0: Illuminance */
    <article
      key="illuminance"
      className={`sun-bento-card sun-bento-card--glow ${!featureOn('Illuminance') ? 'is-disabled-card' : ''}`}
    >
      <header className="sun-bento-card__head">
        <div className="sun-bento-card__title-group">
          <SunIconBadge icon={SunIlluminanceIcon} accent="#f0ca72" />
          <div>
            <h3>Illuminance</h3>
            <span className="sun-bento-card__sub">Direct beam ground flux</span>
          </div>
        </div>
        <CapsuleToggle
          on={featureOn('Illuminance')}
          label="Toggle Sunlight"
          onToggle={() => set('feature:Illuminance', !featureOn('Illuminance'))}
        />
      </header>

      <fieldset
        className="sun-bento-fieldset"
        disabled={!featureOn('Illuminance')}
        inert={!featureOn('Illuminance') ? '' : undefined}
      >
        <div className="sun-bento-card__kpi-row">
          <SplitMetric value={intensity} decimals={0} unit="klux" />
          <span className="sun-status-chip is-positive">
            {Math.round((intensity / 110) * 100)}% of clear noon
          </span>
        </div>

        <PhotometryChart intensity={intensity} temperature={temperature} />

        {slider('intensity', 0, 150)}
        <div className="sun-card-bottom-actions">
          <span className="sun-card-hint">0 – 150 klux · Radiometric direct</span>
          <div className="sun-round-steppers">
            <button
              type="button"
              aria-label="Decrease illuminance"
              onClick={() => stepValue('intensity', -5, 0, 150)}
            >
              −
            </button>
            <button
              type="button"
              aria-label="Increase illuminance"
              onClick={() => stepValue('intensity', 5, 0, 150)}
            >
              +
            </button>
          </div>
        </div>
      </fieldset>
    </article>,

    /* Photometry 1: Temperature */
    <article key="temperature" className="sun-bento-card">
      <header className="sun-bento-card__head">
        <div className="sun-bento-card__title-group">
          <SunIconBadge icon={SunTemperatureIcon} accent="#e89e76" />
          <div>
            <h3>Colour temperature</h3>
            <span className="sun-bento-card__sub">Planckian blackbody locus</span>
          </div>
        </div>
        <span className="sun-swatch-pill">
          <i style={{ background: kelvinToCss(temperature) }} />
          {temperature < 4500
            ? 'Golden warmth'
            : temperature > 7000
              ? 'Cool sky'
              : 'Natural daylight'}
        </span>
      </header>

      <div className="sun-bento-card__kpi-row">
        <SplitMetric value={temperature} decimals={0} unit="K" />
        <span className="sun-pill-tag">{(temperature / 1000).toFixed(1)} kK</span>
      </div>

      <div className="sun-kelvin-bar">
        {slider('temperature', 2000, 10000, 100)}
      </div>

      <div className="sun-preset-carousel sun-preset-carousel--compact" role="group" aria-label="Kelvin presets">
        {KELVIN_PRESETS.filter((p) => p.kelvin >= 2000).map((preset) => {
          const active = Math.abs(temperature - preset.kelvin) <= 250;
          return (
            <button
              key={preset.kelvin}
              type="button"
              className={`sun-preset-pill ${active ? 'is-active' : ''}`}
              onClick={() => set('temperature', preset.kelvin)}
            >
              <i style={{ background: kelvinToCss(preset.kelvin) }} />
              <span>{preset.label}</span>
              <small>{preset.tag}</small>
            </button>
          );
        })}
      </div>

      {/* Paired comparison blocks inspired by Reference Image 3 */}
      <div className="sun-balance-blocks">
        <div className="sun-balance-block sun-balance-block--hatched">
          <small>Cool scatter</small>
          <strong>{100 - warmthRatio}%</strong>
        </div>
        <div
          className="sun-balance-block sun-balance-block--solid"
          style={{ '--block-fill': kelvinToCss(temperature, 0.9) }}
        >
          <small>Warmth index</small>
          <strong>{warmthRatio}%</strong>
        </div>
      </div>
    </article>,

    /* Photometry 2: Sun Disc */
    <article
      key="disc"
      className={`sun-bento-card ${!featureOn('Sun disc') ? 'is-disabled-card' : ''}`}
    >
      <header className="sun-bento-card__head">
        <div className="sun-bento-card__title-group">
          <SunIconBadge icon={SunDiscIcon} accent="#f2be72" />
          <div>
            <h3>Sun disc</h3>
            <span className="sun-bento-card__sub">Visible limb & penumbra angle</span>
          </div>
        </div>
        <CapsuleToggle
          on={featureOn('Sun disc')}
          label="Toggle Sun disc"
          onToggle={() => set('feature:Sun disc', !featureOn('Sun disc'))}
        />
      </header>

      <fieldset
        className="sun-bento-fieldset"
        disabled={!featureOn('Sun disc')}
        inert={!featureOn('Sun disc') ? '' : undefined}
      >
        <div className="sun-disc-caliper">
          <div>
            <SplitMetric value={discSize} decimals={2} unit="°" />
            <p className="sun-card-hint">
              {(discSize / 0.53).toFixed(2)}× Earth solar subtense (0.53°)
            </p>
          </div>
          <div className="sun-disc-caliper__stage" aria-hidden="true">
            <span className="sun-disc-caliper__ref" title="0.53° Real Sun reference" />
            <i
              className="sun-disc-caliper__orb"
              style={{
                width: `${Math.min(76, 16 + discSize * 30)}px`,
                height: `${Math.min(76, 16 + discSize * 30)}px`,
                background: kelvinToCss(temperature),
                boxShadow: `0 0 24px ${kelvinToCss(temperature, 0.42)}`,
              }}
            />
          </div>
        </div>

        {slider('discSize', 0.1, 2, 0.01)}
        <div className="sun-card-bottom-actions">
          <span className="sun-card-hint">Pinpoint 0.10° → Soft 2.00°</span>
          <div className="sun-round-steppers">
            <button
              type="button"
              aria-label="Decrease sun disc size"
              onClick={() => stepValue('discSize', -0.05, 0.1, 2, 2)}
            >
              −
            </button>
            <button
              type="button"
              aria-label="Increase sun disc size"
              onClick={() => stepValue('discSize', 0.05, 0.1, 2, 2)}
            >
              +
            </button>
          </div>
        </div>
      </fieldset>
    </article>,
  ];

  const photometrySection = (
    <div className="sun-subcarousel">
      <div className="sun-subcarousel__bar">
        <div className="sun-subcarousel__pills" role="tablist" aria-label="Photometry cards">
          {['Illuminance', 'Temperature', 'Sun disc'].map((label, idx) => (
            <button
              key={label}
              type="button"
              role="tab"
              aria-selected={photometrySlide === idx}
              className={`sun-subcarousel__pill ${photometrySlide === idx ? 'is-active' : ''}`}
              onClick={() => setPhotometrySlide(idx)}
            >
              <span>0{idx + 1}</span> {label}
            </button>
          ))}
        </div>
        <div className="sun-subcarousel__arrows">
          <button
            type="button"
            aria-label="Previous photometry card"
            onClick={() => setPhotometrySlide((s) => (s + 2) % 3)}
          >
            <SunChevronLeftIcon size={15} />
          </button>
          <button
            type="button"
            aria-label="Next photometry card"
            onClick={() => setPhotometrySlide((s) => (s + 1) % 3)}
          >
            <SunChevronRightIcon size={15} />
          </button>
        </div>
      </div>

      <div className="sun-subcarousel__viewport">
        <div
          className="sun-subcarousel__track"
          style={{ '--sub-slide': photometrySlide }}
        >
          {photometryCards.map((cardEl, idx) => (
            <div
              key={idx}
              className={`sun-subcarousel__item ${photometrySlide === idx ? 'is-focused' : ''}`}
              onFocusCapture={() => setPhotometrySlide(idx)}
            >
              {cardEl}
            </div>
          ))}
        </div>
      </div>
    </div>
  );

  /* Category 2: Smart-Home Bento Control Grid (Reference Image 4) */
  const subsystemsSection = (
    <div className="sun-smart-grid">
      {[
        {
          title: 'Illuminance',
          display: 'Sunlight',
          icon: SunIlluminanceIcon,
          accent: '#f0ca72',
          meta: `${featureOn('Illuminance') ? 'On' : 'Off'} · ${intensity} klux beam`,
          jump: () => {
            selectCategory(1);
            setPhotometrySlide(0);
          },
        },
        {
          title: 'Sun disc',
          display: 'Sun Disc',
          icon: SunDiscIcon,
          accent: '#f2be72',
          meta: `${featureOn('Sun disc') ? 'On' : 'Off'} · ${Number(discSize).toFixed(2)}° subtense`,
          jump: () => {
            selectCategory(1);
            setPhotometrySlide(2);
          },
        },
        {
          title: 'Daylight cycle',
          display: 'Daylight Cycle',
          icon: SunCycleIcon,
          accent: '#d7be74',
          meta: `${featureOn('Daylight cycle') ? 'On' : 'Off'} · ${clock.text} diurnal`,
          jump: () => selectCategory(0),
        },
      ].map((item) => {
        const isOn = featureOn(item.title);
        return (
          <article
            key={item.title}
            className={`sun-smart-tile ${isOn ? 'is-on' : 'is-off'}`}
          >
            <div className="sun-smart-tile__top">
              <SunIconBadge icon={item.icon} accent={item.accent} size={19} />
              <button
                type="button"
                className="sun-circle-action"
                title={`Open ${item.display} parameters`}
                aria-label={`Open ${item.display} parameters`}
                onClick={item.jump}
              >
                <SunCornerArrowIcon size={15} />
              </button>
            </div>
            <div className="sun-smart-tile__body">
              <h4>{item.display}</h4>
              <p>{item.meta}</p>
            </div>
            <div className="sun-smart-tile__foot">
              <span className="sun-smart-tile__state">{isOn ? 'ACTIVE' : 'BYPASSED'}</span>
              <CapsuleToggle
                on={isOn}
                label={`Toggle ${item.display}`}
                onToggle={() => set('feature:' + item.title, !isOn)}
              />
            </div>
          </article>
        );
      })}

      {lensFlareChild && (
        <article className="sun-smart-tile sun-smart-tile--optics is-on">
          <div className="sun-smart-tile__top">
            <SunIconBadge icon={SunOpticsIcon} accent="#e2b58a" size={19} />
            <button
              type="button"
              className="sun-circle-action"
              title="Inspect Lens Flare optical subcomponent"
              aria-label="Inspect Lens Flare"
              onClick={() => onSelectChild(lensFlareChild.id)}
            >
              <SunCornerArrowIcon size={15} />
            </button>
          </div>
          <div className="sun-smart-tile__body">
            <h4>{lensFlareChild.name}</h4>
            <p>Optical effect · Ghosts & aperture streaks</p>
          </div>
          <div className="sun-smart-tile__foot">
            <span className="sun-smart-tile__state">SUBCOMPONENT</span>
            <button
              type="button"
              className="sun-inspect-pill"
              onClick={() => onSelectChild(lensFlareChild.id)}
            >
              Inspect <SunCornerArrowIcon size={13} />
            </button>
          </div>
        </article>
      )}
    </div>
  );

  const bakingHeaderCard = (
    <article className="sun-bento-card sun-bake-banner-card">
      <header className="sun-bento-card__head">
        <div className="sun-bento-card__title-group">
          <SunIconBadge icon={SunBakeIcon} accent="#9cc4b2" />
          <div>
            <h3>Solar capture & lightmap targets</h3>
            <span className="sun-bento-card__sub">
              Independent procedural or imported image overrides for direct lighting and solar disc
            </span>
          </div>
        </div>
        <span className="sun-pill-tag">2 targets</span>
      </header>
    </article>
  );

  return (
    <div className="sun-studio-root">
      {/* 1. Interactive Atmospheric Sky Strip + Time Preset Pill Carousel */}
      <SolarSkyBanner
        azimuth={azimuth}
        elevation={elevation}
        temperature={temperature}
        time={time}
        onScrubTime={(nextTime, nextAz, nextEl) => {
          set('time', nextTime);
          set('azimuth', nextAz);
          set('elevation', nextEl);
        }}
      />

      {/* 2. Paired / 3-Up Accent-Bar Bento KPI Cards (Reference Image 1) */}
      <div className="sun-kpi-strip" role="group" aria-label="Solar telemetry summary">
        <button
          type="button"
          className={`sun-kpi-card ${categoryIndex === 0 && viewMode === 'carousel' ? 'is-selected' : ''}`}
          style={{ '--kpi-accent': '#eab965' }}
          onClick={() => selectCategory(0)}
        >
          <div className="sun-kpi-card__head">
            <span className="sun-kpi-card__bar" />
            <span>Trajectory</span>
          </div>
          <div className="sun-kpi-card__delta">
            {elevation >= 0 ? '▲ Above horizon' : '▼ Below horizon'}
          </div>
          <div className="sun-kpi-card__main">
            <strong>{elevation}°</strong>
            <small>
              {compassLabel(azimuth)} {azimuth}°
            </small>
          </div>
        </button>

        <button
          type="button"
          className={`sun-kpi-card ${categoryIndex === 1 && viewMode === 'carousel' ? 'is-selected' : ''}`}
          style={{ '--kpi-accent': '#f0ca72' }}
          onClick={() => selectCategory(1)}
        >
          <div className="sun-kpi-card__head">
            <span className="sun-kpi-card__bar" />
            <span>Photometry</span>
          </div>
          <div className="sun-kpi-card__delta">
            {featureOn('Illuminance') ? `● ${solarPhaseName(elevation)}` : '○ Muted'}
          </div>
          <div className="sun-kpi-card__main">
            <strong>
              {intensity}
              <em> klux</em>
            </strong>
            <small>{(temperature / 1000).toFixed(1)}kK</small>
          </div>
        </button>

        <button
          type="button"
          className={`sun-kpi-card ${categoryIndex === 2 && viewMode === 'carousel' ? 'is-selected' : ''}`}
          style={{ '--kpi-accent': '#e39e74' }}
          onClick={() => selectCategory(2)}
        >
          <div className="sun-kpi-card__head">
            <span className="sun-kpi-card__bar" />
            <span>Subsystems</span>
          </div>
          <div className="sun-kpi-card__delta">
            ● {activeSubsystems}/3 channels live
          </div>
          <div className="sun-kpi-card__main">
            <strong>
              {Number(discSize).toFixed(2)}°
            </strong>
            <small>Disc · Flare</small>
          </div>
        </button>
      </div>

      {/* 3. Category Tabs + Carousel Controls + Segmented Progress Bars (Images 4 & 5) */}
      <div className="sun-carousel-nav">
        <div className="sun-carousel-tabs" role="tablist" aria-label="Sun parameter categories">
          {CATEGORIES.map((cat, idx) => {
            const CatIcon = cat.icon;
            const active = viewMode === 'carousel' && categoryIndex === idx;
            return (
              <button
                key={cat.id}
                type="button"
                role="tab"
                aria-selected={active}
                className={`sun-carousel-tab ${active ? 'is-active' : ''}`}
                style={{ '--tab-accent': cat.accent }}
                onClick={() => {
                  if (viewMode !== 'carousel') setViewMode('carousel');
                  selectCategory(idx);
                }}
              >
                <CatIcon size={15} />
                <span>{cat.label}</span>
                <sup>{cat.count}</sup>
              </button>
            );
          })}
        </div>

        <div className="sun-carousel-controls">
          <div className="sun-mode-switch" role="group" aria-label="Layout mode">
            <button
              type="button"
              className={viewMode === 'carousel' ? 'is-active' : ''}
              onClick={() => toggleViewMode('carousel')}
            >
              Carousel
            </button>
            <button
              type="button"
              className={viewMode === 'all' ? 'is-active' : ''}
              onClick={() => toggleViewMode('all')}
            >
              All
            </button>
          </div>

          {viewMode === 'carousel' && (
            <div className="sun-carousel-steppers">
              <span className="sun-carousel-index">
                0{categoryIndex + 1} / 0{CATEGORIES.length}
              </span>
              <button
                type="button"
                aria-label="Previous category"
                onClick={() => selectCategory(categoryIndex - 1)}
              >
                <SunChevronLeftIcon size={15} />
              </button>
              <button
                type="button"
                aria-label="Next category"
                onClick={() => selectCategory(categoryIndex + 1)}
              >
                <SunChevronRightIcon size={15} />
              </button>
            </div>
          )}
        </div>
      </div>

      {viewMode === 'carousel' && (
        <div className="sun-segment-rail" aria-hidden="true">
          {CATEGORIES.map((cat, idx) => (
            <button
              key={cat.id}
              type="button"
              tabIndex={-1}
              className={categoryIndex === idx ? 'is-active' : ''}
              style={{ '--seg-accent': cat.accent }}
              onClick={() => selectCategory(idx)}
            />
          ))}
        </div>
      )}

      {/* 4. Carousel Viewport (or Stacked All Mode) */}
      {viewMode === 'all' ? (
        <div className="sun-all-stack">
          {trajectorySection}
          {photometrySection}
          {subsystemsSection}
          {bakingHeaderCard}
        </div>
      ) : (
        <div
          className="sun-carousel-viewport"
          onKeyDown={(e) => {
            if (e.target.tagName === 'INPUT') return;
            if (e.key === 'ArrowRight') selectCategory(categoryIndex + 1);
            if (e.key === 'ArrowLeft') selectCategory(categoryIndex - 1);
          }}
          onPointerDown={(e) => {
            if (e.target.closest('input, button, .gizmo')) return;
            swipeStart.current = e.clientX;
          }}
          onPointerUp={(e) => {
            if (swipeStart.current == null) return;
            const dx = e.clientX - swipeStart.current;
            swipeStart.current = null;
            if (Math.abs(dx) > 48) {
              selectCategory(categoryIndex + (dx < 0 ? 1 : -1));
            }
          }}
        >
          <div
            className="sun-carousel-track"
            style={{ transform: `translate3d(-${categoryIndex * 100}%, 0, 0)` }}
          >
            <section
              className="sun-carousel-slide"
              aria-label="Trajectory category"
              onFocusCapture={() => selectCategory(0)}
            >
              {trajectorySection}
            </section>
            <section
              className="sun-carousel-slide"
              aria-label="Photometry category"
              onFocusCapture={() => selectCategory(1)}
            >
              {photometrySection}
            </section>
            <section
              className="sun-carousel-slide"
              aria-label="Subsystems category"
              onFocusCapture={() => selectCategory(2)}
            >
              {subsystemsSection}
            </section>
            <section
              className="sun-carousel-slide"
              aria-label="Baking category"
              onFocusCapture={() => selectCategory(3)}
            >
              {bakingHeaderCard}
            </section>
          </div>
        </div>
      )}
    </div>
  );
}
