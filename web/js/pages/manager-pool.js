// Pool Manager: one pool. Live stock, price and rules, records check, photos, farmers, and the three ledgers (rows appear at the
// top by themselves; price and status changes are shown as different rows, they are not ledger rows).
import { startPage } from '../layout.js';
import { t } from '../i18n.js';
import { api, ApiError } from '../api.js';
import { h, icon, clear, setValue, debounce, append } from '../dom.js';
import { skeleton, errorState, emptyState, unitToggle, onUnitChange, poolStatusTag, recordsTag, tag, field, button, busy, isBusy, openModal, breadcrumb, poolCode } from '../components.js';
import { formatQuantity, formatPaise, formatPricePerQuintal, formatPricePerQuintalWhole, formatPricePerKg, formatPermille, formatDateTime, parsePricePerQuintal, padColumn, getUnit, toBigInt } from '../format.js';
import { managerError, integritySentence, photoProblem, statusWord } from '../manager.js';

const id = new URLSearchParams(location.search).get('id');
let pool = null;
let refs = null;
const unitName = () => t(`unit.${getUnit()}`);
const qty = (g) => `${formatQuantity(g, getUnit())} ${unitName()}`;
const signedQty = (g) => { const n = toBigInt(g); return n === 0n ? formatQuantity(0n, getUnit()) : `${n < 0n ? '\u2212' : '+'}${formatQuantity(n < 0n ? -n : n, getUnit())}`; };   // a zero change has no sign

// ledgers: rows by id, newest first; cursor for "show older"
const ledgers = { inventory: { rows: new Map(), cursor: null }, ownership: { rows: new Map(), cursor: null }, financial: { rows: new Map(), cursor: null } };
const changes = new Map();
const freshIds = new Set();

const refresh = debounce(() => load(true), 150);
const body = h('div', { class: 'stack stack--loose' });
const { main, note } = await startPage({
  role: 'manager', active: 'pools', titleKey: 'pages.manager_pool', onResync: async () => { await load(true); await loadLedgers(true); },
  liveHandlers: {
    stock_changed: (m) => { if (String(m.pool_id) === id) refresh(); },
    price_changed: (m) => { if (String(m.pool_id) === id) refresh(); },
    status_changed: (m) => { if (String(m.pool_id) === id) refresh(); },
    reconciliation: (m) => { if (String(m.pool_id) === id) refresh(); },
    ledger_rows: (m) => { if (String(m.pool_id) === id) onLedgerRows(m); },
  },
});
append(main, [body]);
body.append(skeleton(), skeleton());

