// Pool Manager: record a deposit, place a sale for an in-person buyer. (People and accounts live on the People page.)
import { startPage } from '../layout.js';
import { t } from '../i18n.js';
import { api } from '../api.js';
import { h, icon, clear, append } from '../dom.js';
import { field, button, busy, isBusy, tag, unitToggle, onUnitChange, skeleton, errorState } from '../components.js';
import { formatQuantity, formatPaise, formatPricePerQuintal, formatPermille, formatQuantityInput, parseQuantity, totalPaise, getUnit, toBigInt } from '../format.js';
import { managerError, poolLabel } from '../manager.js';
import { startPurchase } from '../sale-flow.js';

const params = new URLSearchParams(location.search);
let pools = [], users = [];
const detail = new Map();     // pool_id -> pool detail (for balances and prices)
const body = h('div', { class: 'stack stack--loose' });
const { main, note } = await startPage({ role: 'manager', active: 'accounts', titleKey: 'pages.manager_accounts', onResync: () => loadAll(true), liveHandlers: { stock_changed: () => refreshDetails(), status_changed: () => loadAll(true) } });
append(main, [body, h('div', { class: 'row row--between' }, note, h('span', {}))]);
body.append(skeleton(), skeleton());

const qty = (g) => `${formatQuantity(g, getUnit())} ${t(`unit.${getUnit()}`)}`;
const verified = (u) => u.status === 'active' || u.status === 'waiting_first_login';   // pending and deactivated people cannot deposit or buy
const farmers = () => users.filter((u) => u.type === 'FARMER' && verified(u));
const buyers = () => users.filter((u) => u.type === 'BUYER' && verified(u));
const options = (list, label) => list.map((x) => h('option', { value: x.pool_id ?? x.user_id }, label(x)));
const notice = () => h('p', { class: 'notice notice--grey', hidden: true, role: 'status' }, icon('check'), h('span', {}));
// A form's last message survives the page being redrawn (a refresh after saving must not wipe the "saved" message).
const kept = new Map();
const current = new Map();   // key -> the notice element now on the page (a redraw can replace the one a request started with)
const showNotice = (el, text, kind = 'grey', key = null) => {
  if (key) { kept.set(key, text ? { text, kind } : null); el = current.get(key) || el; }
  el.hidden = !text; el.className = `notice notice--${kind}`; el.lastElementChild.textContent = text || ''; el.firstElementChild.replaceWith(icon(kind === 'bad' ? 'cross' : 'check'));
};
const restore = (el, key) => { current.set(key, el); const k = kept.get(key); if (k) showNotice(el, k.text, k.kind); };

async function poolDetail(id) {
  if (!detail.has(id)) detail.set(id, await api.get(`/pools/${id}`));
  return detail.get(id);
}
async function refreshDetails() { detail.clear(); for (const fn of refreshers) await fn(); }
const refreshers = [];

