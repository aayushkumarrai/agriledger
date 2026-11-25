// Farmer home: one card per pool (my share, my grain left, paid to me) and the latest activity, updating by itself.
import { gradeLabel } from '../components.js';
import { startPage } from '../layout.js';
import { t } from '../i18n.js';
import { api } from '../api.js';
import { h, clear, setValue, debounce, append } from '../dom.js';
import { skeleton, errorState, emptyState, unitToggle, onUnitChange, poolStatusTag, poolCode } from '../components.js';
import { formatQuantity, formatPaise, formatPermille, formatDateTime, getUnit, toBigInt } from '../format.js';
import { summaryStrip, poolsCount } from '../summary.js';

let pools = [];
const cards = h('div', { class: 'pool-grid pool-grid--two', id: 'cards' });
const feed = h('section', { class: 'card card--table', id: 'activity' });
const holder = h('div', { class: 'stack stack--loose' });

const statusLine = h('p', { class: 'page-status' });
const strip = h('div', {});
const refresh = debounce(() => load(true), 150);
const { main, note } = await startPage({
  role: 'farmer', active: 'pools', titleKey: 'pages.farmer_pools', onResync: () => load(true),
  liveHandlers: { stock_changed: refresh, ledger_rows: refresh, price_changed: refresh, status_changed: refresh },
});
append(main, [
  h('div', { class: 'page-head' }, h('div', { class: 'stack stack--tight' }, h('h1', {}, t('farmer.pools_title')), statusLine),
    h('div', { class: 'row' }, h('span', { class: 'muted' }, t('unit.show_in')), unitToggle())),
  strip, holder, h('div', { class: 'row row--between' }, note, h('span', {}))]);

const qty = (g) => `${formatQuantity(g, getUnit())} ${t(`unit.${getUnit()}`)}`;

function card(p) {
  const sold = toBigInt(p.my_deposited_g) - toBigInt(p.my_remaining_g) > 0n || toBigInt(p.paid_to_me_paise) > 0n;
  const fig = (labelKey, value, muted = false) => h('div', { class: 'fig' }, h('p', { class: 'label' }, t(labelKey)), h('p', {}, h('span', { class: `fig__value${muted ? ' fig__value--muted' : ''}` }, value)));
  return h('article', { class: 'card pool-card pool-card--text', dataset: { pool: p.pool_id } },
    h('div', { class: 'pool-card__body' },
      h('div', { class: 'pool-card__content' },
        h('div', { class: 'pool-card__head' }, h('h2', { class: 'pool-card__title' }, `${p.commodity} · ${p.variety}`), poolStatusTag({ status: p.status, stock_g: p.stock_g, photos: [1] })),
        h('p', { class: 'muted small' }, `${t('pool.pool')} ${poolCode(p.pool_id)} · ${gradeLabel(p.range_no)}`),
        h('div', { class: 'grid cols-3 figs' },
          fig('farmer.my_share', formatPermille(p.my_share_permille)),
          h('div', { class: 'fig' }, h('p', { class: 'label' }, t('farmer.grain_left')), h('p', {}, h('span', { class: 'fig__value' }, formatQuantity(p.my_remaining_g, getUnit())), ' ', h('span', { class: 'key-unit' }, t(`unit.${getUnit()}`)))),
          fig('farmer.paid_to_me', formatPaise(p.paid_to_me_paise), !sold)),
        h('p', { class: 'small muted' }, sold ? t('farmer.pool_stock_now', { qty: qty(p.stock_g) }) : t('farmer.nothing_sold'))),
      h('a', { class: 'btn btn--secondary btn--block', href: `/farmer/statement.html?pool=${p.pool_id}` }, t('farmer.view_statement'))));
}

function activityFeed() {
  const rows = pools.flatMap((p) => p.activity.map((a) => ({ ...a, pool: p }))).sort((x, y) => (y.at > x.at ? 1 : y.at < x.at ? -1 : 0)).slice(0, 6);
  clear(feed).append(h('p', { class: 'label feed-label' }, t('farmer.latest')),
    h('ul', { class: 'activity' }, rows.map((a) => {
      const sale = a.kind === 'sale';
      const text = sale
        ? t('farmer.activity_sale', { code: poolCode(a.pool.pool_id), total: qty(a.pool_sale_g), paid: formatPaise(a.paise), mine: qty(-toBigInt(a.my_delta_g)) })
        : t('farmer.activity_deposit', { code: poolCode(a.pool.pool_id), qty: qty(a.my_delta_g) });
      return h('li', { class: 'activity__row' }, h('span', { class: 'muted nowrap' }, formatDateTime(a.at)), h('span', {}, text), h('span', { class: 'num nowrap' }, t('farmer.left', { qty: qty(a.left_g) })));
    })));
}

function paintSummary() {
  clear(strip);
  if (!pools.length) { statusLine.textContent = ''; return; }
  const paid = pools.reduce((s, p) => s + toBigInt(p.paid_to_me_paise), 0n);
  const left = pools.reduce((s, p) => s + toBigInt(p.my_remaining_g), 0n);
  statusLine.textContent = t('summary.grain_in', { pools: poolsCount(t, pools.length) });
  strip.append(summaryStrip([
    { label: t('summary.paid_to_me'), value: formatPaise(paid), note: t('summary.from_all') },
    { label: t('summary.still_unsold'), value: formatQuantity(left, getUnit()), unit: t(`unit.${getUnit()}`), note: t('summary.my_share_across') },
    { label: t('summary.my_pools'), value: String(pools.length), note: t('summary.open_for_sale', { n: pools.filter((p) => p.status === 'open').length }) },
  ]));
}

function paint() {
  paintSummary();
  clear(holder);
  if (!pools.length) { holder.append(emptyState(t('farmer.empty'))); return; }
  clear(cards);
  append(cards, pools.map(card));
  holder.append(cards, feed);
  activityFeed();
}

async function load(quiet = false) {
  if (!quiet) clear(holder).append(skeleton(), skeleton());
  try {
    pools = await api.get('/me/pools');
    paint();
  } catch {
    if (!quiet) { clear(holder).append(errorState(t('farmer.error'), () => load())); }
  }
}
onUnitChange(() => { if (pools.length) paint(); });
await load();
document.documentElement.dataset.ready = 'true';
