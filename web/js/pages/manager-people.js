// Pool Manager: the people list. Farmers and Buyers with their ID, mobile number and identity document; approve a sign-up,
// edit details, deactivate or reactivate, add someone in person. Deactivate (never delete) keeps every ledger row.
import { startPage } from '../layout.js';
import { t } from '../i18n.js';
import { api } from '../api.js';
import { h, icon, clear, append } from '../dom.js';
import { field, button, busy, isBusy, tag, skeleton, errorState, emptyState, openModal } from '../components.js';
import { formatDate } from '../format.js';
import { managerError } from '../manager.js';

const DOCS = { FARMER: ['APMC_REG', 'LAND_RECORD', 'AADHAAR'], BUYER: ['TRADE_LICENCE', 'GST', 'PAN'] };
const ADJ = ['pale', 'calm', 'bright', 'quiet', 'swift', 'gentle', 'amber', 'green', 'golden', 'misty', 'sunny', 'brave'];
const NOUN = ['river', 'meadow', 'harvest', 'lantern', 'orchard', 'granary', 'monsoon', 'sparrow', 'furrow', 'village', 'bazaar', 'temple'];
const randomIndex = (n) => { const a = new Uint32Array(1); crypto.getRandomValues(a); return a[0] % n; };
const temporaryPassword = () => `${'abcdefghjkmnpqrstuvwxyz'[randomIndex(23)]}${randomIndex(10)}-${ADJ[randomIndex(ADJ.length)]}-${NOUN[randomIndex(NOUN.length)]}-${10 + randomIndex(90)}`;

let people = [];
const body = h('div', { class: 'stack stack--loose' });
const { main, note } = await startPage({ role: 'manager', active: 'people', titleKey: 'pages.manager_people', onResync: () => load(true) });
append(main, [body, h('div', { class: 'row row--between' }, note, h('span', {}))]);
body.append(skeleton(), skeleton());

const problem = h('p', { class: 'notice notice--bad', role: 'alert', hidden: true }, icon('cross'), h('span', {}));
const showProblem = (text) => { problem.hidden = !text; problem.lastElementChild.textContent = text || ''; };
const typeFilter = h('select', { 'aria-label': t('people.type') }, h('option', { value: '' }, t('people.all_types')), h('option', { value: 'FARMER' }, t('people.farmers')), h('option', { value: 'BUYER' }, t('people.buyers')));
const statusFilter = h('select', { 'aria-label': t('people.status') }, h('option', { value: '' }, t('people.all_statuses')), h('option', { value: 'pending' }, t('status.pending')), h('option', { value: 'active' }, t('status.active')), h('option', { value: 'inactive' }, t('status.inactive')));
for (const el of [typeFilter, statusFilter]) { el.classList.add('select'); el.addEventListener('change', paint); }
const wrap = (select) => h('div', { class: 'select-wrap tool-select' }, select, icon('chevron-down'));
const holder = h('div', {});
const addBtn = button(t('people.add_button'), { kind: 'primary', onclick: () => addPerson() });

clear(body).append(
  h('div', { class: 'page-head' }, h('div', { class: 'stack stack--tight' }, h('p', { class: 'label' }, t('manager.eyebrow')), h('h1', {}, t('people.title'))), addBtn),
  h('div', { class: 'page-head page-head--tools' }, h('div', { class: 'page-head__tools' }, wrap(typeFilter), wrap(statusFilter))), problem, holder);

const statusTag = (u) => u.status === 'pending' ? tag('grey', t('status.pending'), 'clock') : u.status === 'active' ? tag('ok', t('status.active'))
  : u.status === 'waiting_first_login' ? tag('grey', t('status.waiting'), 'clock') : tag('grey', t('status.inactive'));
const bucket = (u) => (u.status === 'waiting_first_login' ? 'active' : u.status);

function documentCell(u) {
  if (!u.id_doc_type) return h('span', { class: 'muted' }, t('doc.none'));
  return h('div', { class: 'stack stack--tight' }, h('span', {}, t('doc.masked', { doc: t(`doc.${u.id_doc_type}`), last4: u.id_last4 })),
    u.verified_at ? h('span', { class: 'small muted' }, t('doc.checked', { name: u.verified_by, date: formatDate(u.verified_at.slice(0, 10)) })) : null);
}