// ---- the page ---------------------------------------------------------------------------------------------------------
function build() {
  const p = pool;
  const title = h('h1', {});
  const tagSlot = h('span', {});
  const meta = h('p', { class: 'muted' });

  // live stock
  const stock = h('span', { class: 'key-number key-number--xl' });
  const stockUnit = h('span', { class: 'key-unit' });
  const fill = h('div', { class: 'meter__fill' });
  const stockSub = h('p', { class: 'small muted' });
  const stockCard = h('section', { class: 'card stack' }, h('p', { class: 'label' }, t('manager.live_stock')),
    h('p', {}, stock, ' ', stockUnit), h('div', { class: 'meter', role: 'img', 'aria-label': t('manager.live_stock') }, fill), stockSub);

  // price and rules
  const price = h('span', { class: 'key-number' });
  const priceLines = h('p', { class: 'small muted' });
  const minOrder = h('p', {});
  const closeBtn = button('', { onclick: () => confirmStatus() });
  const priceCard = h('section', { class: 'card stack' }, h('p', { class: 'label' }, t('manager.price_rules')),
    h('p', {}, price, ' ', h('span', { class: 'key-unit' }, t('unit.per_quintal'))), priceLines, minOrder,
    h('div', { class: 'row' }, button(t('manager.change_price'), { onclick: changePrice }), closeBtn));

  // records check
  const recTag = h('div', {});
  const recText = h('p', { class: 'small muted' });
  const verify = button(t('manager.verify'), { onclick: () => runVerify(verify) });
  const verifyResult = h('div', { class: 'stack stack--tight', role: 'status' });
  const recCard = h('section', { class: 'card stack' }, h('p', { class: 'label' }, t('manager.records_check')), recTag, recText, h('div', {}, verify), verifyResult);

  // photos
  const slots = h('div', { class: 'photo-row' });
  const photoProblemLine = h('p', { class: 'notice notice--bad', hidden: true, role: 'alert' }, icon('cross'), h('span', {}));
  const fileInput = h('input', { type: 'file', accept: 'image/*', capture: 'environment', class: 'visually-hidden', id: 'photo-input', 'aria-label': t('common.add_photo') });
  fileInput.addEventListener('change', () => uploadPhoto(fileInput));
  const photosCard = h('section', { class: 'card stack' }, h('p', { class: 'label' }, t('manager.photos_title')), slots, photoProblemLine);

  // farmers
  const farmerBody = h('tbody', {});
  const farmerHead = h('tr', {});
  const farmersCard = h('section', { class: 'card stack' },
    h('div', { class: 'row row--between' }, h('p', { class: 'label' }, t('manager.farmers_title')),
      h('div', { class: 'row' }, h('a', { class: 'btn btn--primary', href: `/manager/accounts.html?pool=${p.pool_id}&form=deposit` }, t('manager.record_deposit')),
        h('a', { class: 'btn btn--secondary', href: `/manager/accounts.html?pool=${p.pool_id}&form=sale` }, t('manager.place_sale')))),
    h('table', { class: 'table table--stack' }, h('thead', {}, farmerHead), farmerBody));

  // ledgers
  const ledgerCard = (key, labelKey, cols) => {
    const tbody = h('tbody', {});
    const more = h('button', { class: 'btn btn--secondary', type: 'button', hidden: true, onclick: () => loadMore(key) }, t('manager.show_older'));
    return { key, tbody, more, head: h('tr', {}), cols, root: h('section', { class: 'card card--table ledger' }, h('p', { class: 'label ledger__label' }, t(labelKey)), h('table', { class: 'table' }, h('thead', {}, null), tbody), h('div', { class: 'ledger__more' }, more)) };
  };
  const L = { inventory: ledgerCard('inventory', 'manager.stock_ledger', ['manager.col_event', 'manager.col_change', 'manager.col_stock_after']),
    ownership: ledgerCard('ownership', 'manager.ownership_ledger', ['manager.col_farmer', 'manager.col_change', 'manager.col_balance']),
    financial: ledgerCard('financial', 'manager.money_ledger', ['manager.col_entry', 'manager.col_amount']) };
  for (const l of Object.values(L)) l.root.querySelector('thead').replaceChildren(h('tr', {}, l.cols.map((c, i) => h('th', { class: i ? 'num' : '' }, t(c)))));

  body.replaceChildren(
    h('div', { class: 'stack' },
      breadcrumb([{ label: t('nav.pools'), href: '/manager/pools.html' }, { label: poolCode(p.pool_id) }]),
      h('div', { class: 'page-head' }, title, h('div', { class: 'row' }, h('span', { class: 'muted' }, t('unit.show_in')), unitToggle())),
      h('div', { class: 'row' }, tagSlot, meta)),
    h('div', { class: 'grid cols-3' }, stockCard, priceCard, recCard),
    h('div', { class: 'grid cols-2 grid--unequal' }, photosCard, farmersCard),
    h('div', { class: 'stack' }, h('div', { class: 'page-head page-head--tight' }, h('h2', {}, t('manager.three_ledgers'))),
      h('div', { class: 'grid cols-3 ledgers' }, L.inventory.root, L.ownership.root, L.financial.root)),
    h('div', { class: 'row row--between' }, note, h('span', {})), fileInput);
  refs = { title, tagSlot, meta, stock, stockUnit, fill, stockSub, price, priceLines, minOrder, closeBtn, recTag, recText, verifyResult, slots, photoProblemLine, fileInput, farmerBody, farmerHead, L };
}

