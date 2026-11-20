// The page shell: loads messages, asks the server who is logged in, sends people to the right place, draws the top bar.
// Every screen starts with startPage(); the server still checks the role on every request (CLAUDE.md rule 5).
import { injectSprite, } from './icons.js';
import { loadMessages, applyI18n, t } from './i18n.js';
import { api, setSessionEndedHandler } from './api.js';
import { h, icon, clear } from './dom.js';
import { LiveFeed } from './live.js';
import { onUnitChange } from './components.js';

export const HOME = { buyer: '/buyer/pools.html', farmer: '/farmer/pools.html', manager: '/manager/pools.html' };

const NAV = {
  buyer: [{ key: 'nav.pools', href: '/buyer/pools.html', id: 'pools' }, { key: 'nav.purchases', href: '/buyer/purchases.html', id: 'purchases' }],
  farmer: [{ key: 'nav.my_pools', href: '/farmer/pools.html', id: 'pools' }, { key: 'nav.my_statement', href: '/farmer/statement.html', id: 'statement' }],
  manager: [{ key: 'nav.pools', href: '/manager/pools.html', id: 'pools' }, { key: 'nav.accounts', href: '/manager/accounts.html', id: 'accounts' }],
};

export async function fetchMe() {
  return api.get('/auth/me');
}

function goLogin(query = '') {
  location.replace(`/login.html${query}`);
}

// Returns { me, main, live } where main is the page content container. `role` is the only role allowed here.
export async function startPage({ role, active, titleKey, onResync = async () => {}, liveHandlers = {} }) {
  injectSprite();
  await loadMessages();
  setSessionEndedHandler(() => goLogin('?ended=1'));
  let me;
  try {
    me = await fetchMe();
  } catch {
    goLogin('?ended=1');
    return new Promise(() => {});   // the page is being replaced
  }
  if (me.must_change_password) { goLogin('?step=password'); return new Promise(() => {}); }
  if (me.role !== role) { location.replace(HOME[me.role]); return new Promise(() => {}); }

  const root = document.getElementById('root');
  const main = h('main', { class: 'page', id: 'main' });
  const menu = h('button', { class: 'topbar__menu', type: 'button', 'aria-expanded': 'false', 'aria-label': t('nav.menu') }, icon('menu', 'icon--lg'));
  const bar = h('header', { class: 'topbar' }, h('div', { class: 'topbar__inner' },
    h('a', { class: 'brand', href: HOME[role] }, t('app.name')),
    h('nav', { class: 'nav', 'aria-label': t('nav.main') }, NAV[role].map((n) => h('a', { href: n.href, 'aria-current': n.id === active ? 'page' : null }, t(n.key)))),
    menu,
    h('div', { class: 'topbar__user' }, h('span', {}, `${t(`role.${role}`)} · ${me.name}`),
      h('button', { class: 'btn btn--secondary', type: 'button', onclick: async () => { try { await api.post('/auth/logout'); } finally { goLogin(); } } }, t('common.log_out')))));
  menu.addEventListener('click', () => { const open = bar.classList.toggle('is-open'); menu.setAttribute('aria-expanded', String(open)); });
  clear(root).append(bar, main);
  document.title = t(titleKey);

  // live feed with its small status note; on any reconnect the page re-fetches first (live.js)
  const note = h('p', { class: 'live-note', role: 'status' });
  const show = (status) => {
    note.className = `live-note${status === 'live' ? ' live-note--live' : ''}`;
    clear(note).append(icon(status === 'live' ? 'check' : status === 'reconnecting' ? 'refresh' : 'clock'), h('span', {}, t(`live.${status}`)));
  };
  const live = new LiveFeed({ onResync, onStatus: show });
  for (const [type, fn] of Object.entries(liveHandlers)) live.on(type, fn);
  live.start();
  onUnitChange(() => {});   // keeps the unit-change event channel alive for pages that subscribe later
  return { me, main, live, note };
}
