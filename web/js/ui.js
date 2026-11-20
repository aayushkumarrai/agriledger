// Small DOM helpers shared by every page: start-up, number rendering, the unit toggle, the phone menu.
import { injectSprite } from './icons.js';
import { loadMessages, applyI18n, t } from './i18n.js';
import {
  formatPaise, formatQuantity, formatPricePerQuintal, formatPricePerKg, formatPermille, formatDateTime, formatDate, getUnit, setUnit,
} from './format.js';

// Sprite, messages, text, phone menu. Call first on every page.
export async function initPage() {
  injectSprite();
  await loadMessages();
  applyI18n();
  wireTopbar();
}

// Tells tests (and screenshots) that the page has finished drawing.
export function markReady() {
  document.documentElement.dataset.ready = 'true';
}

// Elements carrying data-fmt="..." data-value="<integer>" are filled with exactly formatted text:
//   money (paise) · quantity (grams, in the chosen unit) · quantity-unit (same, with the unit name) · price-quintal and price-kg
//   (paise per kg) · permille · datetime (ISO; optional data-tz) · date (YYYY-MM-DD) · sold ("· 5.0 quintal sold").
// data-unit-label gets the unit name; data-i18n-unit="key" shows key_kg or key_quintal.
export function renderFormats(root = document, unit = getUnit()) {
  for (const el of root.querySelectorAll('[data-fmt]')) {
    const v = el.dataset.value;
    switch (el.dataset.fmt) {
      case 'money': el.textContent = formatPaise(v); break;
      case 'quantity': el.textContent = formatQuantity(v, unit); break;
      case 'quantity-unit': el.textContent = `${formatQuantity(v, unit)} ${t(`unit.${unit}`)}`; break;
      case 'price-quintal': el.textContent = formatPricePerQuintal(v); break;
      case 'price-kg': el.textContent = formatPricePerKg(v); break;
      case 'permille': el.textContent = formatPermille(v); break;
      case 'datetime': el.textContent = formatDateTime(v, el.dataset.tz); break;
      case 'date': el.textContent = formatDate(v); break;
      case 'sold': el.textContent = t('styleguide.type.sold', { quantity: `${formatQuantity(v, unit)} ${t(`unit.${unit}`)}` }); break;
      default: break;
    }
  }
  for (const el of root.querySelectorAll('[data-unit-label]')) el.textContent = t(`unit.${unit}`);
  for (const el of root.querySelectorAll('[data-i18n-unit]')) el.textContent = t(`${el.dataset.i18nUnit}_${unit}`);
}

// Every [data-unit-toggle] on the page shows the remembered unit; a tap on any of them changes all and calls onChange(unit).
export function bindUnitToggles(onChange = () => {}) {
  const toggles = [...document.querySelectorAll('[data-unit-toggle]')];
  const show = (unit) => {
    for (const toggle of toggles) {
      for (const opt of toggle.querySelectorAll('[data-unit]')) opt.setAttribute('aria-pressed', String(opt.dataset.unit === unit));
    }
  };
  show(getUnit());
  for (const toggle of toggles) {
    toggle.addEventListener('click', (event) => {
      const opt = event.target.closest('[data-unit]');
      if (!opt || opt.disabled) return;
      setUnit(opt.dataset.unit);
      show(opt.dataset.unit);
      onChange(opt.dataset.unit);
    });
  }
}

// Shows the real token value from tokens.css next to a swatch: <p data-token="--paper"></p> -> "#F4EEDF".
export function fillTokenHex(root = document) {
  const style = getComputedStyle(document.documentElement);
  for (const el of root.querySelectorAll('[data-token]')) el.textContent = style.getPropertyValue(el.dataset.token).trim().toUpperCase();
}

// The phone menu button opens and closes the navigation.
export function wireTopbar() {
  const bar = document.querySelector('.topbar');
  const button = document.querySelector('.topbar__menu');
  if (!bar || !button) return;
  button.addEventListener('click', () => {
    const open = bar.classList.toggle('is-open');
    button.setAttribute('aria-expanded', String(open));
  });
}
