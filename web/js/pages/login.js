// Screen 1: log in. Screen 2 (first login only): set your own password. One generic error for any wrong detail.
import { injectSprite } from '../icons.js';
import { loadMessages, t } from '../i18n.js';
import { api, ApiError } from '../api.js';
import { h, icon, clear, append } from '../dom.js';
import { field, button, busy, isBusy } from '../components.js';
import { HOME } from '../layout.js';

injectSprite();
await loadMessages();
document.title = t('pages.login');

const root = document.getElementById('root');
const params = new URLSearchParams(location.search);

const card = h('section', { class: 'card auth-card' });
root.append(h('div', { class: 'auth' }, h('header', { class: 'auth-brand' }, h('span', { class: 'brand' }, t('app.name'))), h('main', { class: 'auth-main' }, card)));

function errorLine(id) {
  return h('p', { class: 'notice notice--bad', id, role: 'alert', hidden: true }, icon('cross'), h('span', {}));
}
function show(line, text) {
  line.hidden = !text;
  line.lastElementChild.textContent = text || '';
}

async function goHome() {
  const me = await api.get('/auth/me');
  location.replace(me.must_change_password ? '/login.html?step=password' : HOME[me.role]);
}

function loginScreen(temporaryPassword = '') {
  clear(card);
  const user = field({ id: 'username', label: t('login.username'), input: h('input', { type: 'text', autocomplete: 'username', autocapitalize: 'none', spellcheck: 'false', required: true }) });
  const pass = field({ id: 'password', label: t('login.password'), input: h('input', { type: 'password', autocomplete: 'current-password', required: true }) });
  const submit = button(t('login.submit'), { kind: 'primary', type: 'submit', block: true });
  const problem = errorLine('login-problem');
  const form = h('form', { class: 'stack', novalidate: true }, user.root, pass.root, submit, problem);
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    if (isBusy(submit)) return;
    show(problem, '');
    if (!user.input.value.trim() || !pass.input.value) { show(problem, t('login.invalid')); return; }
    try {
      const result = await busy(submit, () => api.post('/auth/login', { username: user.input.value.trim(), password: pass.input.value }));
      if (result.must_change_password) passwordScreen(pass.input.value); else await goHome();
    } catch (err) {
      show(problem, err instanceof ApiError && err.status === 429 ? t('login.too_many') : err instanceof ApiError && err.status === 401 ? t('login.invalid') : t('error.generic'));
      pass.input.value = '';
      pass.input.focus();
    }
  });
  append(card, [
    h('h1', { class: 'title-page' }, t('login.title')),
    params.get('ended') ? h('p', { class: 'notice notice--grey', role: 'status' }, icon('clock'), h('span', {}, t('error.session_ended'))) : null,
    form,
    h('p', { class: 'small muted' }, `${t('login.new_here')} `, h('a', { href: '/signup.html' }, t('login.sign_up')))]);
  user.input.focus();
}

function passwordScreen(temporary = '') {
  clear(card);
  const temp = field({ id: 'temporary', label: t('password.temporary'), input: h('input', { type: 'password', autocomplete: 'current-password', value: temporary, required: true }) });
  const next = field({ id: 'new-password', label: t('password.new'), input: h('input', { type: 'password', autocomplete: 'new-password', required: true }) });
  const again = field({ id: 'repeat-password', label: t('password.repeat'), input: h('input', { type: 'password', autocomplete: 'new-password', required: true }) });
  const submit = button(t('common.save'), { kind: 'primary', type: 'submit', block: true });
  const problem = errorLine('password-problem');
  const form = h('form', { class: 'stack', novalidate: true }, temp.root, next.root, again.root, submit, problem);
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    if (isBusy(submit)) return;
    show(problem, '');
    next.error(null); again.error(null);
    if (next.input.value.length < 8) { next.error(t('password.too_short')); next.input.focus(); return; }
    if (next.input.value !== again.input.value) { again.error(t('password.mismatch')); again.input.focus(); return; }
    try {
      await busy(submit, () => api.post('/auth/change-password', { current_password: temp.input.value, new_password: next.input.value }));
      await goHome();
    } catch (err) {
      const text = err instanceof ApiError && err.code === 'WRONG_PASSWORD' ? t('password.wrong_temporary')
        : err instanceof ApiError && err.code === 'SAME_PASSWORD' ? t('password.same')
        : err instanceof ApiError && err.status === 429 ? t('login.too_many')
        : err instanceof ApiError && err.status === 401 ? t('error.session_ended')
        : t('error.generic');
      show(problem, text);
      if (err instanceof ApiError && err.status === 401) setTimeout(() => loginScreen(), 1500);
    }
  });
  card.append(h('h1', { class: 'title-page' }, t('password.title')), h('p', {}, t('password.intro')), form);
  (temporary ? next : temp).input.focus();
}

if (params.get('step') === 'password') {
  try { await api.get('/auth/me'); passwordScreen(); } catch { loginScreen(); }
} else {
  loginScreen();   // the front door (index.html) already sends logged-in people to their home screen
}
document.documentElement.dataset.ready = 'true';
