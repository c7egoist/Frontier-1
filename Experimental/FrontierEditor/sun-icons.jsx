import React, { forwardRef } from 'react';

/**
 * Shared precision-crafted 24x24 optical & celestial icon family for Sun Studio
 * across both Frontier Editor and Project-Zero Editor.
 * Each card, category, and subsystem switch gets its own unmistakable silhouette.
 */
function makeSunIcon(name, renderPaths) {
  const Component = forwardRef(function SunStudioIcon(
    { size = 20, strokeWidth = 1.5, className = '', title, ...props },
    ref,
  ) {
    const labelled = Boolean(
      title || props['aria-label'] || props['aria-labelledby'],
    );
    return (
      <svg
        ref={ref}
        xmlns="http://www.w3.org/2000/svg"
        width={size}
        height={size}
        viewBox="0 0 24 24"
        fill="none"
        stroke="currentColor"
        strokeWidth={strokeWidth}
        strokeLinecap="round"
        strokeLinejoin="round"
        className={`sun-studio-icon sun-studio-icon--${name} ${className}`.trim()}
        aria-hidden={labelled ? undefined : true}
        role={labelled ? 'img' : undefined}
        focusable="false"
        {...props}
      >
        {title && <title>{title}</title>}
        {renderPaths}
      </svg>
    );
  });
  Component.displayName =
    'Sun' + name.replace(/(^|-)([a-z])/g, (_, __, c) => c.toUpperCase()) + 'Icon';
  return Component;
}

/* 1. Entity / Hero Core: crisp solar photosphere + subtle corona ring + alternating rays */
export const SunEntityIcon = makeSunIcon(
  'entity',
  <>
    <circle cx="12" cy="12" r="4.1" fill="currentColor" fillOpacity="0.16" />
    <circle
      cx="12"
      cy="12"
      r="6.6"
      strokeOpacity="0.28"
      strokeWidth="1"
      strokeDasharray="1.6 2.6"
    />
    <path d="M12 2.1v2.3M12 19.6v2.3M2.1 12h2.3M19.6 12h2.3" />
    <path
      d="m5 5 1.55 1.55m10.9 10.9L19 19m0-14-1.55 1.55M6.55 17.45 5 19"
      strokeOpacity="0.82"
    />
  </>,
);

/* 2. Trajectory / Sun Direction: perspective horizon ring + elevated solar arc + nadir plumb */
export const SunTrajectoryIcon = makeSunIcon(
  'trajectory',
  <>
    <ellipse cx="12" cy="16.2" rx="9.2" ry="3.3" strokeOpacity="0.45" />
    <path d="M3.4 15.6C5.4 4.8 18.6 4.8 20.6 15.6" />
    <line
      x1="15.4"
      y1="8.1"
      x2="15.4"
      y2="16.1"
      strokeDasharray="1.6 2.2"
      strokeOpacity="0.55"
    />
    <circle cx="15.4" cy="16.1" r="1.1" fill="currentColor" stroke="none" opacity="0.55" />
    <circle cx="15.4" cy="7.6" r="2.35" fill="currentColor" fillOpacity="0.25" />
  </>,
);

/* 3. Photometry / Illuminance: upper solar source + parallel lux beams striking ground datum */
export const SunIlluminanceIcon = makeSunIcon(
  'illuminance',
  <>
    <path
      d="M7.8 6.5a4.2 4.2 0 0 1 8.4 0"
      fill="currentColor"
      fillOpacity="0.16"
    />
    <path d="M6.2 6.5h11.6" strokeOpacity="0.65" />
    <path d="M8.5 9.5v6.8M12 9.2v7.1M15.5 9.5v6.8" />
    <path d="m10.4 14.5 1.6 1.8 1.6-1.8" strokeOpacity="0.85" />
    <path d="M3.5 19.5h17" />
    <path d="M7 21.3h10" strokeOpacity="0.38" />
  </>,
);

/* 4. Spectrum / Colour Temperature: Planckian bell curve + wavelength peak hairline & node */
export const SunTemperatureIcon = makeSunIcon(
  'temperature',
  <>
    <path
      d="M3 18.2C6.4 17.8 8.3 6.2 12.2 6.2c3.8 0 5.6 9.2 8.8 11.4"
      fill="currentColor"
      fillOpacity="0.12"
    />
    <line
      x1="12.2"
      y1="8.4"
      x2="12.2"
      y2="19.2"
      strokeDasharray="1.6 2.2"
      strokeOpacity="0.55"
    />
    <circle cx="12.2" cy="6.2" r="2.1" fill="currentColor" fillOpacity="0.28" />
    <path d="M3 19.5h18" strokeOpacity="0.55" />
    <circle cx="6.2" cy="19.5" r="1" fill="currentColor" stroke="none" opacity="0.6" />
    <circle cx="18" cy="19.5" r="1" fill="currentColor" stroke="none" opacity="0.6" />
  </>,
);

