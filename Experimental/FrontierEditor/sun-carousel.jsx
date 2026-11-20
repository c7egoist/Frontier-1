import React, { useId, useRef, useState } from 'react';
import { Activity, CalendarDays, ChevronLeft, ChevronRight, CircleDot, Compass, Sunrise, Thermometer, Zap } from 'lucide-react';
import './sun-carousel.css';

export const SolarIcons = { direction: Compass, intensity: Zap, temperature: Thermometer, cycle: Sunrise, disc: CircleDot, settings: CalendarDays };

export default function SunCarousel({ pages }) {
  const [active, select] = useState(0);
  const id = useId();
  const tabs = useRef([]);
  const go = (index) => select((index + pages.length) % pages.length);
  const onKey = (event) => {
    const next = { ArrowLeft: active - 1, ArrowRight: active + 1, Home: 0, End: pages.length - 1 }[event.key];
    if (next === undefined) return;
    event.preventDefault();
    const index = (next + pages.length) % pages.length;
    go(index);
    tabs.current[index]?.focus();
  };
  return <section className="sun-carousel" aria-label="Solar controls">
    <header className="solar-dashboard-header">
      <div><span className="solar-kicker"><Activity size={13} /> ENVIRONMENT / SOLAR</span><h2>Solar controls</h2><p>{pages[active].description}</p></div>
      <div className="solar-pagination" aria-label="Category navigation">
        <button type="button" aria-label="Previous solar category" onClick={() => go(active - 1)}><ChevronLeft size={16} /></button>
        <span aria-live="polite">{active + 1} / {pages.length}</span>
        <button type="button" aria-label="Next solar category" onClick={() => go(active + 1)}><ChevronRight size={16} /></button>
      </div>
    </header>
    <div className="solar-tabs" role="tablist" aria-label="Solar categories" onKeyDown={onKey}>
      {pages.map(({ label, icon }, index) => {
        const Icon = SolarIcons[icon];
        return <button type="button" key={label} ref={node => { tabs.current[index] = node; }} role="tab" id={`${id}-tab-${index}`} aria-selected={index === active} aria-controls={`${id}-page-${index}`} tabIndex={index === active ? 0 : -1} onClick={() => go(index)}><Icon size={15} />{label}</button>;
      })}
    </div>
    {pages.map(({ label, content }, index) => <div className="solar-page" role="tabpanel" key={label} id={`${id}-page-${index}`} aria-labelledby={`${id}-tab-${index}`} hidden={index !== active} tabIndex={0}>{index === active ? content : null}</div>)}
  </section>;
}
