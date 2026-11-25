// Buyer pool screen: photos, about this lot, quality limits, and the price-and-buy box (the one 2 px box). Live updates in place.
import { startPage } from '../layout.js';
import { marketCard } from '../market.js';
import { t } from '../i18n.js';
import { api, ApiError } from '../api.js';
import { h, clear, setValue, debounce, append } from '../dom.js';
import { skeleton, errorState, emptyState, unitToggle, onUnitChange, poolStatusTag, field, button, poolCode, breadcrumb } from '../components.js';
import { formatQuantity, formatQuantityInput, formatPricePerQuintal, formatPricePerQuintalWhole, formatPricePerKg, formatPaise, formatDate, formatDateTime, parseQuantity, totalPaise, getUnit, toBigInt } from '../format.js';
import { startPurchase } from '../sale-flow.js';

const id = new URLSearchParams(location.search).get('id');
let pool = null;
let grams = null;       // the quantity typed, in exact grams (null while it cannot be read)
let touched = false;
let refs = null;
const unitName = () => t(`unit.${getUnit()}`);
const qty = (g) => `${formatQuantity(g, getUnit())} ${unitName()}`;

const refresh = debounce(() => load(true), 150);
const { main, note } = await startPage({
  role: 'buyer', active: 'pools', titleKey: 'pages.buyer_pool', onResync: () => load(true),
  liveHandlers: {
    stock_changed: (m) => { if (String(m.pool_id) === id) refresh(); },
    price_changed: (m) => { if (String(m.pool_id) === id) refresh(); },
    status_changed: (m) => { if (String(m.pool_id) === id) refresh(); },
  },
});
const body = h('div', { class: 'stack stack--loose' });
append(main, [body]);
body.append(skeleton(), skeleton());

function quantityMessage(reason) {
  return reason === 'too_precise' ? t(`input.quantity_too_precise_${getUnit()}`) : t(`input.quantity_${reason}`);
}

// Reads the quantity field. Returns true when the order can be reviewed.
function readQuantity() {
  const r = parseQuantity(refs.input.value, getUnit());
  if (!r.ok) { grams = null; refs.field.error(quantityMessage(r.reason)); setValue(refs.total, t('common.none')); return false; }
  grams = r.grams;
  setValue(refs.total, formatPaise(totalPaise(grams, pool.price_paise_per_kg)));
  const stock = toBigInt(pool.stock_g), min = toBigInt(pool.min_order_g);
  if (grams > stock) { refs.field.error(t('reason.INSUFFICIENT_STOCK.title', { stock: qty(stock) })); return false; }
  if (grams < min && grams !== stock) { refs.field.error(t('reason.BELOW_MIN_ORDER.title', { min: qty(min) })); return false; }
  refs.field.error(null);
  return true;
}

function build() {
  const p = pool;
  const photos = p.photos;
  const gallery = h('div', { class: 'gallery' }, [0, 1, 2].map((i) => photos[i]
    ? h('div', { class: `photo${i === 0 ? ' gallery__main' : ''}` }, h('img', { src: photos[i].url, alt: t('photo.alt') }))
    : h('div', { class: `photo photo--empty${i === 0 ? ' gallery__main' : ''}` }, h('span', {}, t('photo.slot')))));
  const marketBox = marketCard();
  marketBox.show(p.commodity_id, p.variety);
  const history = h('section', { class: 'card stack', hidden: true }, h('p', { class: 'label' }, t('pool.price_history')), h('ul', { class: 'plain-list' }));
  const facts = h('dl', { class: 'facts' });
  const addFact = (label, value) => append(facts, [h('dt', {}, label), h('dd', {}, value)]);
  addFact(t('pool.commodity_variety'), `${p.commodity} · ${p.variety}`);
  if (p.packaging) addFact(t('pool.packaging'), p.packaging);
  if (p.market) addFact(t('buyer.market'), p.market);
  addFact(t('pool.stored'), p.storage_location);
  addFact(t('pool.contributed_by'), t(p.farmer_count === 1 ? 'pool.farmers_private_one' : 'pool.farmers_private', { count: p.farmer_count }));
  const lastSale = h('dd', {}, p.last_sale_at ? formatDateTime(p.last_sale_at) : t('common.none'));
  append(facts, [h('dt', {}, t('pool.last_sale_label')), lastSale]);

  const limits = p.limits.length
    ? h('div', {}, h('table', { class: 'table' }, h('thead', {}, h('tr', {}, h('th', {}, t('pool.check')), h('th', { class: 'num' }, t('pool.at_most')))),
        h('tbody', {}, p.limits.map((l) => h('tr', {}, h('td', {}, l.name), h('td', { class: 'num' }, `${trimZeros(l.max)}${l.unit}`))))),
        p.limits_verified ? null : h('p', { class: 'small muted limits-note' }, t('pool.limits_unverified')))
    : h('p', { class: 'muted' }, t('pool.limits_missing'));

  // price and buy box
  const price = h('span', { class: 'key-number' });
  const priceKg = h('p', { class: 'small muted' });
  const avail = h('span', { class: 'key-number' });
  const availUnit = h('span', { class: 'key-unit' });
  const compare = h('p', { class: 'small muted' });
  const input = h('input', { type: 'text', inputmode: 'decimal', autocomplete: 'off', spellcheck: 'false' });
  const f = field({ id: 'quantity', label: ' ', input, hint: ' ' });   // both are filled in by update()
  const total = h('strong', { class: 'key-number total-box__value' });
  const review = button(t('buyer.review_order'), { kind: 'primary', block: true, onclick: () => {
    touched = true;
    if (!readQuantity()) { input.focus(); return; }
    startPurchase({ pool, grams, onFinished: () => load(true), onViewPurchases: () => location.assign('/buyer/purchases.html') });
  } });
  input.addEventListener('input', () => { touched = true; readQuantity(); });
  const outNote = h('p', { class: 'notice notice--grey', hidden: true }, h('span', {}, t('buyer.out_of_stock_note')));
  const buyBox = h('section', { class: 'card card--key stack' },
    h('div', { class: 'row row--between row--top' },
      h('div', {}, h('p', { class: 'label' }, t('buyer.price')), h('p', {}, price, ' ', h('span', { class: 'key-unit' }, t('unit.per_quintal'))), priceKg),
      h('div', { class: 'fig--right' }, h('p', { class: 'label' }, t('buyer.available_now')), h('p', {}, avail, ' ', availUnit))),
    compare, f.root, h('div', { class: 'total-box' }, h('p', { class: 'label' }, t('sale.total')), total), outNote, review,
    h('p', { class: 'small muted' }, t('buyer.nothing_bought')));

  const title = h('h1', {});
  const tagSlot = h('span', {});
  const meta = h('p', { class: 'muted' });
  body.replaceChildren(
    h('div', { class: 'stack' },
      breadcrumb([{ label: t('nav.pools'), href: '/buyer/pools.html' }, { label: poolCode(p.pool_id) }]),
      h('div', { class: 'page-head' }, title, h('div', { class: 'row' }, h('span', { class: 'muted' }, t('unit.show_in')), unitToggle())),
      h('div', { class: 'row' }, tagSlot, meta)),
    h('div', { class: 'detail-grid' },
      h('div', { class: 'stack stack--loose' }, gallery,
        h('section', { class: 'card stack' }, h('p', { class: 'label' }, t('pool.about')), facts),
        h('section', { class: 'card stack' }, h('p', { class: 'label' }, t('pool.quality', { n: p.range_no })), limits), history),
      h('div', { class: 'stack stack--loose' }, buyBox)),
    marketBox.root,
    h('div', { class: 'row row--between' }, note, h('span', {})));
  refs = { history, title, tagSlot, meta, price, priceKg, avail, availUnit, compare, input, field: f, total, review, outNote, lastSale };
}

