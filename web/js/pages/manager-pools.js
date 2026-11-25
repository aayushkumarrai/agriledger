// Pool Manager home: all pools in one table, with filters; the records badge turns red, with words, if any totals stop matching.
import { gradeLabel } from '../components.js';
import { startPage } from '../layout.js';
import { t } from '../i18n.js';
import { api } from '../api.js';
import { h, icon, clear, debounce, append } from '../dom.js';
import { skeleton, errorState, emptyState, unitToggle, onUnitChange, poolStatusTag, recordsTag, poolCode, button, busy, isBusy } from '../components.js';
import { formatQuantity, formatPricePerQuintal, formatDateTime, padColumn, getUnit } from '../format.js';
import { summaryStrip, poolsCount } from '../summary.js';

let pools = [];
let users = [];
const statusLine = h('p', { class: 'page-status' });
const strip = h('div', {});
let commodity = '';
let status = '';
const holder = h('div', {});
let commodityFilter;
let statusFilter;

const refresh = debounce(() => load(true), 150);
const { main, note } = await startPage({
  role: 'manager', active: 'pools', titleKey: 'pages.manager_pools', onResync: () => load(true),
  liveHandlers: { stock_changed: refresh, price_changed: refresh, status_changed: refresh, reconciliation: refresh },
});
commodityFilter = h('select', { class: 'select', 'aria-label': t('buyer.commodity') });
statusFilter = h('select', { class: 'select', 'aria-label': t('manager.status_label') }, [['', t('manager.all_statuses')], ['draft', t('status.draft')], ['open', t('status.open')], ['out', t('status.out_of_stock')], ['closed', t('status.closed')]].map(([v, l]) => h('option', { value: v }, l)));
const wrap = (select) => h('div', { class: 'select-wrap tool-select' }, select, icon('chevron-down'));
// "Update market prices": the Pool Manager fetches today's Agmarknet prices. A failure changes nothing: the last saved prices stay.
const priceNote = h('p', { class: 'notice notice--grey', role: 'status', hidden: true }, icon('check'), h('span', {}));
const showPriceNote = (text, bad = false) => {
  priceNote.hidden = !text; priceNote.className = `notice notice--${bad ? 'bad' : 'grey'}`;
  priceNote.lastElementChild.textContent = text || ''; priceNote.firstElementChild.replaceWith(icon(bad ? 'cross' : 'check'));
};
const updatePrices = button(t('market.update'), { onclick: async () => {
  if (isBusy(updatePrices)) return;
  showPriceNote('');
  try {
    const result = await busy(updatePrices, () => api.post('/market-prices/refresh', {}));
    showPriceNote(t('market.updated', { n: result.imported }));
  } catch (err) {
    showPriceNote(err.code === 'PRICE_FEED_NOT_SET_UP' ? t('market.not_set_up') : err.code === 'TOO_SOON' ? t('market.too_soon') : t('market.unavailable'), true);
  }
} });

append(main, [
  h('div', { class: 'page-head' },
    h('div', { class: 'stack stack--tight' }, h('h1', {}, t('nav.pools')), statusLine),
    h('div', { class: 'row' }, updatePrices, h('a', { class: 'btn btn--primary', href: '/manager/create.html' }, icon('plus'), h('span', {}, t('manager.create_pool'))))),
  priceNote,
  strip,
  h('div', { class: 'page-head page-head--tools' }, h('div', { class: 'page-head__tools' }, wrap(commodityFilter), wrap(statusFilter)),
    h('div', { class: 'row' }, h('span', { class: 'muted' }, t('manager.show_stock_in')), unitToggle())),
  holder, h('p', { class: 'small muted' }, t('manager.pools_note')), h('div', { class: 'row row--between' }, note, h('span', {}))]);

const stateOf = (p) => (p.status === 'open' && BigInt(p.stock_g) === 0n ? 'out' : p.status);

