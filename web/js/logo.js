// The AgriLedger mark (an open ledger with a sprout) and the wordmark. The colours come from CSS classes (tokens), so the same
// drawing works on paper (green square) and on the green brand panel (paper square, `inverse`).
import { h } from './dom.js';
import { t } from './i18n.js';

const NS = 'http://www.w3.org/2000/svg';
function node(name, attrs = {}, children = []) {
  const el = document.createElementNS(NS, name);
  for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, v);
  for (const c of children) el.append(c);
  return el;
}

export function logoMark({ inverse = false } = {}) {
  return node('svg', { class: `logo${inverse ? ' logo--inverse' : ''}`, viewBox: '0 0 48 48', 'aria-hidden': 'true', focusable: 'false' }, [
    node('rect', { class: 'logo__bg', width: '48', height: '48', rx: '9' }),
    node('path', { class: 'logo__line', d: 'M8 36 L23 38.5 V22 L8 19.5 Z' }),
    node('path', { class: 'logo__line', d: 'M40 36 L25 38.5 V22 L40 19.5 Z' }),
    node('path', { class: 'logo__line', d: 'M12 25.5 L19.5 26.8 M12 30 L19.5 31.3 M36 25.5 L28.5 26.8 M36 30 L28.5 31.3' }),
    node('path', { class: 'logo__line', d: 'M24 22 V13' }),
    node('path', { class: 'logo__leaf', d: 'M24 16 C19 16 17 12 17 9.5 C21 9.5 24 11.5 24 16 Z' }),
    node('path', { class: 'logo__leaf', d: 'M24 13.5 C29 13.5 31 9.5 31 7 C27 7 24 9 24 13.5 Z' }),
  ]);
}

// "AgriLedger": Agri in bold, Ledger in regular weight, after the mark.
export function wordmark({ inverse = false } = {}) {
  const [first, second] = t('app.name').split(/(?=L)/);
  return h('span', { class: `wordmark${inverse ? ' wordmark--inverse' : ''}` }, logoMark({ inverse }), h('span', { class: 'wordmark__text' }, h('b', {}, first), h('i', {}, second)));
}