/* 5. Daylight Cycle / Diurnal Clock: horizon line bisecting 24h diurnal wave with sun crest */
export const SunCycleIcon = makeSunIcon(
  'cycle',
  <>
    <path d="M2.5 13.5h19" strokeDasharray="2 2.5" strokeOpacity="0.45" />
    <path d="M2.5 17.2c2.4 0 3.6-3.7 5-3.7 2.2-5.8 6.8-5.8 9 0 1.4 0 2.6 3.7 5 3.7" />
    <circle cx="12" cy="8.9" r="2.15" fill="currentColor" fillOpacity="0.26" />
    <circle cx="5.2" cy="16.8" r="1.15" strokeOpacity="0.55" />
  </>,
);

/* 6. Sun Disc / Angular Subtense: inner disc + 0.53° dashed limb ring + optical calipers */
export const SunDiscIcon = makeSunIcon(
  'disc',
  <>
    <circle
      cx="12"
      cy="12"
      r="7.4"
      strokeDasharray="2.2 2.4"
      strokeOpacity="0.52"
    />
    <circle cx="12" cy="12" r="3.9" fill="currentColor" fillOpacity="0.24" />
    <path d="M3.2 8.2 1.8 12l1.4 3.8M20.8 8.2 22.2 12l-1.4 3.8" />
  </>,
);

/* 7. Dynamic Ephemeris / Geolocation: meridian globe + inclined ecliptic orbit */
export const SunEphemerisIcon = makeSunIcon(
  'ephemeris',
  <>
    <circle cx="12" cy="12" r="8.2" strokeOpacity="0.72" />
    <ellipse cx="12" cy="12" rx="3.8" ry="8.2" strokeOpacity="0.42" />
    <path d="M3.9 12h16.2" strokeOpacity="0.38" />
    <path d="M4.8 15.8 19.2 8.2" />
    <circle cx="16.8" cy="9.5" r="1.9" fill="currentColor" fillOpacity="0.3" />
  </>,
);

/* 8. Optical Subcomponent / Lens Flare: coaxial optical axis + hexagonal aperture + ghost rings */
export const SunOpticsIcon = makeSunIcon(
  'optics',
  <>
    <path d="M4 20 20 4" strokeOpacity="0.4" strokeDasharray="2 2.4" />
    <polygon
      points="12,7.6 15.8,9.8 15.8,14.2 12,16.4 8.2,14.2 8.2,9.8"
      fill="currentColor"
      fillOpacity="0.14"
    />
    <circle cx="6.8" cy="17.2" r="2.1" strokeOpacity="0.78" />
    <circle cx="17.4" cy="6.6" r="2.4" fill="currentColor" fillOpacity="0.24" />
  </>,
);

/* 9. Lightmap / Sky Bake: isometric capture plate + downward radiant capture arrow */
export const SunBakeIcon = makeSunIcon(
  'bake',
  <>
    <path
      d="m3.5 15.2 8.5-4.1 8.5 4.1-8.5 4.1-8.5-4.1Z"
      fill="currentColor"
      fillOpacity="0.13"
    />
    <path
      d="m7.8 13.1 8.4 4.1m0-4.1-8.4 4.1"
      strokeOpacity="0.35"
      strokeWidth="1.1"
    />
    <path d="M12 3.2v8.1m-2.7-2.5L12 11.5l2.7-2.7" />
  </>,
);

/* 10. UI Navigation Glyphs matching reference studio cards (↗, ‹, ›) */
export const SunCornerArrowIcon = makeSunIcon(
  'corner-arrow',
  <>
    <path d="M7.5 16.5 16.5 7.5M9.5 7.5h7v7" />
  </>,
);

export const SunChevronLeftIcon = makeSunIcon(
  'chevron-left',
  <path d="m14.5 6.5-5.5 5.5 5.5 5.5" />,
);

export const SunChevronRightIcon = makeSunIcon(
  'chevron-right',
  <path d="m9.5 6.5 5.5 5.5-5.5 5.5" />,
);

/**
 * Circular dark obsidian icon badge inspired by the reference Bento studio cards.
 */
export function SunIconBadge({
  icon: IconComponent,
  accent = '#eab965',
  size = 18,
  className = '',
}) {
  return (
    <span
      className={`sun-icon-badge ${className}`.trim()}
      style={{ '--sun-badge-accent': accent }}
      aria-hidden="true"
    >
      <IconComponent size={size} strokeWidth={1.55} />
    </span>
  );
}

/* ── Shared Solar & Chromatic Helpers ───────────────────────────────────────── */