// Fills (or refreshes) every number and label in place. Called on load, on live updates and when the unit changes.
function update(unitChanged = false) {
  const p = pool;
  const r = refs;
  r.title.textContent = `${p.commodity} · ${p.variety}`;
  clear(r.tagSlot).append(poolStatusTag(p));
  const dates = p.first_deposit_at ? t('pool.meta_dates', { first: formatDate(p.first_deposit_at), last: formatDate(p.last_deposit_at) }) : '';
  r.meta.textContent = t('pool.meta', { code: poolCode(p.pool_id), range: p.range_no, dates });
  setValue(r.price, formatPricePerQuintal(p.price_paise_per_kg));
  r.priceKg.textContent = `= ${formatPricePerKg(p.price_paise_per_kg)} ${t('unit.per_kg')}`;
  setValue(r.avail, formatQuantity(p.stock_g, getUnit()));
  r.availUnit.textContent = unitName();
  r.history.hidden = !(p.price_history || []).length;
  clear(r.history.lastElementChild).append(...(p.price_history || []).map((c) => h('li', {}, h('span', { class: 'muted nowrap' }, formatDateTime(c.at)), ' ', t('pool.price_change_row', { old: formatPricePerQuintalWhole(c.old_value), new: formatPricePerQuintalWhole(c.new_value) }))));
  const ref = [];
  if (p.reference.msp) ref.push(t('buyer.msp_line', { label: p.reference.msp.label, price: formatPaise(p.reference.msp.price_paise_per_quintal) }));
  if (p.reference.mandi_modal) ref.push(t('buyer.mandi_line', { price: formatPaise(p.reference.mandi_modal.price_paise_per_quintal), label: p.reference.mandi_modal.label, date: formatDate(p.reference.mandi_modal.as_of) }));
  r.compare.textContent = ref.join(' ');
  r.field.hintEl.textContent = t('buyer.quantity_hint', { min: qty(p.min_order_g), max: qty(p.stock_g) });
  r.field.root.querySelector('label').textContent = t(`buyer.quantity_label_${getUnit()}`);
  const out = BigInt(p.stock_g) === 0n;
  r.input.disabled = out;
  r.review.disabled = out;
  r.outNote.hidden = !out;
  if (unitChanged || !touched) {
    if (grams === null && !touched) grams = toBigInt(p.min_order_g) <= toBigInt(p.stock_g) ? toBigInt(p.min_order_g) : toBigInt(p.stock_g);
    if (grams !== null) r.input.value = formatQuantityInput(grams, getUnit());
  }
  if (touched) readQuantity(); else if (grams !== null) setValue(r.total, formatPaise(totalPaise(grams, p.price_paise_per_kg)));
}

function trimZeros(text) {
  return text.includes('.') ? text.replace(/0+$/, '').replace(/\.$/, '') : text;
}

async function load(quiet = false) {
  try {
    pool = await api.get(`/pools/${id}`);
    if (!refs) build();
    update();
  } catch (err) {
    if (!(err instanceof ApiError)) console.error('pool screen failed', err);   // a bug, not a server answer: keep it visible
    if (quiet && refs) return;
    clear(body);
    if (err instanceof ApiError && err.status === 404) body.append(emptyState(t('reason.POOL_NOT_OPEN.title'), h('a', { class: 'btn btn--secondary', href: '/buyer/pools.html' }, t('sale.back_to_pools'))));
    else body.append(errorState(t('buyer.pool_error'), () => { refs = null; clear(body).append(skeleton(), skeleton()); load(); }));
  }
}

onUnitChange(() => { if (refs) update(true); });
await load();
document.documentElement.dataset.ready = 'true';