function update() {
  const p = pool; const r = refs;
  r.title.textContent = `${p.commodity} · ${p.variety} · ${t('pool.range', { n: p.range_no })}`;
  clear(r.tagSlot).append(poolStatusTag(p));
  r.meta.textContent = [p.storage_location ? t('pool.stored_at', { place: p.storage_location }) : '', p.packaging].filter(Boolean).join(' · ');
  setValue(r.stock, formatQuantity(p.stock_g, getUnit()));
  r.stockUnit.textContent = unitName();
  const dep = toBigInt(p.deposited_g), left = toBigInt(p.stock_g);
  const permille = dep === 0n ? 0n : (left * 1000n) / dep;
  r.fill.style.width = `${permille / 10n}.${permille % 10n}%`;   // exact integer maths for the bar, too
  r.stockSub.textContent = t('manager.stock_sub', { stock: formatQuantity(p.stock_g, getUnit()), deposited: qty(p.deposited_g), sold: formatQuantity(p.sold_g, getUnit()) });   // the unit once, as in the wireframe
  setValue(r.price, formatPricePerQuintal(p.price_paise_per_kg));
  const lines = [`= ${formatPricePerKg(p.price_paise_per_kg)} ${t('unit.per_kg')}`];
  if (p.reference.msp) lines.push(t('manager.msp_short', { label: p.reference.msp.label, price: formatPaise(p.reference.msp.price_paise_per_quintal) }));
  if (p.reference.mandi_modal) lines.push(t('manager.mandi_short', { price: formatPaise(p.reference.mandi_modal.price_paise_per_quintal), label: p.reference.mandi_modal.label }));
  r.priceLines.replaceChildren(...lines.flatMap((l, i) => (i ? [h('br'), l] : [l])));
  r.minOrder.replaceChildren(`${t('manager.min_order')}: `, h('strong', {}, qty(p.min_order_g)));
  const isClosed = p.status === 'closed';
  r.closeBtn.textContent = t(isClosed ? 'manager.reopen_pool' : 'manager.close_pool');
  r.closeBtn.hidden = p.status === 'draft';
  clear(r.recTag).append(recordsTag(p.records_ok ? 'ok' : 'mismatch'));
  r.recText.textContent = p.records_ok
    ? (p.last_sale_at ? t('manager.records_ok_sale', { when: formatDateTime(p.last_sale_at) }) : t('manager.records_ok'))
    : t('manager.records_bad');
  paintPhotos(); paintFarmers(); paintLedgers();
}

function paintPhotos() {
  const r = refs; const photos = pool.photos;   // newest first
  const shown = [...photos].slice(0, 3);
  const items = shown.map((ph, i) => h('div', { class: 'photo' }, h('img', { src: ph.url, alt: t('photo.alt'), loading: 'lazy' }), h('span', { class: 'photo__caption' }, i === 0 && photos.length > 3 ? t('manager.photos_newest') : formatDateTime(ph.uploaded_at))));
  const add = h('label', { class: 'photo photo--add', for: 'photo-input', tabindex: '0', role: 'button', onkeydown: (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); r.fileInput.click(); } } }, icon('camera', 'icon--lg'), h('span', {}, t('common.add_photo')));
  clear(r.slots);
  append(r.slots, items); r.slots.append(add);
}

function paintFarmers() {
  const r = refs;
  const unit = getUnit();
  r.farmerHead.replaceChildren(h('th', {}, t('table.farmer')), h('th', { class: 'num' }, t('table.share')), h('th', { class: 'num' }, t(`table.left_${unit}`)));
  const L = { f: t('table.farmer'), s: t('table.share'), l: t(`table.left_${unit}`) };
  const leftCol = padColumn(pool.farmers.map((f) => formatQuantity(f.remaining_g, unit)));
  clear(r.farmerBody);
  if (!pool.farmers.length) { r.farmerBody.append(h('tr', {}, h('td', { colspan: '3', class: 'muted' }, t('manager.no_farmers')))); return; }
  append(r.farmerBody, pool.farmers.map((f, i) => h('tr', {}, h('td', { 'data-label': L.f }, h('a', { href: `/manager/statement.html?farmer=${f.seller_id}&pool=${pool.pool_id}` }, f.name)),
    h('td', { class: 'num', 'data-label': L.s }, formatPermille(f.share_permille)), h('td', { class: 'num', 'data-label': L.l }, leftCol[i]))));
}

// ---- ledgers ---------------------------------------------------------------------------------------------------------
const MOVEMENT = { OPEN: 'manager.mv_open', IN: 'manager.mv_in', OUT: 'manager.mv_out' };

function changeText(c) {
  if (c.field === 'PRICE') return t('manager.price_changed_row', { old: formatPricePerQuintalWhole(c.old_value), new: formatPricePerQuintalWhole(c.new_value) });
  return t('manager.status_changed_row', { old: statusWord(c.old_value), new: statusWord(c.new_value) });
}

