// Farmer: my statement for one pool (summary tiles and a table of deposits and sales built from the ledgers).
import { gradeLabel } from '../components.js';
import { startPage } from '../layout.js';
import { marketCard } from '../market.js';
import { t } from '../i18n.js';
import { api } from '../api.js';
import { h, clear, debounce, append } from '../dom.js';
import { skeleton, errorState, emptyState, unitToggle, onUnitChange, breadcrumb, field, selectWrap, poolCode, button } from '../components.js';
import { statementView, statementSubtitle } from '../statement.js';

let me; let poolId = new URLSearchParams(location.search).get('pool'); let list = []; let data = null;
const body = h('div', { class: 'stack stack--loose' });
const refresh = debounce(() => loadStatement(true), 150);
const handlers = {};
for (const type of ['stock_changed', 'ledger_rows', 'price_changed', 'status_changed']) handlers[type] = (m) => { if (!poolId || String(m.pool_id) === String(poolId)) refresh(); };
const page = await startPage({ role: 'farmer', active: poolId ? 'pools' : 'statement', titleKey: 'pages.farmer_statement', onResync: () => loadStatement(true), liveHandlers: handlers });
me = page.me;
append(page.main, [body, h('div', { class: 'row row--between' }, page.note, h('span', {}))]);

const marketBox = marketCard();
let marketShownFor = null;

function paint() {
  const view = statementView(data);
  if (marketShownFor !== data.pool_id) { marketBox.show(data.commodity_id, data.variety); marketShownFor = data.pool_id; }
  clear(body).append(
    h('div', { class: 'stack' },
      breadcrumb([{ label: t('nav.my_pools'), href: '/farmer/pools.html' }, { label: poolCode(data.pool_id) }]),
      h('div', { class: 'page-head' }, h('h1', {}, `${t('statement.title', { name: data.farmer_name })} · ${data.farmer_code}`), h('div', { class: 'row no-print' }, h('span', { class: 'muted' }, t('unit.show_in')), unitToggle(), button(t('statement.print'), { onclick: () => window.print() }))),
      h('p', { class: 'muted' }, `${poolCode(data.pool_id)} · ${data.commodity} · ${data.variety} · ${gradeLabel(data.range_no)}`),
      h('div', { class: 'row' }, statementSubtitle(data))),
    view.tiles, view.table, view.changes, marketBox.root);
}

async function loadStatement(quiet = false) {
  if (!poolId) return;
  if (!quiet) clear(body).append(skeleton(), skeleton());
  try {
    data = await api.get(`/farmers/${me.user_id}/statement?pool=${poolId}`);
    paint();
  } catch {
    if (!quiet) clear(body).append(errorState(t('statement.error'), () => loadStatement()));
  }
}

try {
  if (!poolId) {   // "My statement" in the menu: start with my first pool
    list = await api.get('/me/pools');
    if (list.length) poolId = String(list[0].pool_id);
  }
  if (!poolId) clear(body).append(emptyState(t('farmer.empty')));
  else await loadStatement();
} catch {
  clear(body).append(errorState(t('statement.error'), () => location.reload()));
}
onUnitChange(() => { if (data) paint(); });
document.documentElement.dataset.ready = 'true';
