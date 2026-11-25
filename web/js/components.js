// Reusable pieces built from the design system. All text comes from i18n by key.
import { h, icon, clear, append } from './dom.js';
import { t } from './i18n.js';
import { getUnit, setUnit } from './format.js';

// ---- status tags: words always, an icon for good and bad -----------------------------------------------------------------
export function tag(kind, text, iconName = null) {
  return h('span', { class: `tag tag--${kind}` }, iconName ? icon(iconName) : null, h('span', {}, text));
}

// The pool's status as a person reads it. `pool` needs status, stock_g and (for drafts) photos.
export function poolStatusTag(pool) {
  if (pool.status === 'draft') return (pool.photos || []).length ? tag('grey', t('status.draft'), 'clock') : tag('grey', t('status.draft_needs_photo'), 'clock');
  if (pool.status === 'closed') return tag('grey', t('status.closed'));
  if (BigInt(pool.stock_g) === 0n) return tag('grey', t('status.out_of_stock'));
  return tag('ok', t('status.open'));
}

export function recordsTag(state) {
  if (state === 'ok' || state === true) return tag('ok', t('status.reconciled'), 'check');
  if (state === 'mismatch' || state === false) return tag('bad', t('status.mismatch'), 'cross');
  return h('span', { class: 'muted small' }, t('status.no_records'));
}

// ---- buttons --------------------------------------------------------------------------------------------------------
export function button(label, { kind = 'secondary', onclick, type = 'button', disabled = false, block = false, id } = {}) {
  return h('button', { class: `btn btn--${kind}${block ? ' btn--block' : ''}`, type, onclick, disabled, id }, label);
}

// True while a button's work is running: handlers call this first so a double tap or a second Enter does nothing.
export const isBusy = (btn) => btn.getAttribute('aria-busy') === 'true';

// Shows "Working…" with a clock while a promise runs. The button keeps its look (it is not disabled) but ignores taps
// (CSS pointer-events) and isBusy() lets keyboard submits be ignored too. Returns the promise's result.
export async function busy(btn, work) {
  const label = [...btn.childNodes];
  btn.setAttribute('aria-busy', 'true');
  btn.replaceChildren(icon('clock'), h('span', {}, t('common.working')));
  try {
    return await work();
  } finally {
    btn.removeAttribute('aria-busy');
    btn.replaceChildren(...label);
  }
}

// ---- form fields ----------------------------------------------------------------------------------------------------
// field({ id, label, input, hint }) -> { root, input, error(text|null) }. The label sits above the field, never inside it.
export function field({ id, label, input, hint = null }) {
  const err = h('p', { class: 'field__error', id: `${id}-error`, hidden: true, role: 'alert' }, icon('cross'), h('span', {}));
  const el = input;
  el.id = id;
  el.classList.add(el.tagName === 'SELECT' ? 'select' : 'input');
  const hintEl = hint ? h('p', { class: 'field__hint', id: `${id}-hint` }, hint) : null;
  const root = h('div', { class: 'field' }, h('label', { class: 'field__label', for: id }, label), el.tagName === 'SELECT' ? selectWrap(el) : el, hintEl, err);   // a select gets its chevron
  el.setAttribute('aria-describedby', [hint ? `${id}-hint` : '', `${id}-error`].filter(Boolean).join(' '));
  return {
    root, input: el, hintEl,
    error(text) {
      err.hidden = !text;
      err.lastElementChild.textContent = text || '';
      if (text) el.setAttribute('aria-invalid', 'true'); else el.removeAttribute('aria-invalid');
    },
  };
}

export function selectBox(options, { value } = {}) {
  const select = h('select', {}, options.map((o) => h('option', { value: o.value, selected: o.value === value }, o.label)));
  return select;
}

// A select with its chevron. Wrap the result of field() input in this before adding it to a field.
export function selectWrap(select) {
  return h('div', { class: 'select-wrap' }, select, icon('chevron-down'));
}

// ---- the kg / quintal toggle: one choice for the whole browser ------------------------------------------------------
export function unitToggle() {
  const make = (unit) => h('button', { class: 'toggle__opt', type: 'button', dataset: { unit }, 'aria-pressed': String(getUnit() === unit) }, t(`unit.${unit}`));
  const root = h('div', { class: 'toggle', role: 'group', 'aria-label': t('unit.toggle_label') }, make('kg'), make('quintal'));
  const show = (unit) => { for (const b of root.querySelectorAll('[data-unit]')) b.setAttribute('aria-pressed', String(b.dataset.unit === unit)); };
  root.addEventListener('click', (e) => {
    const b = e.target.closest('[data-unit]');
    if (!b) return;
    setUnit(b.dataset.unit);
    document.dispatchEvent(new CustomEvent('unitchange', { detail: b.dataset.unit }));
  });
  document.addEventListener('unitchange', (e) => show(e.detail));
  return root;
}

