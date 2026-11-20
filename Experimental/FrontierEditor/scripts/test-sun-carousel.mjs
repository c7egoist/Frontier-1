import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { JSDOM } from 'jsdom';

const html = await readFile(new URL('../../ProjectZeroEditor/index.html', import.meta.url), 'utf8');
const dom = new JSDOM(html, {
  runScripts: 'dangerously', url: 'http://localhost:8092/', pretendToBeVisual: true,
  beforeParse(window) {
    window.ResizeObserver = class { observe() {} disconnect() {} };
    window.HTMLElement.prototype.scrollIntoView = () => {};
    window.HTMLCanvasElement.prototype.getContext = () => null;
  },
});
try {
  const { document, KeyboardEvent } = dom.window;
  const flush = () => new Promise(resolve => setTimeout(resolve, 60));
  await flush();
  const tabs = [...document.querySelectorAll('.solar-tabs [role=tab]')];
  const active = () => tabs.find(tab => tab.getAttribute('aria-selected') === 'true');
  assert.deepEqual(tabs.map(tab => tab.textContent), ['Position', 'Light', 'Cycle', 'Bake']);
  assert.equal(active(), tabs[0]);
  tabs[0].dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true, cancelable: true }));
  await flush();
  assert.equal(active(), tabs[1]);
  assert.equal(document.activeElement, tabs[1]);
  const visibleCards = () => [...document.querySelectorAll('.solar-page:not([hidden]) [data-card]')].map(card => card.dataset.card);
  assert.deepEqual(visibleCards(), ['Sunlight intensity', 'Temperature', 'Sun disc']);
  const next = () => document.querySelector('[aria-label="Next solar category"]').click();
  next(); await flush();
  assert.deepEqual(visibleCards(), ['Daylight cycle', 'Dynamic settings']);
  tabs[3].click(); await flush();
  assert.deepEqual(visibleCards(), ['Sun lighting bake', 'Sun disk bake']);
  next(); await flush();
  assert.equal(active(), tabs[0]);
  console.log('Solar carousel: tab selection, keyboard focus, grouping and wraparound passed');
} finally {
  dom.window.close();
}
