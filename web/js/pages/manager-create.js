// Pool Manager: create a pool. The draft is saved on the first photo or on "Save as draft"; "Open for sale" waits for a photo.
import { startPage } from '../layout.js';
import { t } from '../i18n.js';
import { api } from '../api.js';
import { h, icon, clear, append } from '../dom.js';
import { field, button, busy, isBusy, breadcrumb, skeleton, errorState } from '../components.js';
import { formatPricePerKg, formatPaise, parsePricePerQuintal, parseQuantity } from '../format.js';
import { managerError, photoProblem } from '../manager.js';

let commodities = [];
let poolId = null;       // set once the draft exists
let photoCount = 0;
let range = 1;
const body = h('div', { class: 'stack stack--loose' });
const { main } = await startPage({ role: 'manager', active: 'pools', titleKey: 'pages.manager_create' });
append(main, [body]);
body.append(skeleton());

function build() {
  const commodity = h('select', {}, commodities.map((c) => h('option', { value: c.commodity_id }, c.name)));
  const cField = field({ id: 'commodity', label: t('manager.commodity'), input: commodity });
  const variety = field({ id: 'variety', label: t('manager.variety'), input: h('input', { type: 'text', autocomplete: 'off', maxlength: '60' }) });
  const price = field({ id: 'price', label: t('manager.price_field'), input: h('input', { type: 'text', inputmode: 'numeric', autocomplete: 'off' }), hint: t('manager.price_hint') });
  const minOrder = field({ id: 'min-order', label: t('manager.min_field'), input: h('input', { type: 'text', inputmode: 'decimal', autocomplete: 'off', value: '1' }), hint: t('manager.min_hint') });
  const packaging = field({ id: 'packaging', label: t('manager.packaging'), input: h('input', { type: 'text', autocomplete: 'off', maxlength: '60' }) });
  const storage = field({ id: 'storage', label: t('manager.stored_at'), input: h('input', { type: 'text', autocomplete: 'off', maxlength: '120' }) });
  const rangeBtns = [1, 2, 3].map((n) => h('button', { class: 'toggle__opt', type: 'button', 'aria-pressed': String(n === range), onclick: () => { range = n; for (const [i, b] of rangeBtns.entries()) b.setAttribute('aria-pressed', String(i + 1 === n)); paintLimits(); } }, t('pool.range', { n })));
  const rangeBox = h('div', { class: 'field' }, h('span', { class: 'field__label' }, t('manager.range_label')), h('div', { class: 'row' }, h('div', { class: 'toggle', role: 'group', 'aria-label': t('manager.range_label') }, rangeBtns)));
  const fields = [commodity, variety.input, price.input, minOrder.input, packaging.input, storage.input];

  // photo slots
  const photoInput = h('input', { type: 'file', accept: 'image/*', capture: 'environment', class: 'visually-hidden', id: 'photo-pick', 'aria-label': t('manager.take_or_upload') });
  const slots = h('div', { class: 'photo-row photo-row--three' });
  const slotProblem = h('p', { class: 'notice notice--bad', hidden: true, role: 'alert' }, icon('cross'), h('span', {}));
  const paintSlots = (previews = []) => {
    clear(slots);
    for (let i = 0; i < 3; i += 1) {
      if (previews[i]) slots.append(h('div', { class: 'photo' }, h('img', { src: previews[i], alt: t('photo.alt') })));
      else if (i === previews.length && previews.length < 3) slots.append(h('label', { class: 'photo photo--add', for: 'photo-pick', tabindex: '0', role: 'button', onkeydown: (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); photoInput.click(); } } }, icon('camera', 'icon--lg'), h('span', {}, i === 0 ? t('manager.take_or_upload') : t('manager.add_another'))));
      else slots.append(h('div', { class: 'photo photo--empty' }, h('span', {}, t('photo.slot'))));
    }
  };
  const previews = [];
  paintSlots();

  const draft = button(t('manager.save_draft'), { onclick: () => saveDraft(false) });
  const open = button(t('manager.open_for_sale'), { kind: 'primary', disabled: true, onclick: () => saveDraft(true) });
  const draftNote = h('p', { class: 'notice notice--grey', hidden: true, role: 'status' }, icon('check'), h('span', {}, t('manager.draft_saved_note')));
  const formProblem = h('p', { class: 'notice notice--bad', hidden: true, role: 'alert' }, icon('cross'), h('span', {}));
  const showProblem = (text) => { formProblem.hidden = !text; formProblem.lastElementChild.textContent = text || ''; };

  function read() {
    let ok = true;
    for (const f of [variety, price, minOrder, packaging, storage]) f.error(null);
    if (!variety.input.value.trim()) { variety.error(t('manager.err_required')); ok = false; }
    const p = parsePricePerQuintal(price.input.value);
    if (!p.ok) { price.error(t(`input.price_${p.reason}`)); ok = false; }
    const m = parseQuantity(minOrder.input.value, 'quintal');
    if (!m.ok) { minOrder.error(t(m.reason === 'too_precise' ? 'input.quantity_too_precise_quintal' : `input.quantity_${m.reason}`)); ok = false; }
    if (!storage.input.value.trim()) { storage.error(t('manager.err_required')); ok = false; }
    return ok ? { commodity_id: Number(commodity.value), variety: variety.input.value.trim(), range_no: range, packaging: packaging.input.value.trim() || null, storage_location: storage.input.value.trim(), min_order_g: Number(m.grams), price_paise_per_kg: Number(p.paisePerKg) } : null;
  }

  async function ensureDraft() {
    if (poolId) return poolId;
    const payload = read();
    if (!payload) return null;
    const created = await api.post('/pools', payload);
    poolId = created.pool_id;
    for (const el of fields) el.disabled = true;      // a saved pool's commodity, variety, Range and rules are not edited here
    for (const b of rangeBtns) b.disabled = true;
    showProblem(null);
    draftNote.hidden = false;
    return poolId;
  }

  async function saveDraft(openAfter) {
    const btn = openAfter ? open : draft;
    if (isBusy(btn)) return;
    try {
      await busy(btn, async () => {
        const created = await ensureDraft();
        if (!created) throw new Error('invalid');
        if (openAfter) await api.patch(`/pools/${poolId}`, { status: 'open' });
      });
      location.assign(`/manager/pool.html?id=${poolId}`);
    } catch (err) {
      if (err.message !== 'invalid') showProblem(managerError(err));
    }
  }

  photoInput.addEventListener('change', async () => {
    const file = photoInput.files[0];
    slotProblem.hidden = true;
    if (!file) return;
    const problem = photoProblem(file);
    if (problem) { slotProblem.hidden = false; slotProblem.lastElementChild.textContent = problem; photoInput.value = ''; return; }
    try {
      const created = await ensureDraft();           // the draft is saved on the first photo
      if (!created) { photoInput.value = ''; return; }
      await api.upload(`/pools/${poolId}/photos`, file);
      previews.push(URL.createObjectURL(file));
      photoCount += 1;
      paintSlots(previews);
      open.disabled = false;
      draft.disabled = false;
    } catch (err) { slotProblem.hidden = false; slotProblem.lastElementChild.textContent = managerError(err); }
    photoInput.value = '';
  });

  // limits panel and price hint follow the choices
  const limits = h('div', {});
  const limitsTitle = h('p', { class: 'label' });
  function paintLimits() {
    const c = commodities.find((x) => String(x.commodity_id) === commodity.value) || commodities[0];
    limitsTitle.textContent = t('manager.limits_title', { n: range, commodity: c.name });
    const key = `range${range}_max`;
    const rows = c.parameters.filter((p) => p[key] !== null);
    clear(limits).append(rows.length
      ? h('table', { class: 'table' }, h('thead', {}, h('tr', {}, h('th', {}, t('manager.quality_check')), h('th', { class: 'num' }, t('manager.allowed_at_most')))),
          h('tbody', {}, rows.map((p) => h('tr', {}, h('td', {}, p.name), h('td', { class: 'num' }, `${p[key].includes('.') ? p[key].replace(/0+$/, '').replace(/\.$/, '') : p[key]}${p.unit}`)))))
      : h('p', { class: 'muted' }, t('pool.limits_missing')));
  }
  commodity.addEventListener('change', paintLimits);
  price.input.addEventListener('input', () => { const r = parsePricePerQuintal(price.input.value); price.hintEl.textContent = r.ok ? `${t('manager.price_hint')} = ${formatPricePerKg(r.paisePerKg)} ${t('unit.per_kg')}` : t('manager.price_hint'); });
  paintLimits();

  body.replaceChildren(
    h('div', { class: 'stack' }, breadcrumb([{ label: t('nav.pools'), href: '/manager/pools.html' }, { label: t('manager.new_pool') }]), h('h1', {}, t('manager.create_pool_title'))),
    h('div', { class: 'detail-grid detail-grid--create' },
      h('section', { class: 'card stack' },
        h('div', { class: 'grid cols-2 grid--tight' }, cField.root, variety.root), rangeBox,
        h('div', { class: 'grid cols-2 grid--tight' }, price.root, minOrder.root), h('div', { class: 'grid cols-2 grid--tight' }, packaging.root, storage.root),
        h('div', { class: 'field' }, h('span', { class: 'field__label' }, t('manager.photos_field')), slots, slotProblem),
        draftNote, formProblem,
        h('div', { class: 'row form-actions' }, draft, open, h('span', { class: 'small muted' }, t('manager.open_needs_photo')))),
      h('div', { class: 'stack stack--loose' },
        h('section', { class: 'card stack' }, limitsTitle, limits))),
    photoInput);
}

try {
  commodities = await api.get('/reference/commodities');
  build();
} catch {
  clear(body).append(errorState(t('manager.create_error'), () => location.reload()));
}
document.documentElement.dataset.ready = 'true';
