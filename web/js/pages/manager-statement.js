// Pool Manager: the statement of any farmer for any pool (the same view the farmer sees).
import { startPage } from '../layout.js';
import { t } from '../i18n.js';
import { api, ApiError } from '../api.js';
import { h, clear, append, icon } from '../dom.js';
import { field, unitToggle, onUnitChange, skeleton, errorState, emptyState, breadcrumb, poolCode } from '../components.js';
import { statementView, statementSubtitle } from '../statement.js';
import { poolLabel } from '../manager.js';

const params = new URLSearchParams(location.search);
const body = h('div', { class: 'stack stack--loose' });
const holder = h('div', {});
const { main, note } = await startPage({ role: 'manager', active: 'pools', titleKey: 'pages.manager_statement', onResync: () => show(true) });
append(main, [body, h('div', { class: 'row row--between' }, note, h('span', {}))]);
let data = null; let farmerSel; let poolSel;

async function show(quiet = false) {
  if (!farmerSel || !poolSel.value || !farmerSel.value) return;
  if (!quiet) clear(holder).append(skeleton(), skeleton());
  try {
    data = await api.get(`/farmers/${farmerSel.value}/statement?pool=${poolSel.value}`);
    paint();
  } catch (err) {
    if (quiet) return;
    clear(holder);
    if (err instanceof ApiError && err.status === 404) holder.append(emptyState(t('manager.no_share')));
    else holder.append(errorState(t('statement.error'), () => show()));
  }
}

function paint() {
  const view = statementView(data);
  clear(holder).append(h('div', { class: 'stack stack--loose' }, h('div', { class: 'row' }, statementSubtitle(data)), view.tiles, view.table, view.changes));
}

try {
  const [users, pools] = await Promise.all([api.get('/users'), api.get('/pools')]);
  const farmers = users.filter((u) => u.type === 'FARMER');
  const f = field({ id: 'st-farmer', label: t('manager.farmer'), input: h('select', {}, farmers.map((u) => h('option', { value: u.user_id }, u.name))) });
  const p = field({ id: 'st-pool', label: t('manager.pool'), input: h('select', {}, pools.filter((x) => x.status !== 'draft').map((x) => h('option', { value: x.pool_id }, poolLabel(x)))) });
  farmerSel = f.input; poolSel = p.input;
  if (params.get('farmer')) farmerSel.value = params.get('farmer');
  if (params.get('pool')) poolSel.value = params.get('pool');
  farmerSel.addEventListener('change', () => show()); poolSel.addEventListener('change', () => show());
  body.append(
    h('div', { class: 'stack' }, breadcrumb([{ label: t('nav.pools'), href: '/manager/pools.html' }, { label: t('manager.farmer_statement') }]),
      h('div', { class: 'page-head' }, h('h1', {}, t('manager.farmer_statement')), h('div', { class: 'row' }, h('span', { class: 'muted' }, t('unit.show_in')), unitToggle()))),
    h('div', { class: 'grid cols-2 grid--tight' }, f.root, p.root), holder);
  if (!farmers.length || !pools.length) holder.append(emptyState(t('manager.no_pools')));
  else await show();
} catch {
  body.append(errorState(t('manager.accounts_error'), () => location.reload()));
}
onUnitChange(() => { if (data) paint(); });
document.documentElement.dataset.ready = 'true';
