// A farmer's statement for one pool: four summary tiles and the table of deposits and sales. Used by the Farmer's own screen and
// by the Pool Manager's "farmer statement" screen (same numbers, same words).
import { h, icon } from './dom.js';
import { t } from './i18n.js';
import { tag, poolStatusTag } from './components.js';
import { formatQuantity, formatPaise, formatPricePerQuintal, formatPermille, formatDateTime, padColumn, getUnit, toBigInt } from './format.js';

const signed = (g, unit) => {
  const n = toBigInt(g);
  return `${n < 0n ? '−' : '+'}${formatQuantity(n < 0n ? -n : n, unit)}`;
};

// data: the response of GET /farmers/{id}/statement?pool=. Returns the elements for the whole statement.
export function statementView(data) {
  const unit = getUnit();
  const s = data.summary;
  const unitText = t(`unit.${unit}`);
  const tile = (labelKey, value, unitLabel, note) => h('div', { class: 'tile' }, h('p', { class: 'label' }, t(labelKey)),
    h('p', { class: 'tile__value' }, h('span', { class: 'key-number' }, value), unitLabel ? h('span', { class: 'key-unit' }, unitLabel) : null), note ? h('p', { class: 'small muted' }, note) : null);
  const tiles = h('div', { class: 'grid cols-4' },
    tile('statement.deposited', formatQuantity(s.deposited_g, unit), unitText),
    tile('statement.sold', formatQuantity(s.sold_g, unit), unitText),
    tile('statement.left', formatQuantity(s.left_g, unit), unitText, t('statement.of_pool', { share: formatPermille(s.share_permille) })),
    tile('statement.paid', formatPaise(s.paid_paise), null));

  const L = { date: t('statement.date'), what: t('statement.what'), grain: t(`statement.grain_${unit}`), have: t('statement.have'), money: t('statement.money') };
  const grainCol = padColumn(data.rows.map((r) => signed(r.delta_g, unit)));   // figures in a column share one number of decimals
  const haveCol = padColumn(data.rows.map((r) => formatQuantity(r.left_g, unit)));
  const rows = data.rows.map((r, i) => {
    const sale = r.kind === 'sale';
    const what = sale
      ? t('statement.sale_row', { sold: `${formatQuantity(r.pool_sale_g, unit)} ${unitText}`, price: formatPricePerQuintal(r.unit_price_paise_per_kg), share: formatPermille(r.share_permille) })
      : t('statement.deposit_row');
    return h('tr', {},
      h('td', { class: 'nowrap', 'data-label': L.date }, formatDateTime(r.at)), h('td', { 'data-label': L.what }, what),
      h('td', { class: 'num', 'data-label': L.grain }, grainCol[i]), h('td', { class: 'num', 'data-label': L.have }, haveCol[i]),
      h('td', { class: 'num', 'data-label': L.money }, r.paise === null ? t('common.none') : h('strong', {}, formatPaise(r.paise))));
  });
  const table = h('section', { class: 'card card--table' },
    h('div', { class: 'row row--between statement-head' }, h('p', { class: 'label' }, t('statement.mine_title')),
      data.records_ok ? tag('ok', t('statement.matches'), 'check') : tag('bad', t('status.mismatch'), 'cross')),
    h('table', { class: 'table table--stack' },
      h('thead', {}, h('tr', {}, h('th', {}, L.date), h('th', {}, L.what), h('th', { class: 'num' }, L.grain), h('th', { class: 'num' }, L.have), h('th', { class: 'num' }, L.money))),
      h('tbody', {}, rows)));
  return { tiles, table };
}

// "Open · Price ₹2,500.00 per quintal · Pool stock 15.0 quintal"
export function statementSubtitle(data) {
  const unit = getUnit();
  return [poolStatusTag({ status: data.status, stock_g: data.stock_g, photos: [1] }),
    h('span', { class: 'muted' }, t('statement.subtitle', { price: formatPricePerQuintal(data.price_paise_per_kg), stock: `${formatQuantity(data.stock_g, unit)} ${t(`unit.${unit}`)}` }))];
}
