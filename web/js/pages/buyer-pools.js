// Buyer home: available pools as cards. Numbers update in place from the live feed; an out-of-stock pool stays, greyed, for a day.
import { gradeLabel } from '../components.js';
import { startPage } from '../layout.js';
import { t } from '../i18n.js';
import { api } from '../api.js';
import { h, icon, clear, setValue, debounce, append } from '../dom.js';
import { skeleton, errorState, emptyState, unitToggle, onUnitChange, poolStatusTag, poolCode } from '../components.js';
import { formatQuantity, formatPricePerQuintalWhole, formatDate, getUnit } from '../format.js';

const cards = new Map();   // pool_id -> { root, update(pool) }
let pools = [];
let commodityFilter = '';
let grid;
let filterSelect;
let marketSelect;
let marketFilter = '';

function card(pool) {
  const photo = pool.photos[0] ? h('div', { class: 'photo photo--wide' }, h('img', { src: pool.photos[0].url, alt: t('photo.alt'), loading: 'lazy' })) : h('div', { class: 'photo photo--wide photo--empty' }, h('span', {}, t('photo.slot')));
  const title = h('h2', { class: 'pool-card__title' });
  const tagSlot = h('span', {});
  const sub = h('p', { class: 'muted small' });
  const price = h('span', { class: 'fig__value' });
  const stock = h('span', { class: 'fig__value' });
  const stockUnit = h('span', { class: 'key-unit' });
  const meta = h('p', { class: 'small muted' });
  const action = h('a', { class: 'btn btn--secondary btn--block', href: `/buyer/pool.html?id=${pool.pool_id}` }, t('buyer.view_and_buy'));
  const root = h('article', { class: 'card pool-card', dataset: { pool: pool.pool_id } }, photo,
    h('div', { class: 'pool-card__body' },
      h('div', { class: 'pool-card__content' },
        h('div', { class: 'pool-card__head' }, title, tagSlot), sub,
        h('div', { class: 'row row--between row--top' },
          h('div', { class: 'fig' }, h('p', { class: 'label' }, t('buyer.price')), h('p', {}, price, ' ', h('span', { class: 'key-unit' }, t('unit.per_quintal')))),
          h('div', { class: 'fig fig--right' }, h('p', { class: 'label' }, t('buyer.available')), h('p', {}, stock, ' ', stockUnit))),
        meta),
      action));

  function update(p) {
    const out = BigInt(p.stock_g) === 0n;
    title.textContent = `${p.commodity} · ${p.variety}`;
    clear(tagSlot).append(poolStatusTag(p));
    sub.textContent = `${t('pool.pool')} ${poolCode(p.pool_id)} · ${gradeLabel(p.range_no)}`;
    setValue(price, formatPricePerQuintalWhole(p.price_paise_per_kg));
    setValue(stock, formatQuantity(p.stock_g, getUnit()));
    stockUnit.textContent = t(`unit.${getUnit()}`);
    const farmers = t(p.farmer_count === 1 ? 'pool.from_farmer_one' : 'pool.from_farmers', { count: p.farmer_count });
    meta.replaceChildren(out
      ? farmers + (p.last_sale_at ? ' · ' + t('pool.last_sale', { date: formatDate(p.last_sale_at.slice(0, 10)) }) : '')
      : `${farmers} · ${t('pool.min_order', { qty: `${formatQuantity(p.min_order_g, getUnit())} ${t(`unit.${getUnit()}`)}` })}`,
      h('br'), t('pool.stored_at', { place: p.storage_location }));
    root.classList.toggle('pool-card--grey', out);
    action.textContent = out ? t('status.out_of_stock') : t('buyer.view_and_buy');
    action.classList.toggle('is-disabled', out);
    if (out) { action.setAttribute('aria-disabled', 'true'); action.removeAttribute('href'); action.tabIndex = -1; }
    else { action.setAttribute('href', `/buyer/pool.html?id=${p.pool_id}`); action.removeAttribute('aria-disabled'); action.removeAttribute('tabindex'); }
  }
  update(pool);
  return { root, update };
}

function show(list) {
  pools = list;
  const names = [...new Set(list.map((p) => p.commodity))];
  const keep = filterSelect.value;
  clear(filterSelect).append(h('option', { value: '' }, t('buyer.all_commodities')), ...names.map((n) => h('option', { value: n }, n)));
  filterSelect.value = names.includes(keep) ? keep : '';
  commodityFilter = filterSelect.value;
  const markets = [...new Set(list.map((p) => p.market).filter(Boolean))].sort();
  const keepMarket = marketSelect.value;
  clear(marketSelect).append(h('option', { value: '' }, t('buyer.all_markets')), ...markets.map((n) => h('option', { value: n }, n)));
  marketSelect.value = markets.includes(keepMarket) ? keepMarket : '';
  marketFilter = marketSelect.value;
  marketWrap.hidden = markets.length < 2;   // a filter is only useful when there is a choice
  paint();
}

function paint() {
  // pools with stock first (newest first), sold-out pools after them, as in the wireframe
  const visible = pools.filter((p) => (!commodityFilter || p.commodity === commodityFilter) && (!marketFilter || p.market === marketFilter))
    .sort((a, b) => (BigInt(b.stock_g) > 0n) - (BigInt(a.stock_g) > 0n) || b.pool_id - a.pool_id);
  const seen = new Set();
  const ordered = [];
  for (const p of visible) {
    seen.add(p.pool_id);
    if (!cards.has(p.pool_id)) cards.set(p.pool_id, card(p)); else cards.get(p.pool_id).update(p);
    ordered.push(cards.get(p.pool_id).root);
  }
  for (const id of [...cards.keys()]) if (!seen.has(id)) cards.delete(id);
  clear(grid);
  if (!ordered.length) grid.append(emptyState(t('buyer.no_pools')));
  else append(grid, ordered);
}

async function reload(quiet = false) {
  try {
    show(await api.get('/pools'));
  } catch {
    if (!quiet) { clear(grid).append(errorState(t('buyer.pools_error'), () => reload())); }
  }
}

const refresh = debounce(() => reload(true), 150);
const { main, note } = await startPage({
  role: 'buyer', active: 'pools', titleKey: 'pages.buyer_pools', onResync: () => reload(true),
  liveHandlers: { stock_changed: refresh, price_changed: refresh, status_changed: refresh },
});

filterSelect = h('select', { class: 'select', id: 'commodity-filter', 'aria-label': t('buyer.commodity') });
filterSelect.addEventListener('change', () => { commodityFilter = filterSelect.value; paint(); });
marketSelect = h('select', { class: 'select', id: 'market-filter', 'aria-label': t('buyer.market') });
marketSelect.addEventListener('change', () => { marketFilter = marketSelect.value; paint(); });
const marketWrap = h('div', { class: 'select-wrap tool-select', hidden: true }, marketSelect, icon('chevron-down'));
grid = h('div', { class: 'pool-grid', id: 'pool-grid' }, skeleton(), skeleton(), skeleton());

append(main, [
  h('div', { class: 'page-head' },
    h('div', { class: 'stack stack--tight' }, h('h1', {}, t('buyer.pools_title'))),
    h('div', { class: 'page-head__tools' }, h('div', { class: 'select-wrap tool-select' }, filterSelect, icon('chevron-down')), marketWrap, h('div', { class: 'row' }, h('span', { class: 'muted' }, t('unit.show_in')), unitToggle()))),
  grid, h('div', { class: 'row row--between' }, note, h('span', {})),
]);
onUnitChange(() => paint());
await reload();
document.documentElement.dataset.ready = 'true';
