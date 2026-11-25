// The "Market prices" card: today's Agmarknet prices for one commodity (and one variety when it is known), as a median over the
// markets that quoted it, plus the individual quotes. Prices come from the server as whole paise per kg, which is the same number as
// whole rupees per quintal. Used on the buyer pool page, the Pool Manager's pool page and Create pool page, and the farmer statement.
import { h, clear } from './dom.js';
import { t } from './i18n.js';
import { api } from './api.js';
import { button, skeleton } from './components.js';
import { formatPricePerQuintalWhole, formatDate, formatDateTime } from './format.js';

const rupees = (paisePerKg) => formatPricePerQuintalWhole(paisePerKg);
const isoToShort = (iso) => formatDate(iso);   // "2026-11-25" -> "25 Nov"
const markets = (n) => t(n === 1 ? 'market.markets_one' : 'market.markets_many', { n });

// marketCard({ emptyKey }) -> { root, show(commodityId, variety) }. Call show() again when the commodity or variety changes.
export function marketCard({ emptyKey = 'market.none' } = {}) {
  const body = h('div', { class: 'stack' });
  const root = h('section', { class: 'card stack', id: 'market-card' }, h('p', { class: 'label' }, t('market.title')), body);
  let showAll = false;
  let last = null;
  let ticket = 0;

  function paint(data) {
    clear(body);
    if (!data || !data.as_of) { body.append(h('p', { class: 'muted' }, t(emptyKey))); return; }
    const c = data.commodity;
    const v = data.variety;
    const headline = v || c;
    body.append(
      h('div', { class: 'stack stack--tight' },
        h('p', {}, h('span', { class: 'key-number' }, rupees(headline.median_modal_paise_per_kg)), h('span', { class: 'key-unit' }, ` ${t('unit.per_quintal')}`)),
        h('p', { class: 'small muted' }, v
          ? t('market.median_variety', { variety: last.variety, markets: markets(v.markets) })
          : t('market.median_all', { markets: markets(c.markets) })),
        v && c ? h('p', { class: 'small muted' }, t('market.all_varieties', { price: rupees(c.median_modal_paise_per_kg), markets: markets(c.markets) })) : null,
        h('p', { class: 'small muted' }, t('market.range', { low: rupees(headline.lowest_min_paise_per_kg), high: rupees(headline.highest_max_paise_per_kg) })),
        h('p', { class: 'small muted' }, t('market.as_of', { date: isoToShort(data.as_of), time: formatDateTime(data.fetched_at) }))));
    const quotes = data.quotes;
    const shown = showAll ? quotes : quotes.slice(0, 6);
    const L = { market: t('market.col_market'), variety: t('market.col_variety'), grade: t('market.col_grade'), min: t('market.col_min'), max: t('market.col_max'), modal: t('market.col_modal') };
    body.append(h('table', { class: 'table table--stack' },
      h('thead', {}, h('tr', {}, h('th', {}, L.market), h('th', {}, L.variety), h('th', {}, L.grade), h('th', { class: 'num' }, L.min), h('th', { class: 'num' }, L.max), h('th', { class: 'num' }, L.modal))),
      h('tbody', {}, shown.map((q) => h('tr', {},
        h('td', { 'data-label': L.market }, q.market), h('td', { 'data-label': L.variety }, q.variety), h('td', { 'data-label': L.grade }, q.market_grade),
        h('td', { class: 'num', 'data-label': L.min }, rupees(q.min_paise_per_kg)), h('td', { class: 'num', 'data-label': L.max }, rupees(q.max_paise_per_kg)),
        h('td', { class: 'num', 'data-label': L.modal }, h('strong', {}, rupees(q.modal_paise_per_kg))))))));
    if (quotes.length > 6) body.append(h('div', {}, button(showAll ? t('market.show_fewer') : t('market.show_all', { n: quotes.length }), { onclick: () => { showAll = !showAll; paint(data); } })));
    body.append(h('p', { class: 'small muted' }, t('market.source')));
  }

  async function show(commodityId, variety = null) {
    const mine = ++ticket;
    last = { commodityId, variety };
    clear(body).append(skeleton(2));
    try {
      const query = `/market-prices?commodity_id=${encodeURIComponent(commodityId)}${variety ? `&variety=${encodeURIComponent(variety)}` : ''}`;
      const data = await api.get(query);
      if (mine === ticket) paint(data);
    } catch {
      if (mine === ticket) clear(body).append(h('p', { class: 'notice notice--bad' }, t('market.error')));
    }
  }
  return { root, show };
}