function paint() {
  const rows = people.filter((u) => u.type !== 'MANAGER' && (!typeFilter.value || u.type === typeFilter.value) && (!statusFilter.value || bucket(u) === statusFilter.value));
  if (!rows.length) { clear(holder).append(emptyState(t('people.empty'))); return; }
  const L = { id: t('people.id'), name: t('people.name'), mobile: t('people.mobile'), type: t('people.type'), doc: t('people.document'), status: t('people.status') };
  clear(holder).append(h('div', { class: 'card card--table' }, h('table', { class: 'table table--stack' },
    h('thead', {}, h('tr', {}, [L.id, L.name, L.mobile, L.type, L.doc, L.status].map((x) => h('th', {}, x)), h('th', {}, h('span', { class: 'sr-only' }, t('people.col_actions'))))),
    h('tbody', {}, rows.map((u) => h('tr', {},
      h('td', { 'data-label': L.id, class: 'nowrap' }, h('strong', {}, u.code)), h('td', { 'data-label': L.name }, u.name),
      h('td', { 'data-label': L.mobile, class: 'nowrap' }, u.phone || t('common.none')), h('td', { 'data-label': L.type }, t(`role.${u.type.toLowerCase()}`)),
      h('td', { 'data-label': L.doc }, documentCell(u)), h('td', { 'data-label': L.status }, statusTag(u)),
      h('td', { class: 'row-actions' }, h('div', { class: 'row' },
        u.status === 'pending' ? button(t('people.approve'), { onclick: (e) => act(e.currentTarget, `/users/${u.user_id}/approve`) }) : null,
        button(t('people.edit'), { onclick: () => editPerson(u) }),
        u.status === 'inactive' ? button(t('people.reactivate'), { onclick: (e) => act(e.currentTarget, `/users/${u.user_id}/reactivate`) })
          : button(t('people.deactivate'), { onclick: () => confirmDeactivate(u) })))))))));
}

async function act(btn, path) {
  if (isBusy(btn)) return;
  showProblem('');
  try { await busy(btn, () => api.post(path, {})); await load(true); } catch (err) { showProblem(managerError(err)); }
}

function confirmDeactivate(u) {
  openModal((close) => {
    const go = button(t('people.deactivate'), { kind: 'primary', onclick: async () => {
      if (isBusy(go)) return;
      try { await busy(go, () => api.post(`/users/${u.user_id}/deactivate`, {})); close(); await load(true); } catch (err) { close(); showProblem(managerError(err)); }
    } });
    return [h('h2', {}, t('people.deactivate_title', { name: u.name })), h('p', { class: 'muted' }, t('people.deactivate_body')), h('div', { class: 'row' }, button(t('common.cancel'), { onclick: close }), go)];
  });
}

function docFields(type, current = {}) {
  const doc = field({ id: 'p-doc', label: t('signup.document'), input: h('select', {}, DOCS[type].map((d) => h('option', { value: d, selected: d === current.id_doc_type }, t(`doc.${d}`)))) });
  const last4 = field({ id: 'p-last4', label: t('signup.last4'), input: h('input', { type: 'text', autocomplete: 'off', autocapitalize: 'characters', maxlength: '4', spellcheck: 'false', value: current.id_last4 || '' }) });
  return { doc, last4 };
}

