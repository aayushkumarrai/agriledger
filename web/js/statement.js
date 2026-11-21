// A farmer's statement for one pool: four summary tiles and the table of deposits and sales. Used by the Farmer's own screen and
// by the Pool Manager's "farmer statement" screen (same numbers, same words).
import { h, icon } from './dom.js';
import { t } from './i18n.js';
import { tag, poolStatusTag } from './components.js';
import { formatQuantity, formatPaise, formatPricePerQuintal, formatPricePerQuintalWhole, formatPermille, formatDateTime, padColumn, getUnit, toBigInt } from './format.js';

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
    const mine = toBigInt(r.delta_g) < 0n ? -toBigInt(r.delta_g) : toBigInt(r.delta_g);
    const what = sale
      ? h('div', { class: 'stack stack--tight' },
        h('span', {}, t('statement.sale_row', { sold: `${formatQuantity(r.pool_sale_g, unit)} ${unitText}`, price: formatPricePerQuintal(r.unit_price_paise_per_kg), mine: `${formatQuantity(mine, unit)} ${unitText}` })),
        h('span', { class: 'small muted' }, t('statement.sale_to', { buyer: r.buyer_name, receipt: r.receipt_no }), r.seal ? ` · ${t('statement.seal', { seal: r.seal.toUpperCase().replace(/^(.{4})(.{4})$/, '$1 $2') })}` : ''))
      : h('div', { class: 'stack stack--tight' }, h('span', {}, t('statement.deposit_row')), r.seal ? h('span', { class: 'small muted' }, t('statement.seal', { seal: r.seal.toUpperCase().replace(/^(.{4})(.{4})$/, '$1 $2') })) : null);
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
  const changes = (data.changes || []).length ? h('section', { class: 'card stack no-print-break' }, h('p', { class: 'label' }, t('statement.changes_title')),
    h('ul', { class: 'plain-list' }, data.changes.map((c) => h('li', {}, h('span', { class: 'muted nowrap' }, formatDateTime(c.at)), ' ',
      c.field === 'PRICE' ? t('statement.change_price', { old: formatPricePerQuintalWhole(c.old_value), new: formatPricePerQuintalWhole(c.new_value) })
        : t('statement.change_status', { old: t(`status.${String(c.old_value).toLowerCase()}`), new: t(`status.${String(c.new_value).toLowerCase()}`) }))))) : null;
  return { tiles, table, changes };
}

// "Open · Price ₹2,500.00 per quintal · Pool stock 15.0 quintal"
export function statementSubtitle(data) {
  const unit = getUnit();
  return [poolStatusTag({ status: data.status, stock_g: data.stock_g, photos: [1] }),
    h('span', { class: 'muted' }, t('statement.subtitle', { price: formatPricePerQuintal(data.price_paise_per_kg), stock: `${formatQuantity(data.stock_g, unit)} ${t(`unit.${unit}`)}` }))];
}
