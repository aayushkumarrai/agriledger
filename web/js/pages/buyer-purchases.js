// Buyer: my purchases, only the buyer's own receipts.
import { startPage } from '../layout.js';
import { t } from '../i18n.js';
import { api } from '../api.js';
import { h, clear, debounce, append } from '../dom.js';
import { loadInto, unitToggle, onUnitChange, poolCode } from '../components.js';
import { formatQuantity, formatPricePerQuintal, formatPaise, formatDateTime, padColumn, getUnit } from '../format.js';

let data = [];
const holder = h('div', {});
const refresh = debounce(() => load(true), 150);
const { main, note } = await startPage({
  role: 'buyer', active: 'purchases', titleKey: 'pages.buyer_purchases', onResync: () => load(true), liveHandlers: { my_purchase: refresh },
});
append(main, [
  h('div', { class: 'page-head' }, h('div', { class: 'stack stack--tight' }, h('h1', {}, t('buyer.purchases_title'))), h('div', { class: 'row' }, h('span', { class: 'muted' }, t('unit.show_in')), unitToggle())),
  holder, h('div', { class: 'row row--between' }, note, h('span', {}))]);

function table(rows) {
  const unit = getUnit();
  const L = { date: t('purchases.date'), pool: t('purchases.pool'), quantity: t(`purchases.quantity_${unit}`), price: t('purchases.price'), total: t('purchases.total'), receipt: t('purchases.receipt') };
  const quantities = padColumn(rows.map((r) => formatQuantity(r.quantity_g, unit)));
  clear(holder).append(h('div', { class: 'card card--table' }, h('table', { class: 'table table--stack' },
    h('thead', {}, h('tr', {}, h('th', {}, t('purchases.date')), h('th', {}, t('purchases.pool')), h('th', { class: 'num' }, t(`purchases.quantity_${unit}`)), h('th', { class: 'num' }, t('purchases.price')), h('th', { class: 'num' }, t('purchases.total')), h('th', { class: 'num' }, t('purchases.receipt')))),
    h('tbody', {}, rows.map((r, i) => h('tr', {},
      h('td', { class: 'nowrap', 'data-label': L.date }, formatDateTime(r.at)), h('td', { 'data-label': L.pool }, `${poolCode(r.pool_id)} · ${r.commodity} · ${r.variety}`),
      h('td', { class: 'num', 'data-label': L.quantity }, quantities[i]), h('td', { class: 'num', 'data-label': L.price }, formatPricePerQuintal(r.unit_price_paise_per_kg)),
      h('td', { class: 'num', 'data-label': L.total }, h('strong', {}, formatPaise(r.gross_paise))), h('td', { class: 'num nowrap', 'data-label': L.receipt }, r.receipt_no)))))));
}

async function load(quiet = false) {
  const fetcher = async () => { data = await api.get('/me/purchases'); return data; };
  if (quiet) { try { await fetcher(); data.length ? table(data) : null; } catch { /* the page keeps what it shows */ } return; }
  await loadInto(holder, { fetcher, isEmpty: (d) => d.length === 0, emptyText: t('purchases.empty'), emptyAction: h('a', { class: 'btn btn--secondary', href: '/buyer/pools.html' }, t('purchases.browse')), errorText: t('purchases.error'), render: table });
}
onUnitChange(() => { if (data.length) table(data); });
await load();
document.documentElement.dataset.ready = 'true';
