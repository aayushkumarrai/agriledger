// The summary strip at the top of a home page: three or four figures, each with a label and a short note. Figures are derived from
// data the page already has (no new numbers are stored). A note can be text or an element, for example a link.
import { h } from './dom.js';

export function statTile({ label, value, unit = null, note = null }) {
  return h('div', { class: 'stat' },
    h('span', { class: 'stat__label' }, label),
    h('span', { class: 'stat__value' }, value, unit ? h('small', {}, unit) : null),
    note ? h('span', { class: 'stat__note' }, note) : null);
}

export const summaryStrip = (tiles) => h('div', { class: `summary${tiles.length === 3 ? ' summary--three' : ''}` }, tiles.map(statTile));

// "3 pools" / "1 pool": one word form per count
export const poolsCount = (t, n) => t(n === 1 ? 'summary.pools_one' : 'summary.pools_many', { n });