function paint() {
  const unit = getUnit();
  const keep = commodityFilter.value;
  const names = [...new Set(pools.map((p) => p.commodity))];
  clear(commodityFilter).append(h('option', { value: '' }, t('buyer.all_commodities')), ...names.map((n) => h('option', { value: n }, n)));
  commodityFilter.value = names.includes(keep) ? keep : '';
  commodity = commodityFilter.value; status = statusFilter.value;
  const rows = pools.filter((p) => (!commodity || p.commodity === commodity) && (!status || stateOf(p) === status));
  if (!pools.length) { clear(holder).append(emptyState(t('manager.no_pools'), h('a', { class: 'btn btn--primary', href: '/manager/create.html' }, t('manager.create_pool')))); return; }
  if (!rows.length) { clear(holder).append(emptyState(t('manager.no_match'))); return; }
  const stocks = padColumn(rows.map((p) => formatQuantity(p.stock_g, unit)));
  const L = { pool: t('manager.col_pool'), what: t('manager.col_commodity'), range: t('manager.col_range'), price: t('manager.col_price'), stock: t(`manager.col_stock_${unit}`), farmers: t('manager.col_farmers'), status: t('manager.col_status'), records: t('manager.col_records') };
  clear(holder).append(h('div', { class: 'card card--table' }, h('table', { class: 'table table--rows table--stack' },
    h('thead', {}, h('tr', {}, h('th', {}, L.pool), h('th', {}, L.what), h('th', {}, L.range), h('th', { class: 'num' }, L.price), h('th', { class: 'num' }, L.stock), h('th', { class: 'num' }, L.farmers), h('th', {}, L.status), h('th', {}, L.records))),
    h('tbody', {}, rows.map((p, i) => {
      const go = () => location.assign(`/manager/pool.html?id=${p.pool_id}`);
      const tr = h('tr', { tabindex: '0', role: 'link', 'aria-label': `${t('pool.pool')} ${poolCode(p.pool_id)}`, onclick: go, onkeydown: (e) => { if (e.key === 'Enter') go(); } },
        h('td', { 'data-label': L.pool }, h('strong', { class: 'pool-code' }, poolCode(p.pool_id))), h('td', { 'data-label': L.what }, `${p.commodity} · ${p.variety}`),
        h('td', { 'data-label': L.range }, gradeLabel(p.range_no)), h('td', { class: 'num', 'data-label': L.price }, formatPricePerQuintal(p.price_paise_per_kg)),
        h('td', { class: 'num', 'data-label': L.stock }, stocks[i]), h('td', { class: 'num', 'data-label': L.farmers }, p.farmer_count),
        h('td', { 'data-label': L.status }, poolStatusTag(p)), h('td', { 'data-label': L.records }, recordsTag(p.records)));
      return tr;
    })))));
}

// the line under the title and the four figures: all taken from the pools and people the page already loads
function paintSummary() {
  const open = pools.filter((p) => p.status === 'open');
  const out = open.filter((p) => BigInt(p.stock_g) === 0n).length;
  const stock = open.reduce((s, p) => s + BigInt(p.stock_g), 0n);
  const waiting = users.filter((u) => u.status === 'pending').length;
  const bad = pools.filter((p) => p.records === 'mismatch').length;
  const lastSale = pools.map((p) => p.last_sale_at).filter(Boolean).sort().pop();
  statusLine.textContent = `${t('summary.open_line', { pools: poolsCount(t, open.length) })} · ${lastSale ? t('summary.last_sale', { when: formatDateTime(lastSale) }) : t('summary.no_sales')}`;
  clear(strip).append(summaryStrip([
    { label: t('summary.open_pools'), value: String(open.length), note: out ? t('summary.out_of_stock', { n: out }) : t('summary.all_in_stock') },
    { label: t('summary.stock_open'), value: formatQuantity(stock, getUnit()), unit: t(`unit.${getUnit()}`), note: t('summary.across_open') },
    { label: t('summary.waiting'), value: String(waiting), note: waiting ? h('a', { href: '/manager/people.html' }, t('summary.review')) : t('summary.no_one') },
    { label: t('summary.records'), value: bad ? t('summary.records_bad', { n: bad }) : t('summary.records_ok') },
  ]));
}

async function load(quiet = false) {
  if (!quiet) clear(holder).append(skeleton());
  try { pools = await api.get('/pools'); users = await api.get('/users').catch(() => users); paintSummary(); paint(); }
  catch { if (!quiet) clear(holder).append(errorState(t('buyer.pools_error'), () => load())); }
}
commodityFilter.addEventListener('change', paint);
statusFilter.addEventListener('change', paint);
onUnitChange(() => { if (pools.length) { paintSummary(); paint(); } });
await load();
document.documentElement.dataset.ready = 'true';