// ---- deposit ---------------------------------------------------------------------------------------------------------
function depositCard() {
  const usable = pools.filter((p) => p.status !== 'closed');
  const pool = field({ id: 'dep-pool', label: t('manager.pool'), input: h('select', {}, options(usable, poolLabel)) });
  const farmer = field({ id: 'dep-farmer', label: t('manager.farmer'), input: h('select', {}, options(farmers(), (u) => u.name)) });
  const quantity = field({ id: 'dep-qty', label: '', input: h('input', { type: 'text', inputmode: 'decimal', autocomplete: 'off' }) });
  const tested = h('input', { type: 'checkbox', class: 'check__box', id: 'dep-tested' });
  const testedLabel = h('label', { class: 'check', for: 'dep-tested' }, tested, h('span', {}));
  const preview = h('div', { class: 'total-box' }, h('p', { class: 'label' }, t('manager.after_saving')), h('p', { id: 'dep-stock' }), h('p', { id: 'dep-farmer-line' }));
  const problem = notice();
  restore(problem, 'deposit');
  const save = button(t('manager.save_deposit'), { kind: 'primary', block: true, onclick: submit });
  const initial = params.get('pool') && usable.some((p) => String(p.pool_id) === params.get('pool')) ? params.get('pool') : '';
  if (initial) pool.input.value = initial;

  async function paint() {
    if (!usable.length) { preview.hidden = true; return; }
    const p = usable.find((x) => String(x.pool_id) === pool.input.value) || usable[0];
    testedLabel.lastElementChild.textContent = t('manager.tested', { n: p.range_no });
    quantity.root.querySelector('label').textContent = t(`manager.quantity_${getUnit()}`);
    const r = parseQuantity(quantity.input.value, getUnit());
    const d = await poolDetail(p.pool_id).catch(() => null);
    preview.hidden = !(r.ok && d);
    if (!(r.ok && d)) return;
    const f = d.farmers.find((x) => String(x.seller_id) === farmer.input.value);
    const stockNow = toBigInt(d.stock_g), mineNow = f ? toBigInt(f.remaining_g) : 0n;
    const stockAfter = stockNow + r.grams, mineAfter = mineNow + r.grams;
    const permille = (mineAfter * 2000n + stockAfter) / (2n * stockAfter);
    const name = farmers().find((u) => String(u.user_id) === farmer.input.value)?.name || '';
    const arrow = () => icon('arrow-right');
    clear(preview.querySelector('#dep-stock')).append(`${t('manager.pool_stock')}: ${formatQuantity(stockNow, getUnit())} `, arrow(), ' ', h('strong', {}, formatQuantity(stockAfter, getUnit())), ` ${t(`unit.${getUnit()}`)}`);
    clear(preview.querySelector('#dep-farmer-line')).append(`${name}: ${formatQuantity(mineNow, getUnit())} `, arrow(), ' ', h('strong', {}, formatQuantity(mineAfter, getUnit())), ` ${t(`unit.${getUnit()}`)} (${formatPermille(permille)})`);
  }

  async function submit() {
    if (isBusy(save)) return;
    quantity.error(null); showNotice(problem, '', 'grey', 'deposit');
    const r = parseQuantity(quantity.input.value, getUnit());
    if (!r.ok) { quantity.error(t(r.reason === 'too_precise' ? `input.quantity_too_precise_${getUnit()}` : `input.quantity_${r.reason}`)); quantity.input.focus(); return; }
    if (!tested.checked) { showNotice(problem, t('manager.tested_required'), 'bad', 'deposit'); tested.focus(); return; }
    try {
      await busy(save, () => api.post(`/pools/${pool.input.value}/contributions`, { seller_id: Number(farmer.input.value), quantity_g: Number(r.grams) }));
      const name = farmers().find((u) => String(u.user_id) === farmer.input.value)?.name;
      showNotice(problem, t('manager.deposit_saved', { qty: qty(r.grams), name }), 'grey', 'deposit');
      quantity.input.value = ''; tested.checked = false; detail.clear(); await paint();
    } catch (err) { showNotice(problem, managerError(err), 'bad', 'deposit'); }
  }
  for (const el of [pool.input, farmer.input, quantity.input]) el.addEventListener('input', paint);
  refreshers.push(paint);
  paint();
  return h('section', { class: 'card stack', id: 'form-deposit' }, h('p', { class: 'label' }, t('manager.intake')), h('h2', {}, t('manager.deposit_title')),
    usable.length && farmers().length ? [pool.root, farmer.root, quantity.root, testedLabel, preview, problem, save] : h('p', { class: 'muted' }, t('manager.deposit_none')));
}