function paintLedgers() {
  const r = refs;
  const mark = (tr, key) => { if (freshIds.has(key)) tr.classList.add('is-new'); return tr; };
  // newest event first; the rows of one event read in the order they were written (sale total, then each payout)
  const sorted = (map) => [...map.values()].sort((a, b) => b.event_id - a.event_id || a.id - b.id);

  const order = (x) => (x.kind === 'change' ? 0 : 1);
  const combined = [...[...ledgers.inventory.rows.values()].map((x) => ({ ...x, kind: 'row' })), ...[...changes.values()].map((x) => ({ ...x, kind: 'change' }))]
    .sort((a, b) => b.event_id - a.event_id || order(a) - order(b) || a.id - b.id);
  const invRows = combined.filter((x) => x.kind === 'row');
  const invChange = padColumn(invRows.map((x) => signedQty(x.delta_g)));   // each column of quantities shares one number of decimals
  const invAfter = padColumn(invRows.map((x) => formatQuantity(x.resulting_stock_g, getUnit())));
  let n = 0;
  clear(r.L.inventory.tbody);
  append(r.L.inventory.tbody, combined.map((x) => {
    if (x.kind === 'change') return h('tr', { class: 'ledger__change' }, h('td', { colspan: '3' }, icon('clock'), ' ', changeText(x)));
    const i = n; n += 1;
    return mark(h('tr', {}, h('td', {}, t(MOVEMENT[x.movement_type] || 'event.record')), h('td', { class: 'num' }, invChange[i]), h('td', { class: 'num' }, invAfter[i])), `inventory:${x.id}`);
  }));
  const ownRows = sorted(ledgers.ownership.rows);
  const ownChange = padColumn(ownRows.map((x) => signedQty(x.delta_g)));
  const ownBalance = padColumn(ownRows.map((x) => formatQuantity(x.resulting_balance_g, getUnit())));
  clear(r.L.ownership.tbody);
  append(r.L.ownership.tbody, ownRows.map((x, i) => mark(h('tr', {}, h('td', {}, x.seller_name), h('td', { class: 'num' }, ownChange[i]), h('td', { class: 'num' }, ownBalance[i])), `ownership:${x.id}`)));
  clear(r.L.financial.tbody);
  append(r.L.financial.tbody, sorted(ledgers.financial.rows).map((x) => mark(h('tr', {}, h('td', {}, x.entry_type === 'SALE_GROSS' ? t('manager.sale_total') : t('manager.paid_to', { name: x.seller_name })), h('td', { class: 'num' }, formatPaise(x.amount_paise))), `financial:${x.id}`)));
  for (const key of Object.keys(ledgers)) r.L[key].more.hidden = ledgers[key].cursor === null;
}

function addRows(key, rows) {
  for (const row of rows) {
    if (row.kind === 'change') changes.set(row.id, row); else ledgers[key].rows.set(row.id, row);
  }
}

function onLedgerRows(m) {
  freshIds.clear();
  const fresh = m.rows.filter((r) => r.kind !== 'change' && !ledgers[m.ledger].rows.has(r.id));
  for (const r of fresh) freshIds.add(`${m.ledger}:${r.id}`);
  addRows(m.ledger, m.rows);
  if (refs) paintLedgers();
}

async function loadLedgers(quiet = false) {
  for (const key of Object.keys(ledgers)) {
    try {
      const page = await api.get(`/pools/${id}/ledgers/${key}?limit=30`);
      ledgers[key].rows.clear();
      addRows(key, page.rows);
      if (key === 'inventory') { changes.clear(); addRows('inventory', page.changes || []); }
      ledgers[key].cursor = page.next_before_id;
    } catch { if (!quiet) throw new Error('ledger'); }
  }
  freshIds.clear();
  if (refs) paintLedgers();
}

async function loadMore(key) {
  const l = ledgers[key];
  if (l.cursor === null) return;
  try { const page = await api.get(`/pools/${id}/ledgers/${key}?limit=30&before=${l.cursor}`); addRows(key, page.rows); l.cursor = page.next_before_id; freshIds.clear(); paintLedgers(); } catch { /* the rows already shown stay */ }
}

