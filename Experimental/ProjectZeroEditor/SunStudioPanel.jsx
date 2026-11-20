import React, { useId, useRef, useState } from "react";
import { SunGizmo } from "../FrontierEditor/environment-graphics.jsx";
import "../FrontierEditor/sun-inspector.css";
import {
  SunEntityIcon,
  SunTrajectoryIcon,
  SunIlluminanceIcon,
  SunTemperatureIcon,
  SunCycleIcon,
  SunDiscIcon,
  SunEphemerisIcon,
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
} from "../FrontierEditor/sun-icons.jsx";
import { SunGraph } from "./LiveGraph.jsx";
import { Solar } from "./Solar.mjs";

const PZ_CATEGORIES = [
  { id: "trajectory", label: "Trajectory", count: 2, icon: SunTrajectoryIcon, accent: "#eab965" },
  { id: "photometry", label: "Photometry", count: 3, icon: SunIlluminanceIcon, accent: "#f0ca72" },
  { id: "ephemeris", label: "Ephemeris", count: 3, icon: SunEphemerisIcon, accent: "#8fc6b4" },
  { id: "baking", label: "Baking", count: 2, icon: SunBakeIcon, accent: "#e39e74" },
];

const PZ_TIME_PRESETS = [
  { id: "dawn", label: "Dawn", hours: 6.0 },
  { id: "morning", label: "Morning", hours: 9.0 },
  { id: "noon", label: "Noon", hours: 12.0 },
  { id: "afternoon", label: "Afternoon", hours: 15.0 },
  { id: "golden", label: "Golden", hours: 17.5 },
  { id: "twilight", label: "Twilight", hours: 19.0 },
  { id: "night", label: "Midnight", hours: 0.0 },
];

function SplitMetric({ value, decimals = 1, unit = "" }) {
  const num = Number(value);
  if (!Number.isFinite(num)) {
    return (
      <div className="sun-bento-metric">
        <span className="sun-bento-metric__int">{String(value)}</span>
        {unit && <small className="sun-bento-metric__unit">{unit}</small>}
      </div>
    );
  }
  const formatted = num.toFixed(decimals);
  const [whole, frac] = formatted.split(".");
  const sign = num < 0 ? "−" : "";
  const absWhole = Math.abs(Number(whole)).toLocaleString("en-US");
  return (
    <div className="sun-bento-metric">
      <span className="sun-bento-metric__int">
        {sign}
        {absWhole}
      </span>
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
      className={`sun-capsule-toggle ${on ? "is-on" : "is-off"}`}
      onClick={onToggle}
    >
      <span className="sun-capsule-toggle__thumb" />
    </button>
  );
}