function editPerson(u) {
  openModal((close) => {
    const name = field({ id: 'p-name', label: t('signup.name'), input: h('input', { type: 'text', maxlength: '100', value: u.name }) });
    const phone = field({ id: 'p-phone', label: t('signup.mobile'), input: h('input', { type: 'text', inputmode: 'numeric', maxlength: '10', value: u.phone || '' }) });
    const { doc, last4 } = docFields(u.type, u);
    const err = h('p', { class: 'notice notice--bad', role: 'alert', hidden: true }, icon('cross'), h('span', {}));
    const save = button(t('people.save'), { kind: 'primary', onclick: async () => {
      if (isBusy(save)) return;
      err.hidden = true; for (const f of [name, phone, last4]) f.error(null);
      const patch = {};
      if (name.input.value.trim() !== u.name) { if (!name.input.value.trim()) { name.error(t('signup.err_name')); return; } patch.name = name.input.value.trim(); }
      if (phone.input.value.trim() !== (u.phone || '')) { if (!/^[6-9][0-9]{9}$/.test(phone.input.value.trim())) { phone.error(t('people.err_phone')); return; } patch.phone = phone.input.value.trim(); }
      const typed = last4.input.value.trim().toUpperCase();
      if (typed && (doc.input.value !== u.id_doc_type || typed !== u.id_last4)) {
        if (!/^[0-9A-Z]{4}$/.test(typed)) { last4.error(t('signup.err_last4')); return; }
        patch.id_doc_type = doc.input.value; patch.id_last4 = typed;
      }
      if (!Object.keys(patch).length) { close(); return; }
      try { await busy(save, () => api.patch(`/users/${u.user_id}`, patch)); close(); await load(true); }
      catch (e) { err.hidden = false; err.lastElementChild.textContent = managerError(e); }
    } });
    return [h('h2', {}, t('people.edit_title', { name: u.name })), h('div', { class: 'stack' }, name.root, phone.root, doc.root, last4.root, err), h('div', { class: 'row' }, button(t('common.cancel'), { onclick: close }), save)];
  });
}

function addPerson() {
  openModal((close, replace) => {
    let type = 'FARMER';
    const name = field({ id: 'a-name', label: t('signup.name'), input: h('input', { type: 'text', maxlength: '100' }) });
    const phone = field({ id: 'a-phone', label: t('signup.mobile'), input: h('input', { type: 'text', inputmode: 'numeric', maxlength: '10' }) });
    const temp = field({ id: 'a-pass', label: t('manager.temp_password'), input: h('input', { type: 'text', autocomplete: 'off', spellcheck: 'false', value: temporaryPassword() }) });
    const btns = [['FARMER', 'role.farmer'], ['BUYER', 'role.buyer']].map(([v, k]) => h('button', { class: 'toggle__opt', type: 'button', dataset: { type: v }, 'aria-pressed': String(v === type),
      onclick: () => { type = v; for (const b of btns) b.setAttribute('aria-pressed', String(b.dataset.type === v)); } }, t(k)));
    const err = h('p', { class: 'notice notice--bad', role: 'alert', hidden: true }, icon('cross'), h('span', {}));
    const create = button(t('manager.create_account'), { kind: 'primary', onclick: async () => {
      if (isBusy(create)) return;
      err.hidden = true; for (const f of [name, phone, temp]) f.error(null);
      const bad = [[name, name.input.value.trim(), 'signup.err_name'], [phone, /^[6-9][0-9]{9}$/.test(phone.input.value.trim()), 'people.err_phone'], [temp, temp.input.value.length >= 8, 'password.too_short']].filter(([, ok]) => !ok);
      for (const [f, , key] of bad) f.error(t(key));
      if (bad.length) { bad[0][0].input.focus(); return; }
      try {
        await busy(create, () => api.post('/users', { name: name.input.value.trim(), phone: phone.input.value.trim(), type, temporary_password: temp.input.value }));
        replace([h('h2', {}, t('manager.account_created', { name: name.input.value.trim(), username: phone.input.value.trim(), password: temp.input.value })), h('div', { class: 'row' }, button(t('people.done'), { kind: 'primary', onclick: close }))]);
        await load(true);
      } catch (e) { err.hidden = false; err.lastElementChild.textContent = managerError(e); }
    } });
    return [h('h2', {}, t('people.add_title')), h('div', { class: 'stack' },
      h('div', { class: 'field' }, h('span', { class: 'field__label' }, t('manager.account_type')), h('div', { class: 'toggle', role: 'group', 'aria-label': t('manager.account_type') }, btns)),
      name.root, phone.root, temp.root, err), h('div', { class: 'row' }, button(t('common.cancel'), { onclick: close }), create)];
  });
}

async function load(quiet = false) {
  try {
    people = await api.get('/users');
    paint();
  } catch {
    if (!quiet) clear(holder).append(errorState(t('people.error'), () => load()));
  }
}
await load();
document.documentElement.dataset.ready = 'true';