export function kelvinToRgb(kelvin) {
  const t = Math.max(1000, Math.min(40000, Number(kelvin) || 5600)) / 100;
  let r, g, b;
  if (t <= 66) {
    r = 255;
    g = 99.4708025861 * Math.log(t) - 161.1195681661;
  } else {
    r = 329.698727446 * Math.pow(t - 60, -0.1332047592);
    g = 288.1221695283 * Math.pow(t - 60, -0.0755148492);
  }
  if (t >= 66) b = 255;
  else if (t <= 19) b = 0;
  else b = 138.5177312231 * Math.log(t - 10) - 305.0447927307;
  const clamp = (v) => Math.max(0, Math.min(255, Math.round(v)));
  return [clamp(r), clamp(g), clamp(b)];
}

export function kelvinToCss(kelvin, alpha = 1) {
  const [r, g, b] = kelvinToRgb(kelvin);
  return alpha >= 1 ? `rgb(${r}, ${g}, ${b})` : `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

export function compassLabel(azimuth) {
  const dirs = [
    'N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE',
    'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW',
  ];
  const norm = (((Number(azimuth) || 0) % 360) + 360) % 360;
  return dirs[Math.round(norm / 22.5) % 16];
}

export function solarPhaseName(elevation) {
  const alt = Number(elevation) || 0;
  if (alt >= 48) return 'High Sun';
  if (alt >= 22) return 'Full Day';
  if (alt >= 6) return 'Low Sun';
  if (alt >= -0.8) return 'Golden Hour';
  if (alt >= -6) return 'Civil Twilight';
  if (alt >= -12) return 'Blue Hour';
  return 'Night';
}

export function formatClockHours(hours) {
  const norm = (((Number(hours) || 0) % 24) + 24) % 24;
  const hh = Math.floor(norm);
  const mm = Math.min(59, Math.round((norm - hh) * 60));
  return {
    hh: String(hh).padStart(2, '0'),
    mm: String(mm).padStart(2, '0'),
    text: `${String(hh).padStart(2, '0')}:${String(mm).padStart(2, '0')}`,
  };
}

export function skyPaletteForElevation(elevation) {
  const stops = [
    [-20, [5, 7, 13], [10, 14, 24]],
    [-10, [7, 10, 18], [20, 28, 52]],
    [-4, [12, 18, 36], [52, 54, 88]],
    [0, [20, 32, 62], [168, 98, 72]],
    [5, [28, 50, 92], [220, 142, 78]],
    [14, [34, 68, 118], [198, 174, 138]],
    [32, [36, 84, 144], [148, 182, 214]],
    [65, [32, 92, 162], [136, 178, 222]],
  ];
  const alt = Number(elevation) || 0;
  let idx = 0;
  while (idx < stops.length - 2 && alt > stops[idx + 1][0]) idx++;
  const [a0, z0, h0] = stops[idx];
  const [a1, z1, h1] = stops[idx + 1];
  const t = Math.max(0, Math.min(1, (alt - a0) / (a1 - a0 || 1)));
  const lerp = (u, v) =>
    `rgb(${Math.round(u[0] + (v[0] - u[0]) * t)}, ${Math.round(u[1] + (v[1] - u[1]) * t)}, ${Math.round(u[2] + (v[2] - u[2]) * t)})`;
  return { zenith: lerp(z0, z1), horizon: lerp(h0, h1) };
}

export const KELVIN_PRESETS = [
  { kelvin: 1900, label: 'Candle', tag: '1.9k' },
  { kelvin: 2800, label: 'Tungsten', tag: '2.8k' },
  { kelvin: 3500, label: 'Golden', tag: '3.5k' },
  { kelvin: 5600, label: 'Daylight', tag: '5.6k' },
  { kelvin: 6500, label: 'Overcast', tag: '6.5k' },
  { kelvin: 8500, label: 'Shade', tag: '8.5k' },
];

export const DIURNAL_PRESETS = [
  { id: 'dawn', label: 'Dawn', time: 6.0, azimuth: 90, elevation: 2 },
  { id: 'morning', label: 'Morning', time: 9.0, azimuth: 112, elevation: 32 },
  { id: 'noon', label: 'Noon', time: 12.0, azimuth: 180, elevation: 62 },
  { id: 'afternoon', label: 'Afternoon', time: 15.0, azimuth: 225, elevation: 42 },
  { id: 'golden', label: 'Golden', time: 17.5, azimuth: 258, elevation: 9 },
  { id: 'twilight', label: 'Twilight', time: 19.0, azimuth: 278, elevation: -4 },
  { id: 'night', label: 'Midnight', time: 0.0, azimuth: 0, elevation: -42 },
];
