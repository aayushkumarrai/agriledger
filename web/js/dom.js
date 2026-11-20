// A tiny DOM builder. Text from anywhere (including the server: farmer names, varieties, places) is always added as text,
// never parsed as markup, so nothing a user typed can become HTML or script.
import { iconMarkup } from './icons.js';

export function append(el, children) {
  for (const c of children.flat(Infinity)) {
    if (c === null || c === undefined || c === false) continue;
    el.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
  return el;
}

// h('button', { class: 'btn', type: 'button', onclick: fn, disabled: true }, 'text', childElement)
export function h(tag, attrs = {}, ...children) {
  const el = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs || {})) {
    if (value === false || value === null || value === undefined) continue;
    if (key === 'class') el.className = value;
    else if (key === 'dataset') Object.assign(el.dataset, value);
    else if (key.startsWith('on') && typeof value === 'function') el.addEventListener(key.slice(2).toLowerCase(), value);
    else if (value === true) el.setAttribute(key, '');
    else el.setAttribute(key, String(value));
  }
  return append(el, children);
}

export function clear(el) {
  el.replaceChildren();
  return el;
}

export function icon(name, extraClass = '') {
  const holder = document.createElement('template');
  holder.innerHTML = iconMarkup(name, extraClass);
  return holder.content.firstElementChild;
}

// A changed number fades in over 300 ms; nothing moves (spec §8.2 rule 8).
export function flash(el) {
  el.classList.remove('is-changed');
  void el.offsetWidth;   // restart the animation
  el.classList.add('is-changed');
}

export function setValue(el, text) {
  if (el.textContent !== text) {
    el.textContent = text;
    flash(el);
  }
}

export function debounce(fn, ms) {
  let timer = null;
  return (...args) => { clearTimeout(timer); timer = setTimeout(() => fn(...args), ms); };
}
