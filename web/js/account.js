// The account menu in the top bar: a round badge with the person's initials and name; it opens a small pop-up with the name, the
// role and ID, and Log out. Escape or a tap outside closes it and focus goes back to the badge.
import { h, icon } from './dom.js';
import { t } from './i18n.js';

const initialsOf = (name) => name.split(/\s+/).filter(Boolean).slice(0, 2).map((w) => w[0]).join('').toUpperCase();

export function accountMenu({ me, role, onLogout }) {
  const badge = h('span', { class: 'acct__badge', 'aria-hidden': 'true' }, initialsOf(me.name));
  const button = h('button', { class: 'acct__btn', type: 'button', 'aria-haspopup': 'true', 'aria-expanded': 'false', 'aria-label': t('account.open', { name: me.name }) },
    badge, h('span', { class: 'acct__name' }, me.name), icon('chevron-down'));
  const logout = h('button', { class: 'btn btn--secondary btn--block', type: 'button', onclick: onLogout }, t('common.log_out'));
  const menu = h('div', { class: 'acct__menu', hidden: true },
    h('div', { class: 'acct__who' }, h('strong', {}, me.name), h('span', {}, role === 'manager' ? t(`role.${role}`) : `${t(`role.${role}`)} · ${me.code}`)), logout);
  const root = h('div', { class: 'acct' }, button, menu);
  const close = (refocus = false) => { menu.hidden = true; button.setAttribute('aria-expanded', 'false'); if (refocus) button.focus(); };
  const open = () => { menu.hidden = false; button.setAttribute('aria-expanded', 'true'); logout.focus(); };
  button.addEventListener('click', () => (menu.hidden ? open() : close()));
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && !menu.hidden) close(true); });
  document.addEventListener('mousedown', (e) => { if (!menu.hidden && !root.contains(e.target)) close(); });
  return root;
}