export function onUnitChange(fn) {
  document.addEventListener('unitchange', (e) => fn(e.detail));
}

// ---- the four states every screen has -------------------------------------------------------------------------------
export function skeleton(lines = 3) {
  const widths = ['skeleton--key', '', 'skeleton--mid', 'skeleton--short'];
  return h('div', { class: 'card stack', role: 'status', 'aria-label': t('common.loading') },
    Array.from({ length: lines }, (_, i) => h('span', { class: `skeleton ${widths[i % widths.length]}` })));
}

export function emptyState(text, action = null) {
  return h('div', { class: 'card state' }, icon('image', 'icon--lg muted'), h('p', { class: 'state__text' }, text), action);
}

export function errorState(text, retry) {
  return h('div', { class: 'card state' },
    h('p', { class: 'notice notice--bad', role: 'alert' }, icon('alert'), h('span', {}, text)),
    retry ? button(t('common.retry'), { onclick: retry }) : null);
}

// Runs fetcher() and fills `container`: skeleton while loading, an error with retry, the empty message, or render(data).
export async function loadInto(container, { fetcher, render, isEmpty = () => false, emptyText = t('table.empty'), emptyAction = null, errorText = t('table.error') }) {
  clear(container).append(skeleton());
  try {
    const data = await fetcher();
    clear(container);
    if (isEmpty(data)) container.append(emptyState(emptyText, emptyAction));
    else render(data);
    return data;
  } catch {
    clear(container);
    container.append(errorState(errorText, () => loadInto(container, { fetcher, render, isEmpty, emptyText, emptyAction, errorText })));
    return null;
  }
}

// ---- pop-up ---------------------------------------------------------------------------------------------------------
// openModal(buildContent) -> { close, root }. buildContent(close) returns the elements to show. Esc and a tap outside close it
// unless `locked()` says a request is running. Focus goes to the first button and returns afterwards.
export function openModal(build, { locked = () => false } = {}) {
  const previous = document.activeElement;
  const dialog = h('div', { class: 'modal', role: 'dialog', 'aria-modal': 'true' });
  const root = h('div', { class: 'backdrop' }, dialog);
  const close = () => {
    document.removeEventListener('keydown', onKey);
    root.remove();
    document.body.classList.remove('has-modal');
    if (previous && previous.focus) previous.focus();
  };
  const onKey = (e) => { if (e.key === 'Escape' && !locked()) close(); };
  document.addEventListener('keydown', onKey);
  root.addEventListener('mousedown', (e) => { if (e.target === root && !locked()) close(); });
  const content = build(close, (next) => { clear(dialog); append(dialog, [].concat(next)); (dialog.querySelector('button, input, select') || dialog).focus(); });
  append(dialog, [].concat(content));
  document.body.classList.add('has-modal');
  document.body.append(root);
  (dialog.querySelector('button, input, select') || dialog).focus();
  return { close, root, dialog };
}

export function photoSlot(photo, caption = null) {
  return h('div', { class: 'photo' }, h('img', { src: photo.url, alt: t('photo.alt'), loading: 'lazy' }), caption ? h('span', { class: 'photo__caption' }, caption) : null);
}

export function emptyPhotoSlot() {
  return h('div', { class: 'photo photo--empty' }, h('span', {}, t('photo.slot')));
}

// "Pool P-12" style id used everywhere a pool is named
export const poolCode = (id) => `P-${id}`;

export function breadcrumb(parts) {
  return h('nav', { class: 'breadcrumb', 'aria-label': t('nav.breadcrumb') }, parts.map((p, i) => [i ? h('span', { 'aria-hidden': 'true' }, ' / ') : null, p.href ? h('a', { href: p.href }, p.label) : h('span', {}, p.label)]));
}

// "Category 1 (best)", "Category 2 (standard)", "Category 3 (basic)": the number says which quality limits apply; the word says where it sits.
export const gradeLabel = (n) => t(n === 1 ? 'pool.range_best' : n === 3 ? 'pool.range_basic' : n === 2 ? 'pool.range_standard' : 'pool.range', { n });