// ---- actions ---------------------------------------------------------------------------------------------------------
function changePrice() {
  openModal((close, replace) => {
    const input = h('input', { type: 'text', inputmode: 'numeric', autocomplete: 'off', value: String(pool.price_paise_per_kg) });
    const f = field({ id: 'new-price', label: t('manager.new_price'), input, hint: t('manager.price_hint') });
    const save = button(t('manager.save_price'), { kind: 'primary', onclick: async () => {
      if (isBusy(save)) return;
      const parsed = parsePricePerQuintal(input.value);
      if (!parsed.ok) { f.error(t(`input.price_${parsed.reason}`)); input.focus(); return; }
      try { await busy(save, () => api.patch(`/pools/${id}`, { price_paise_per_kg: Number(parsed.paisePerKg) })); close(); await load(true); }
      catch (err) { f.error(managerError(err)); }
    } });
    input.addEventListener('input', () => { f.error(null); const r = parsePricePerQuintal(input.value); f.hintEl.textContent = r.ok ? `${t('manager.price_hint')} = ${formatPricePerKg(r.paisePerKg)} ${t('unit.per_kg')}` : t('manager.price_hint'); });
    return [h('h2', {}, t('manager.change_price')), h('p', { class: 'muted' }, t('manager.change_price_note')), f.root, h('div', { class: 'row' }, button(t('common.cancel'), { onclick: close }), save)];
  });
}

function confirmStatus() {
  const closing = pool.status !== 'closed';
  openModal((close) => {
    const go = button(closing ? t('manager.close_pool') : t('manager.reopen_pool'), { kind: 'primary', onclick: async () => {
      if (isBusy(go)) return;
      try { await busy(go, () => api.patch(`/pools/${id}`, { status: closing ? 'closed' : 'open' })); close(); await load(true); }
      catch (err) { problem.hidden = false; problem.lastElementChild.textContent = managerError(err); }
    } });
    const problem = h('p', { class: 'notice notice--bad', hidden: true, role: 'alert' }, icon('cross'), h('span', {}));
    return [h('h2', {}, closing ? t('manager.close_title') : t('manager.reopen_title')), h('p', { class: 'muted' }, closing ? t('manager.close_body') : t('manager.reopen_body')), problem, h('div', { class: 'row' }, button(t('common.cancel'), { onclick: close }), go)];
  });
}

async function runVerify(btn) {
  if (isBusy(btn)) return;
  const out = refs.verifyResult;
  try {
    const r = await busy(btn, () => api.post(`/pools/${id}/integrity-check`));
    clear(out).append(r.ok ? tag('ok', t('manager.integrity_ok', { n: r.checked_entries }), 'check') : h('p', { class: 'notice notice--bad' }, icon('cross'), h('span', {}, integritySentence(r.first_broken))));
  } catch (err) {
    clear(out).append(h('p', { class: 'notice notice--bad' }, icon('cross'), h('span', {}, managerError(err))));
  }
}

async function uploadPhoto(input) {
  const file = input.files[0];
  const line = refs.photoProblemLine;
  line.hidden = true;
  if (!file) return;
  const problemText = photoProblem(file);
  if (problemText) { line.hidden = false; line.lastElementChild.textContent = problemText; input.value = ''; return; }
  const addSlot = refs.slots.querySelector('.photo--add');
  addSlot.classList.add('is-loading');
  addSlot.replaceChildren(icon('clock', 'icon--lg'), h('span', {}, t('common.working')));
  try { await api.upload(`/pools/${id}/photos`, file); await load(true); }
  catch (err) { line.hidden = false; line.lastElementChild.textContent = managerError(err); paintPhotos(); }
  input.value = '';
}

// ---- loading ---------------------------------------------------------------------------------------------------------
async function load(quiet = false) {
  try {
    pool = await api.get(`/pools/${id}`);
    if (!refs) build();
    update();
  } catch (err) {
    if (!(err instanceof ApiError)) console.error('pool screen failed', err);
    if (quiet && refs) return;
    clear(body);
    if (err instanceof ApiError && err.status === 404) body.append(emptyState(t('error.not_found'), h('a', { class: 'btn btn--secondary', href: '/manager/pools.html' }, t('nav.pools'))));
    else body.append(errorState(t('buyer.pool_error'), () => { refs = null; clear(body).append(skeleton(), skeleton()); start(); }));
  }
}

async function start() {
  await load();
  if (refs) { try { await loadLedgers(); } catch { /* ledgers show empty; the live feed fills them */ } }
}
onUnitChange(() => { if (refs) update(); });
await start();
document.documentElement.dataset.ready = 'true';