// ---- sale for a buyer ---------------------------------------------------------------------------------------------------
function saleCard() {
  const selling = pools.filter((p) => p.status === 'open' && BigInt(p.stock_g) > 0n);
  const pool = field({ id: 'sale-pool', label: t('manager.pool'), input: h('select', {}, options(selling, poolLabel)) });
  const buyer = field({ id: 'sale-buyer', label: t('manager.buyer'), input: h('select', {}, options(buyers(), (u) => u.name)) });
  const quantity = field({ id: 'sale-qty', label: '', input: h('input', { type: 'text', inputmode: 'decimal', autocomplete: 'off' }), hint: ' ' });
  const review = h('div', { class: 'total-box' }, h('p', { class: 'label' }, t('manager.review')), h('p', { id: 'sale-line' }), h('p', { class: 'key-number total-box__value', id: 'sale-total' }));
  const initial = params.get('pool') && selling.some((p) => String(p.pool_id) === params.get('pool')) ? params.get('pool') : '';
  if (initial) pool.input.value = initial;
  let grams = null;

  async function paint() {
    if (!selling.length) return;
    const p = selling.find((x) => String(x.pool_id) === pool.input.value) || selling[0];
    quantity.root.querySelector('label').textContent = t(`manager.quantity_${getUnit()}`);
    const d = await poolDetail(p.pool_id).catch(() => null);
    if (!d) return;
    quantity.hintEl.textContent = t('manager.sale_hint', { stock: qty(d.stock_g), min: qty(d.min_order_g) });
    const r = parseQuantity(quantity.input.value, getUnit());
    grams = r.ok ? r.grams : null;
    review.hidden = !r.ok;
    if (r.ok) {
      document.getElementById('sale-line').textContent = t('manager.sale_line', { qty: qty(r.grams), price: formatPricePerQuintal(d.price_paise_per_kg) });
      document.getElementById('sale-total').textContent = formatPaise(totalPaise(r.grams, d.price_paise_per_kg));
    }
  }
  const go = button(t('manager.review_confirm'), { kind: 'primary', onclick: async () => {
    quantity.error(null);
    const p = selling.find((x) => String(x.pool_id) === pool.input.value) || selling[0];
    const d = await poolDetail(p.pool_id);
    const r = parseQuantity(quantity.input.value, getUnit());
    if (!r.ok) { quantity.error(t(r.reason === 'too_precise' ? `input.quantity_too_precise_${getUnit()}` : `input.quantity_${r.reason}`)); quantity.input.focus(); return; }
    if (r.grams > toBigInt(d.stock_g)) { quantity.error(t('reason.INSUFFICIENT_STOCK.title', { stock: qty(d.stock_g) })); return; }
    if (r.grams < toBigInt(d.min_order_g) && r.grams !== toBigInt(d.stock_g)) { quantity.error(t('reason.BELOW_MIN_ORDER.title', { min: qty(d.min_order_g) })); return; }
    const names = Object.fromEntries(d.farmers.map((f) => [f.seller_id, f.name]));
    startPurchase({ pool: d, grams: r.grams, buyer: { id: Number(buyer.input.value) }, farmerNames: names, onFinished: async () => { detail.clear(); await loadAll(true); } });
  } });
  for (const el of [pool.input, quantity.input]) el.addEventListener('input', paint);
  refreshers.push(paint);
  paint();
  return h('section', { class: 'card stack', id: 'form-sale' }, h('p', { class: 'label' }, t('manager.phone_buyers')), h('h2', {}, t('manager.sale_title')),
    selling.length && buyers().length ? [pool.root, buyer.root, quantity.root, review, h('div', { class: 'row' }, go)] : h('p', { class: 'muted' }, t('manager.sale_none')));
}

async function loadAll(quiet = false) {
  try {
    [pools, users] = await Promise.all([api.get('/pools'), api.get('/users')]);
    refreshers.length = 0; detail.clear();
    // a redraw (after saving, or on a live event) must never wipe what the Manager is typing in another form
    const keep = [...body.querySelectorAll('input[id], select[id]')].map((el) => [el.id, el.type === 'checkbox' ? el.checked : el.value]);
    clear(body).append(
      h('div', { class: 'page-head' }, h('div', { class: 'stack stack--tight' }, h('p', { class: 'label' }, t('manager.eyebrow')), h('h1', {}, t('manager.forms_title'))), h('div', { class: 'row' }, h('span', { class: 'muted' }, t('unit.show_in')), unitToggle())),
      h('div', { class: 'grid cols-2 grid--forms' }, depositCard(), saleCard()));
    for (const [elId, value] of keep) {
      const el = document.getElementById(elId);
      if (!el) continue;
      if (el.type === 'checkbox') el.checked = value; else if (value !== '' || el.tagName === 'INPUT') el.value = value;
    }
    for (const fn of refreshers) fn();
    const target = params.get('form') ? document.getElementById(`form-${params.get('form')}`) : null;
    if (target && !quiet) target.scrollIntoView({ block: 'start' });
  } catch {
    if (!quiet) clear(body).append(errorState(t('manager.accounts_error'), () => loadAll()));
  }
}
onUnitChange(() => { for (const fn of refreshers) fn(); });
await loadAll();
document.documentElement.dataset.ready = 'true';
