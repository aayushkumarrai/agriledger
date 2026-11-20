// Pool Manager home: all pools in one table, with filters; the records badge turns red, with words, if any totals stop matching.
import { startPage } from '../layout.js';
import { t } from '../i18n.js';
import { api } from '../api.js';
import { h, icon, clear, debounce, append } from '../dom.js';
import { skeleton, errorState, emptyState, unitToggle, onUnitChange, poolStatusTag, recordsTag, poolCode } from '../components.js';
import { formatQuantity, formatPricePerQuintal, padColumn, getUnit } from '../format.js';

let pools = [];
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
append(main, [
  h('div', { class: 'page-head' },
    h('div', { class: 'stack stack--tight' }, h('p', { class: 'label' }, t('manager.eyebrow')), h('h1', {}, t('nav.pools'))),
    h('a', { class: 'btn btn--primary', href: '/manager/create.html' }, icon('plus'), h('span', {}, t('manager.create_pool')))),
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
        h('td', { 'data-label': L.range }, t('pool.range', { n: p.range_no })), h('td', { class: 'num', 'data-label': L.price }, formatPricePerQuintal(p.price_paise_per_kg)),
        h('td', { class: 'num', 'data-label': L.stock }, stocks[i]), h('td', { class: 'num', 'data-label': L.farmers }, p.farmer_count),
        h('td', { 'data-label': L.status }, poolStatusTag(p)), h('td', { 'data-label': L.records }, recordsTag(p.records)));
      return tr;
    })))));
}

async function load(quiet = false) {
  if (!quiet) clear(holder).append(skeleton());
  try { pools = await api.get('/pools'); paint(); }
  catch { if (!quiet) clear(holder).append(errorState(t('buyer.pools_error'), () => load())); }
}
commodityFilter.addEventListener('change', paint);
statusFilter.addEventListener('change', paint);
onUnitChange(() => { if (pools.length) paint(); });
await load();
document.documentElement.dataset.ready = 'true';