export default function SunStudioPanel({
  Subject,
  V,
  AssignProperty,
  F,
  Hidden,
  ToggleHidden,
  EntityNotesSlot,
  OnSelectEntity,
}) {
  const uid = useId().replace(/:/g, "");
  const [categoryIndex, setCategoryIndex] = useState(0);
  const [viewMode, setViewMode] = useState("carousel");
  const [trajectorySlide, setTrajectorySlide] = useState(0);
  const [photometrySlide, setPhotometrySlide] = useState(0);
  const skyDragRef = useRef(false);
  const swipeStart = useRef(null);

  const localHours = Number(V("Local Hours")) || 0;
  const solved = Solar({
    LocalHours: localHours,
    Month: V("Month"),
    Day: V("Day of Month"),
    Latitude: V("Latitude"),
    Longitude: V("Longitude"),
  });
  const { Elevation: elevation, Azimuth: azimuth } = solved;
  const useKelvin = !!V("Colour source");
  const temperature = Number(V("Temperature")) || 6500;
  const sunTint = V("Sun Tint") || "#fff4e0";
  const intensity = Number(V("Intensity")) || 1;
  const direct = Number(V("Direct")) || 2.5;
  const angularDiameter = Number(V("Angular Diameter")) || 0.533;
  const animate = !!V("Animate");

  const apparentColor = useKelvin
    ? kelvinToCss(
        elevation < 12
          ? temperature +
              (1850 - temperature) *
                Math.pow(Math.max(0, Math.min(1, (12 - elevation) / 18)), 0.72)
          : temperature,
      )
    : sunTint;

  const clock = formatClockHours(localHours);
  const phase = solarPhaseName(elevation);
  const { zenith, horizon } = skyPaletteForElevation(elevation);
  const sunX = 48 + ((((azimuth % 360) + 360) % 360) / 360) * 524;
  const normAlt = Math.max(-0.32, Math.min(1, elevation / 68));
  const horizonY = 104;
  const sunY = horizonY - normAlt * 72;

  const selectCategory = (idx) => {
    const wrapped =
      ((idx % PZ_CATEGORIES.length) + PZ_CATEGORIES.length) %
      PZ_CATEGORIES.length;
    setCategoryIndex(wrapped);
  };

  const scrubSky = (clientX, rect) => {
    const nextHours = Math.max(
      0,
      Math.min(24, ((clientX - rect.left) / Math.max(1, rect.width)) * 24),
    );
    AssignProperty("Local Hours", Number(nextHours.toFixed(2)));
  };

  const stepHours = (delta) => {
    const next = (((localHours + delta) % 24) + 24) % 24;
    AssignProperty("Local Hours", Number(next.toFixed(2)));
  };

  /* ── Slide 0: Trajectory (Sun Direction & Daylight Cycle) ───────────────── */
  const directionCard = (
    <section
      className="sun-bento-card sun-bento-card--glow"
      data-card="Sun direction"
    >
      <header className="sun-bento-card__head">
        <div className="sun-bento-card__title-group">
          <SunIconBadge icon={SunTrajectoryIcon} accent="#eab965" />
          <div>
            <h3>Sun direction</h3>
            <span className="sun-bento-card__sub">NOAA ephemeris altitude & bearing</span>
          </div>
        </div>
        <span className="sun-pill-tag">
          {compassLabel(azimuth)} · {Math.round(azimuth)}°
        </span>
      </header>

      <div className="sun-bento-card__kpi-row">
        <SplitMetric value={elevation} decimals={1} unit="°" />
        <span
          className={`sun-status-chip ${elevation < 0 ? "is-muted" : "is-positive"}`}
        >
          {elevation < 0 ? "▼ Below horizon" : "▲ Above horizon"}
        </span>
      </div>

      <div
        className="sun-gizmo-well"
        title="Drag horizontally to scrub Local Hours; orbit solved from date & location"
      >
        <SunGizmo
          azimuth={Math.round(azimuth)}
          elevation={Math.round(elevation)}
          onChange={(nextAz) => {
            const deltaAz = ((((nextAz - azimuth + 540) % 360) - 180) / 15);
            stepHours(deltaAz);
          }}
        />
      </div>

      <div className="pz-sun-field-stack">{F("Local Hours")}</div>

      <div className="sun-dual-controls">
        <div className="sun-axis-box">
          <div className="sun-axis-box__top">
            <span>Azimuth</span>
            <strong>
              {azimuth.toFixed(1)}
              <small>°</small>
            </strong>
          </div>
          <div className="pz-solved-bar">
            <i style={{ width: `${Math.max(2, (azimuth / 360) * 100)}%` }} />
          </div>
        </div>
        <div className="sun-axis-box">
          <div className="sun-axis-box__top">
            <span>Elevation</span>
            <strong>
              {elevation.toFixed(1)}
              <small>°</small>
            </strong>
          </div>
          <div className="pz-solved-bar">
            <i
              style={{
                width: `${Math.max(2, ((elevation + 90) / 180) * 100)}%`,
              }}
            />
          </div>
        </div>
      </div>
    </section>
  );

  const daylightCard = (
    <section className="sun-bento-card" data-card="Daylight cycle">
      <header className="sun-bento-card__head">
        <div className="sun-bento-card__title-group">
          <SunIconBadge icon={SunCycleIcon} accent="#d7be74" />
          <div>
            <h3>Daylight cycle</h3>
            <span className="sun-bento-card__sub">Diurnal elevation curve</span>
          </div>
        </div>
        <CapsuleToggle
          on={animate}
          label="Animate day cycle"
          onToggle={() => AssignProperty("Animate", !animate)}
        />
      </header>

      <div className="sun-bento-card__kpi-row">
        <div className="sun-bento-metric">
          <span className="sun-bento-metric__int">{clock.hh}</span>
          <span className="sun-bento-metric__frac">:{clock.mm}</span>
        </div>
        <span className="sun-pill-tag">{phase}</span>
      </div>

      <div className="pz-sun-graph-frame">
        <SunGraph V={V} Change={AssignProperty} Mode="day" />
      </div>

      <div className="sun-card-bottom-actions">
        <span className="sun-card-hint">
          {animate ? "Day cycle running live" : "Held at authored solar hour"}
        </span>
        <div className="sun-round-steppers">
          <button
            type="button"
            aria-label="Step local time back 30 minutes"
            onClick={() => stepHours(-0.5)}
          >
            −
          </button>
          <button
            type="button"
            aria-label="Step local time forward 30 minutes"
            onClick={() => stepHours(0.5)}
          >
            +
          </button>
        </div>
      </div>
    </section>
  );

  const trajectorySection = (
    <div className="sun-subcarousel">
      <div className="sun-subcarousel__bar pz-only-compact">
        <div
          className="sun-subcarousel__pills"
          role="tablist"
          aria-label="Trajectory cards"
        >
          {["Sun direction", "Daylight cycle"].map((label, idx) => (
            <button
              key={label}
              type="button"
              role="tab"
              aria-selected={trajectorySlide === idx}
              className={`sun-subcarousel__pill ${trajectorySlide === idx ? "is-active" : ""}`}
              onClick={() => setTrajectorySlide(idx)}
            >
              <span>0{idx + 1}</span> {label}
            </button>
          ))}
        </div>
        <div className="sun-subcarousel__arrows">
          <button
            type="button"
            aria-label="Previous trajectory card"
            onClick={() => setTrajectorySlide((s) => (s + 1) % 2)}
          >
            <SunChevronLeftIcon size={15} />
          </button>
          <button
            type="button"
            aria-label="Next trajectory card"
            onClick={() => setTrajectorySlide((s) => (s + 1) % 2)}
          >
            <SunChevronRightIcon size={15} />
          </button>
        </div>
      </div>

      <div className="pz-trajectory-deck" style={{ "--traj-slide": trajectorySlide }}>
        <div
          className="pz-trajectory-deck__item"
          onFocusCapture={() => setTrajectorySlide(0)}
        >
          {directionCard}
        </div>
        <div
          className="pz-trajectory-deck__item"
          onFocusCapture={() => setTrajectorySlide(1)}
        >
          {daylightCard}
        </div>
      </div>
    </div>
  );

  /* ── Slide 1: Photometry (Intensity, Temperature, Sun Disc) ─────────────── */
  const photometryCards = [
    <section
      key="gain"
      className="sun-bento-card sun-bento-card--glow"
      data-card="Sunlight intensity"
    >
      <header className="sun-bento-card__head">
        <div className="sun-bento-card__title-group">
          <SunIconBadge icon={SunIlluminanceIcon} accent="#f0ca72" />
          <div>
            <h3>Sunlight intensity</h3>
            <span className="sun-bento-card__sub">Authored multiplier × direct beam</span>
          </div>
        </div>
        <span className="sun-status-chip is-positive">
          {(intensity * direct).toFixed(1)}× combined
        </span>
      </header>

      <div className="sun-bento-card__kpi-row">
        <SplitMetric value={intensity} decimals={1} unit="×" />
        <span className="sun-pill-tag">Direct {direct.toFixed(2)}×</span>
      </div>

      <div className="pz-sun-graph-frame">
        <SunGraph V={V} Change={AssignProperty} Mode="gain" />
      </div>

      <div className="pz-sun-field-stack">
        {F("Intensity")}
        {F("Direct")}
      </div>
    </section>,

    <section key="temp" className="sun-bento-card" data-card="Temperature">
      <header className="sun-bento-card__head">
        <div className="sun-bento-card__title-group">
          <SunIconBadge icon={SunTemperatureIcon} accent="#e89e76" />
          <div>
            <h3>Temperature & tint</h3>
            <span className="sun-bento-card__sub">Blackbody locus or custom RGB slot</span>
          </div>
        </div>
        <span className="sun-swatch-pill">
          <i style={{ background: apparentColor }} />
          {useKelvin ? `${(temperature / 1000).toFixed(1)} kK` : sunTint.toUpperCase()}
        </span>
      </header>

      <div className="sun-bento-card__kpi-row">
        {useKelvin ? (
          <SplitMetric value={temperature} decimals={0} unit="K" />
        ) : (
          <SplitMetric value="RGB" decimals={0} unit="tint" />
        )}
      </div>

      <div
        className="sun-preset-carousel sun-preset-carousel--compact"
        role="group"
        aria-label="Kelvin presets"
      >
        {KELVIN_PRESETS.map((preset) => {
          const active = useKelvin && Math.abs(temperature - preset.kelvin) <= 220;
          return (
            <button
              key={preset.kelvin}
              type="button"
              className={`sun-preset-pill ${active ? "is-active" : ""}`}
              onClick={() => {
                AssignProperty("Colour source", true);
                AssignProperty("Temperature", preset.kelvin);
              }}
            >
              <i style={{ background: kelvinToCss(preset.kelvin) }} />
              <span>{preset.label}</span>
              <small>{preset.tag}</small>
            </button>
          );
        })}
      </div>

      <div className="pz-sun-graph-frame">
        <SunGraph V={V} Change={AssignProperty} Mode="temperature" />
      </div>

      <div className="pz-sun-field-stack">
        {F("Colour source")}
        {F("Temperature", !useKelvin)}
        {F("Sun Tint", useKelvin)}
      </div>
    </section>,

    <section key="disc" className="sun-bento-card" data-card="Sun disc">
      <header className="sun-bento-card__head">
        <div className="sun-bento-card__title-group">
          <SunIconBadge icon={SunDiscIcon} accent="#f2be72" />
          <div>
            <h3>Sun disc</h3>
            <span className="sun-bento-card__sub">Solar photosphere angular subtense</span>
          </div>
        </div>
        <span className="sun-pill-tag">
          {(angularDiameter / 0.533).toFixed(2)}× real
        </span>
      </header>

      <div className="sun-disc-caliper">
        <div>
          <SplitMetric value={angularDiameter} decimals={2} unit="°" />
          <p className="sun-card-hint">Reference dashed limb: 0.53°</p>
        </div>
        <div className="sun-disc-caliper__stage" aria-hidden="true">
          <span className="sun-disc-caliper__ref" />
          <i
            className="sun-disc-caliper__orb"
            style={{
              width: `${Math.min(76, 16 + angularDiameter * 30)}px`,
              height: `${Math.min(76, 16 + angularDiameter * 30)}px`,
              background: apparentColor,
              boxShadow: `0 0 22px ${apparentColor}`,
            }}
          />
        </div>
      </div>

      <div className="pz-sun-field-stack">{F("Angular Diameter")}</div>

      <div className="sun-card-bottom-actions">
        <span className="sun-card-hint">Adjusts disc limb & penumbra angle</span>
        <div className="sun-round-steppers">
          <button
            type="button"
            aria-label="Decrease angular diameter"
            onClick={() =>
              AssignProperty(
                "Angular Diameter",
                Number(Math.max(0.05, angularDiameter - 0.05).toFixed(3)),
              )
            }
          >
            −
          </button>
          <button
            type="button"
            aria-label="Increase angular diameter"
            onClick={() =>
              AssignProperty(
                "Angular Diameter",
                Number(Math.min(5, angularDiameter + 0.05).toFixed(3)),
              )
            }
          >
            +
          </button>
        </div>
      </div>
    </section>,
  ];

  const photometrySection = (
    <div className="sun-subcarousel">
      <div className="sun-subcarousel__bar">
        <div
          className="sun-subcarousel__pills"
          role="tablist"
          aria-label="Photometry cards"
        >
          {["Intensity", "Temperature", "Sun disc"].map((label, idx) => (
            <button
              key={label}
              type="button"
              role="tab"
              aria-selected={photometrySlide === idx}
              className={`sun-subcarousel__pill ${photometrySlide === idx ? "is-active" : ""}`}
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
          style={{ "--sub-slide": photometrySlide }}
        >
          {photometryCards.map((cardEl, idx) => (
            <div
              key={idx}
              className="sun-subcarousel__item"
              onFocusCapture={() => setPhotometrySlide(idx)}
            >
              {cardEl}
            </div>
          ))}
        </div>
      </div>
    </div>
  );

  /* ── Slide 2: Ephemeris & Subsystems ────────────────────────────────────── */
  const ephemerisSection = (
    <div className="pz-ephemeris-stack">
      <div className="sun-smart-grid tiles sun-tiles">
        <article className={`sun-smart-tile ${!Hidden ? "is-on" : "is-off"}`}>
          <div className="sun-smart-tile__top">
            <SunIconBadge icon={SunEntityIcon} accent="#eab965" size={19} />
            <span className="sun-pill-tag">{!Hidden ? "Visible" : "Hidden"}</span>
          </div>
          <div className="sun-smart-tile__body">
            <h4>Sun Emitter</h4>
            <p>{!Hidden ? "Active in world outliner" : "Muted in scene"}</p>
          </div>
          <div className="sun-smart-tile__foot">
            <span className="sun-smart-tile__state">VISIBILITY</span>
            <CapsuleToggle
              on={!Hidden}
              label="Enabled"
              onToggle={ToggleHidden}
            />
          </div>
        </article>

        <article className={`sun-smart-tile ${animate ? "is-on" : "is-off"}`}>
          <div className="sun-smart-tile__top">
            <SunIconBadge icon={SunCycleIcon} accent="#d7be74" size={19} />
            <button
              type="button"
              className="sun-circle-action"
              title="Go to Daylight Cycle"
              aria-label="Go to Daylight Cycle"
              onClick={() => {
                selectCategory(0);
                setTrajectorySlide(1);
              }}
            >
              <SunCornerArrowIcon size={15} />
            </button>
          </div>
          <div className="sun-smart-tile__body">
            <h4>Day Cycle</h4>
            <p>{animate ? "Dynamic diurnal clock" : "Static authored hour"}</p>
          </div>
          <div className="sun-smart-tile__foot">
            <span className="sun-smart-tile__state">
              {animate ? "DYNAMIC" : "STATIC"}
            </span>
            <CapsuleToggle
              on={animate}
              label="Day cycle"
              onToggle={() => AssignProperty("Animate", !animate)}
            />
          </div>
        </article>

        <article className="sun-smart-tile sun-smart-tile--optics is-on">
          <div className="sun-smart-tile__top">
            <SunIconBadge icon={SunOpticsIcon} accent="#e2b58a" size={19} />
            <button
              type="button"
              className="sun-circle-action"
              title="Inspect Lens Flare subcomponent"
              aria-label="Inspect Lens Flare"
              onClick={() => OnSelectEntity?.("flare")}
            >
              <SunCornerArrowIcon size={15} />
            </button>
          </div>
          <div className="sun-smart-tile__body">
            <h4>Lens Flare</h4>
            <p>Optical effect subcomponent · Ghosts & halo</p>
          </div>
          <div className="sun-smart-tile__foot">
            <span className="sun-smart-tile__state">OPTICS</span>
            <button
              type="button"
              className="sun-inspect-pill"
              onClick={() => OnSelectEntity?.("flare")}
            >
              Inspect <SunCornerArrowIcon size={13} />
            </button>
          </div>
        </article>

        <article className="sun-smart-tile is-on">
          <div className="sun-smart-tile__top">
            <SunIconBadge icon={SunEphemerisIcon} accent="#8fc6b4" size={19} />
            <span className="sun-pill-tag">NOAA</span>
          </div>
          <div className="sun-smart-tile__body">
            <h4>Solar Ephemeris</h4>
            <p>
              Declination {solved.Declination.toFixed(1)}° · EoT{" "}
              {solved.EquationOfTime.toFixed(1)}m
            </p>
          </div>
          <div className="sun-smart-tile__foot">
            <span className="sun-smart-tile__state">SOLVER</span>
            <span className="sun-pill-tag">
              {Number(V("Latitude")).toFixed(1)}°, {Number(V("Longitude")).toFixed(1)}°
            </span>
          </div>
        </article>
      </div>

      <section className="sun-bento-card" data-card="Dynamic settings">
        <header className="sun-bento-card__head">
          <div className="sun-bento-card__title-group">
            <SunIconBadge icon={SunEphemerisIcon} accent="#8fc6b4" />
            <div>
              <h3>Dynamic settings & geolocation</h3>
              <span className="sun-bento-card__sub">
                Annual noon-elevation curve, coordinates & clock speed
              </span>
            </div>
          </div>
          <span className="sun-pill-tag">
            Month {V("Month")} · Day {V("Day of Month")}
          </span>
        </header>

        <div className="pz-sun-graph-frame">
          <SunGraph V={V} Change={AssignProperty} Mode="year" />
        </div>

        <div className="pz-sun-field-grid">
          {F("Latitude")}
          {F("Longitude")}
          {F("Month")}
          {F("Day of Month")}
          {F("Day duration")}
          {F("Speed")}
        </div>

        <div className="sun-balance-blocks">
          <div className="sun-balance-block sun-balance-block--hatched">
            <small>Solar Declination</small>
            <strong>{solved.Declination.toFixed(2)}°</strong>
          </div>
          <div
            className="sun-balance-block sun-balance-block--hatched"
            style={{ borderColor: "rgba(143, 198, 180, 0.28)" }}
          >
            <small>Equation of Time</small>
            <strong>{solved.EquationOfTime.toFixed(2)} min</strong>
          </div>
        </div>
      </section>
    </div>
  );

  /* ── Slide 3: Baking (Sun Lighting Bake & Sun Disk Bake) ────────────────── */
  const bakingSection = (
    <div className="sun-slide-grid sun-slide-grid--two">
      {[
        ["Sun lighting bake", "Direct & sky irradiance capture"],
        ["Sun disk bake", "Procedural solar limb & corona sprite"],
      ].map(([title, subtitle]) => (
        <section key={title} className="sun-bento-card" data-card={title}>
          <header className="sun-bento-card__head">
            <div className="sun-bento-card__title-group">
              <SunIconBadge icon={SunBakeIcon} accent="#e39e74" />
              <div>
                <h3>{title}</h3>
                <span className="sun-bento-card__sub">{subtitle}</span>
              </div>
            </div>
            <span className="sun-pill-tag">Procedural</span>
          </header>

          <div className="pz-bake-bento-row">
            <div>
              <strong className="pz-bake-bento-row__title">Use baked image</strong>
              <p className="sun-card-hint">
                Renderer / imported-image binding unavailable in HTML preview
              </p>
            </div>
            <CapsuleToggle
              on={false}
              disabled
              label={`Use baked image for ${title}`}
              onToggle={() => {}}
            />
          </div>

          <div className="pz-bake-bento-actions">
            <button type="button" disabled>
              <SunBakeIcon size={14} /> Bake capture
            </button>
            <button type="button" disabled>
              Load image
            </button>
          </div>
        </section>
      ))}
    </div>
  );

  return (
    <div className="sun-studio-shell">
      <header className="pz-sun-header">
        <div className="pz-sun-header__top">
          <span className="pz-sun-header__crumb">Inspector / Environment</span>
          <span className="sun-pill-tag">NOAA Solar Studio</span>
        </div>
        <div className="pz-sun-header__main">
          <SunIconBadge icon={SunEntityIcon} accent="#eab965" size={22} />
          <div>
            <h1>{Subject.Name}</h1>
            <p>{Subject.Description}</p>
          </div>
        </div>
        {EntityNotesSlot}
      </header>

      <div className="sun-studio-root">
        {/* 1. Live NOAA Sky Banner + Time-Preset Pill Carousel */}
        <div className="sun-sky-banner">
          <div
            className="sun-sky-banner__stage"
            title="Drag horizontally to scrub Local Hours"
            onPointerDown={(e) => {
              e.currentTarget.setPointerCapture(e.pointerId);
              skyDragRef.current = true;
              scrubSky(e.clientX, e.currentTarget.getBoundingClientRect());
            }}
            onPointerMove={(e) => {
              if (skyDragRef.current)
                scrubSky(e.clientX, e.currentTarget.getBoundingClientRect());
            }}
            onPointerUp={() => {
              skyDragRef.current = false;
            }}
            onPointerCancel={() => {
              skyDragRef.current = false;
            }}
          >
            <svg viewBox="0 0 620 132" preserveAspectRatio="none" aria-hidden="true">
              <defs>
                <linearGradient id={`${uid}-pz-sky`} x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stopColor={zenith} />
                  <stop offset="100%" stopColor={horizon} />
                </linearGradient>
                <radialGradient id={`${uid}-pz-bloom`} cx="50%" cy="50%" r="50%">
                  <stop offset="0%" stopColor={apparentColor} stopOpacity={elevation < 0 ? 0.2 : 0.5} />
                  <stop offset="100%" stopColor={apparentColor} stopOpacity="0" />
                </radialGradient>
              </defs>
              <rect x="0" y="0" width="620" height={horizonY} fill={`url(#${uid}-pz-sky)`} />
              {elevation < -2 &&
                Array.from({ length: 24 }, (_, i) => (
                  <circle
                    key={i}
                    cx={20 + ((i * 179) % 580)}
                    cy={12 + ((i * 61) % 80)}
                    r={i % 3 === 0 ? 1.2 : 0.85}
                    fill="#dce7ff"
                    opacity={Math.min(0.6, (-elevation / 24) * 0.45)}
                  />
                ))}
              <circle cx={sunX} cy={sunY} r="54" fill={`url(#${uid}-pz-bloom)`} />
              {sunY < horizonY + 12 && (
                <>
                  <circle cx={sunX} cy={sunY} r="9.5" fill={apparentColor} />
                  <circle cx={sunX} cy={sunY} r="5" fill="#ffffff" opacity="0.65" />
                </>
              )}
              <rect x="0" y={horizonY} width="620" height={132 - horizonY} fill="#0a0b0e" />
              <line
                x1="0"
                y1={horizonY}
                x2="620"
                y2={horizonY}
                stroke="rgba(255,255,255,0.14)"
                strokeWidth="1"
              />
            </svg>
            <div className="sun-sky-banner__hud">
              <div>
                <span className="sun-sky-banner__phase">{phase}</span>
                <span className="sun-sky-banner__meta">
                  {clock.text} ·{" "}
                  {useKelvin ? `${temperature.toLocaleString("en-US")} K` : sunTint.toUpperCase()}
                </span>
              </div>
              <span className="sun-sky-banner__bearing">
                {elevation >= 0 ? `+${elevation.toFixed(0)}°` : `−${Math.abs(elevation).toFixed(0)}°`} ·{" "}
                {compassLabel(azimuth)} {Math.round(azimuth)}°
              </span>
            </div>
          </div>

          <div className="sun-preset-carousel" role="group" aria-label="Solar hour presets">
            {PZ_TIME_PRESETS.map((preset) => {
              const active = Math.abs(localHours - preset.hours) <= 0.75;
              return (
                <button
                  key={preset.id}
                  type="button"
                  className={`sun-preset-pill ${active ? "is-active" : ""}`}
                  aria-pressed={active}
                  onClick={() => AssignProperty("Local Hours", preset.hours)}
                >
                  <i
                    style={{
                      background:
                        preset.hours === 0
                          ? "#7f93c8"
                          : preset.hours >= 17 || preset.hours <= 6.5
                            ? "#f09e54"
                            : "#f2cf7d",
                    }}
                  />
                  <span>{preset.label}</span>
                  <small>{formatClockHours(preset.hours).text}</small>
                </button>
              );
            })}
          </div>
        </div>

        {/* 2. 3-Up Accent-Bar Bento KPI Strip (Reference Image 1) */}
        <div className="sun-kpi-strip" role="group" aria-label="Solar KPI summary">
          <button
            type="button"
            className={`sun-kpi-card ${categoryIndex === 0 && viewMode === "carousel" ? "is-selected" : ""}`}
            style={{ "--kpi-accent": "#eab965" }}
            onClick={() => selectCategory(0)}
          >
            <div className="sun-kpi-card__head">
              <span className="sun-kpi-card__bar" />
              <span>Orbit</span>
            </div>
            <div className="sun-kpi-card__delta">
              {elevation >= 0 ? "▲ Above" : "▼ Below"}
            </div>
            <div className="sun-kpi-card__main">
              <strong>{elevation.toFixed(0)}°</strong>
              <small>{compassLabel(azimuth)}</small>
            </div>
          </button>

          <button
            type="button"
            className={`sun-kpi-card ${categoryIndex === 1 && viewMode === "carousel" ? "is-selected" : ""}`}
            style={{ "--kpi-accent": "#f0ca72" }}
            onClick={() => selectCategory(1)}
          >
            <div className="sun-kpi-card__head">
              <span className="sun-kpi-card__bar" />
              <span>Photometry</span>
            </div>
            <div className="sun-kpi-card__delta">
              ● {useKelvin ? `${(temperature / 1000).toFixed(1)}kK` : "RGB"}
            </div>
            <div className="sun-kpi-card__main">
              <strong>
                {(intensity * direct).toFixed(1)}
                <em>×</em>
              </strong>
              <small>{angularDiameter.toFixed(2)}°</small>
            </div>
          </button>

          <button
            type="button"
            className={`sun-kpi-card ${categoryIndex === 2 && viewMode === "carousel" ? "is-selected" : ""}`}
            style={{ "--kpi-accent": "#8fc6b4" }}
            onClick={() => selectCategory(2)}
          >
            <div className="sun-kpi-card__head">
              <span className="sun-kpi-card__bar" />
              <span>Ephemeris</span>
            </div>
            <div className="sun-kpi-card__delta">
              ● {animate ? "Dynamic" : "Static"}
            </div>
            <div className="sun-kpi-card__main">
              <strong>{solved.Declination.toFixed(0)}°</strong>
              <small>
                {V("Month")}/{V("Day of Month")}
              </small>
            </div>
          </button>
        </div>

        {/* 3. Category Tabs + Carousel Controls + Segmented Progress Rail */}
        <div className="sun-carousel-nav">
          <div className="sun-carousel-tabs" role="tablist" aria-label="Sun categories">
            {PZ_CATEGORIES.map((cat, idx) => {
              const CatIcon = cat.icon;
              const active = viewMode === "carousel" && categoryIndex === idx;
              return (
                <button
                  key={cat.id}
                  type="button"
                  role="tab"
                  aria-selected={active}
                  className={`sun-carousel-tab ${active ? "is-active" : ""}`}
                  style={{ "--tab-accent": cat.accent }}
                  onClick={() => {
                    if (viewMode !== "carousel") setViewMode("carousel");
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
                className={viewMode === "carousel" ? "is-active" : ""}
                onClick={() => setViewMode("carousel")}
              >
                Carousel
              </button>
              <button
                type="button"
                className={viewMode === "all" ? "is-active" : ""}
                onClick={() => setViewMode("all")}
              >
                All
              </button>
            </div>

            {viewMode === "carousel" && (
              <div className="sun-carousel-steppers">
                <span className="sun-carousel-index">
                  0{categoryIndex + 1} / 0{PZ_CATEGORIES.length}
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

        {viewMode === "carousel" && (
          <div className="sun-segment-rail" aria-hidden="true">
            {PZ_CATEGORIES.map((cat, idx) => (
              <button
                key={cat.id}
                type="button"
                tabIndex={-1}
                className={categoryIndex === idx ? "is-active" : ""}
                style={{ "--seg-accent": cat.accent }}
                onClick={() => selectCategory(idx)}
              />
            ))}
          </div>
        )}

        {/* 4. Carousel Slides */}
        {viewMode === "all" ? (
          <div className="sun-all-stack">
            {trajectorySection}
            {photometrySection}
            {ephemerisSection}
            {bakingSection}
          </div>
        ) : (
          <div
            className="sun-carousel-viewport"
            onPointerDown={(e) => {
              if (e.target.closest("input, button, select, .gizmo, svg")) return;
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
              <div
                className="sun-carousel-slide"
                onFocusCapture={() => selectCategory(0)}
              >
                {trajectorySection}
              </div>
              <div
                className="sun-carousel-slide"
                onFocusCapture={() => selectCategory(1)}
              >
                {photometrySection}
              </div>
              <div
                className="sun-carousel-slide"
                onFocusCapture={() => selectCategory(2)}
              >
                {ephemerisSection}
              </div>
              <div
                className="sun-carousel-slide"
                onFocusCapture={() => selectCategory(3)}
              >
                {bakingSection}
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
