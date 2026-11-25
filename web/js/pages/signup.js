// Sign up for Farmers and Buyers. The account starts as "pending verification": the Pool Manager checks the identity document in
// person and approves. There is no Pool Manager sign-up here (Manager accounts are created on the server).
import { injectSprite } from '../icons.js';
import { loadMessages, t } from '../i18n.js';
import { api, ApiError } from '../api.js';
import { h, icon, clear, append } from '../dom.js';
import { field, button, busy, isBusy } from '../components.js';
import { HOME } from '../layout.js';
import { wordmark } from '../logo.js';

injectSprite();
await loadMessages();
document.title = t('pages.signup');

const DOCS = { FARMER: ['APMC_REG', 'LAND_RECORD', 'AADHAAR'], BUYER: ['TRADE_LICENCE', 'GST', 'PAN'] };
let type = 'FARMER';

const root = document.getElementById('root');
const card = h('section', { class: 'card auth-card' });
root.append(h('div', { class: 'auth' },
  h('aside', { class: 'brandpanel' }, wordmark({ inverse: true }), h('div', { class: 'brandpanel__copy' }, h('h2', { class: 'brandpanel__title' }, t('login.panel_title')), h('p', { class: 'brandpanel__text' }, t('login.panel_text')))),
  h('main', { class: 'auth-main' }, card)));

const name = field({ id: 'name', label: t('signup.name'), input: h('input', { type: 'text', autocomplete: 'name', maxlength: '100' }) });
const phone = field({ id: 'phone', label: t('signup.mobile'), input: h('input', { type: 'text', inputmode: 'numeric', autocomplete: 'tel-national', maxlength: '10', spellcheck: 'false' }) });
const doc = field({ id: 'doc', label: t('signup.document'), input: h('select', {}) });
const last4 = field({ id: 'last4', label: t('signup.last4'), input: h('input', { type: 'text', autocomplete: 'off', autocapitalize: 'characters', maxlength: '4', spellcheck: 'false' }) });
const pass = field({ id: 'password', label: t('signup.password'), input: h('input', { type: 'password', autocomplete: 'new-password' }) });
const again = field({ id: 'repeat-password', label: t('signup.repeat'), input: h('input', { type: 'password', autocomplete: 'new-password' }) });
const problem = h('p', { class: 'notice notice--bad', id: 'signup-problem', role: 'alert', hidden: true }, icon('cross'), h('span', {}));
const show = (text) => { problem.hidden = !text; problem.lastElementChild.textContent = text || ''; };

const typeBtns = [['FARMER', 'role.farmer'], ['BUYER', 'role.buyer']].map(([v, k]) => h('button', { class: 'toggle__opt', type: 'button', dataset: { type: v }, 'aria-pressed': String(v === type),
  onclick: () => { type = v; for (const b of typeBtns) b.setAttribute('aria-pressed', String(b.dataset.type === v)); paintDocs(); } }, t(k)));
function paintDocs() {
  clear(doc.input).append(...DOCS[type].map((d) => h('option', { value: d }, t(`doc.${d}`))));
}
paintDocs();

const submit = button(t('signup.submit'), { kind: 'primary', type: 'submit', block: true });
const form = h('form', { class: 'stack', novalidate: true },
  h('div', { class: 'field' }, h('span', { class: 'field__label' }, t('signup.i_am')), h('div', { class: 'toggle', role: 'group', 'aria-label': t('signup.i_am') }, typeBtns)),
  name.root, phone.root, doc.root, last4.root, pass.root, again.root, submit, problem);
form.addEventListener('submit', async (e) => {
  e.preventDefault();
  if (isBusy(submit)) return;
  show('');
  for (const f of [name, phone, doc, last4, pass, again]) f.error(null);
  const checks = [
    [name, name.input.value.trim().length > 0, 'signup.err_name'],
    [phone, /^[6-9][0-9]{9}$/.test(phone.input.value.trim()), 'signup.err_phone'],
    [last4, /^[0-9A-Za-z]{4}$/.test(last4.input.value.trim()), 'signup.err_last4'],
    [pass, pass.input.value.length >= 8, 'password.too_short'],
    [again, again.input.value === pass.input.value, 'password.mismatch'],
  ];
  const bad = checks.filter(([, ok]) => !ok);
  for (const [f, , key] of bad) f.error(t(key));
  if (bad.length) { bad[0][0].input.focus(); return; }
  try {
    const result = await busy(submit, () => api.post('/auth/signup', { type, name: name.input.value.trim(), phone: phone.input.value.trim(), id_doc_type: doc.input.value, id_last4: last4.input.value.trim().toUpperCase(), password: pass.input.value }));
    location.replace(HOME[result.role]);
  } catch (err) {
    if (err instanceof ApiError && err.code === 'DUPLICATE_PHONE') { phone.error(t('signup.phone_taken')); phone.input.focus(); }
    else show(err instanceof ApiError && err.status === 429 ? t('login.too_many') : t('error.generic'));
  }
});
append(card, [h('h1', { class: 'title-page' }, t('signup.title')), form,
  h('p', { class: 'small muted' }, `${t('signup.have_account')} `, h('a', { href: '/login.html' }, t('signup.log_in')))]);
name.input.focus();
document.documentElement.dataset.ready = 'true';
