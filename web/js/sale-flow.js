// The purchase pop-ups, shared by the buyer's pool screen and the Pool Manager's "place a sale for a buyer":
// confirm -> accepted receipt, or one of the plain messages for a refusal (spec §6.3, wireframe BuyerFlow).
// "Buy remaining" and "Review and confirm again" reopen the confirmation with new numbers and a NEW request id;
// "Try again" after BUSY (or a lost connection) re-sends the SAME request id, so a purchase can never happen twice.
import { gradeLabel } from './components.js';
import { h, icon } from './dom.js';
import { t } from './i18n.js';
import { api, describeError, newRequestId } from './api.js';
import { button, busy, isBusy, openModal, tag, poolCode } from './components.js';
import { formatQuantity, formatPricePerQuintal, formatPaise, totalPaise, formatDateTime, toBigInt, getUnit } from './format.js';

const rows = (pairs) => h('div', { class: 'modal__rows' }, pairs.map(([label, value]) => h('p', { class: 'modal__row' }, h('span', {}, label), h('span', { class: 'modal__value' }, value))));

// pool: a pool detail (pool_id, commodity, variety, range_no, price_paise_per_kg). grams: the quantity to buy.
// buyer: { id } when the Manager places the sale for that buyer. farmerNames: { seller_id: name } for the Manager's split table.
export function startPurchase({ pool, grams, buyer = null, farmerNames = {}, onFinished = () => {}, onViewPurchases = null }) {
  let order = { grams: toBigInt(grams), paisePerKg: toBigInt(pool.price_paise_per_kg), requestId: newRequestId() };
  let sending = false;
  let replace = () => {};
  let closeModal = () => {};
  let confirmButton = null;
  const unit = () => getUnit();
  const qty = (g) => `${formatQuantity(g, unit())} ${t(`unit.${unit()}`)}`;
  const lot = `${poolCode(pool.pool_id)} · ${pool.commodity} · ${pool.variety} · ${gradeLabel(pool.range_no)}`;
  const total = () => formatPaise(totalPaise(order.grams, order.paisePerKg));

  async function send() {
    if (sending || isBusy(confirmButton)) return;   // a second tap does nothing
    sending = true;
    try {
      const body = { request_id: order.requestId, quantity_g: Number(order.grams), confirmed_price_paise_per_kg: Number(order.paisePerKg) };
      if (buyer) body.buyer_id = buyer.id;
      const result = await busy(confirmButton, () => api.post(`/pools/${pool.pool_id}/sales`, body));
      sending = false;
      replace(receiptView(result));
    } catch (err) {
      sending = false;
      replace(problemView(err));
    }
    onFinished();
  }

  function newOrder(patch) {
    order = { ...order, ...patch, requestId: newRequestId() };
    replace(confirmView());
  }

  function confirmView() {
    confirmButton = button(t('common.confirm'), { kind: 'primary', onclick: send });
    return [
      h('p', {}, tag('grey', t('sale.tag.confirm'))),
      h('h2', {}, t('sale.confirm_title')),
      rows([[t('sale.pool'), lot], [t('sale.quantity'), qty(order.grams)], [t('sale.price'), `${formatPricePerQuintal(order.paisePerKg)} ${t('unit.per_quintal')}`], [t('sale.total'), h('strong', {}, total())]]),
      h('div', { class: 'row' }, button(t('common.go_back'), { onclick: () => closeModal() }), confirmButton),
    ];
  }

  function receiptView(r) {
    const SL = { f: t('table.farmer'), a: t('sale.allocated'), p: t('table.paid_out') };
    const split = r.payouts ? h('table', { class: 'table table--stack' },
      h('thead', {}, h('tr', {}, h('th', {}, SL.f), h('th', { class: 'num' }, SL.a), h('th', { class: 'num' }, SL.p))),
      h('tbody', {}, r.payouts.map((p) => h('tr', {}, h('td', { 'data-label': SL.f }, farmerNames[p.seller_id] || `#${p.seller_id}`), h('td', { class: 'num', 'data-label': SL.a }, qty(p.alloc_g)), h('td', { class: 'num', 'data-label': SL.p }, formatPaise(p.paise)))))) : null;
    return [
      h('p', {}, tag('ok', t('sale.tag.accepted'), 'check')),
      h('h2', {}, t('sale.done_title')),
      rows([[t('sale.receipt'), r.receipt_no], [t('sale.bought'), `${qty(r.quantity_g)} · ${pool.commodity} · ${pool.variety}`],
        [t('sale.price'), `${formatPricePerQuintal(r.unit_price_paise_per_kg)} ${t('unit.per_quintal')}`], [t('sale.date'), formatDateTime(r.created_at)], [t('sale.total'), h('strong', {}, formatPaise(r.gross_paise))]]),
      split,
      h('p', { class: 'small muted' }, t('sale.collect', { place: r.storage_location })),
      onViewPurchases ? button(t('sale.view_purchases'), { kind: 'primary', block: true, onclick: onViewPurchases }) : button(t('common.close'), { kind: 'primary', block: true, onclick: () => closeModal() }),
    ];
  }

  function problemView(err) {
    const d = describeError(err, unit(), { grams: order.grams, paisePerKg: order.paisePerKg });
    const code = err.code;
    const tagKey = { OUT_OF_STOCK: 'stock_gone', INSUFFICIENT_STOCK: 'some_left', PRICE_CHANGED: 'price_changed', BUSY: 'busy' }[code] || 'not_done';
    let actions;
    if (code === 'INSUFFICIENT_STOCK') {
      actions = [button(t('common.cancel'), { onclick: () => closeModal() }), button(d.action, { kind: 'primary', onclick: () => newOrder({ grams: toBigInt(err.data.stock_g) }) })];
    } else if (code === 'PRICE_CHANGED') {
      actions = [button(t('common.cancel'), { onclick: () => closeModal() }), button(d.action, { kind: 'primary', onclick: () => newOrder({ paisePerKg: toBigInt(err.data.current_price_paise_per_kg) }) })];
    } else if (code === 'BUSY' || err.status === 0) {
      const again = button(d.action || t('common.retry'), { kind: 'primary', block: true, onclick: () => { replace(confirmView()); send(); } });   // the SAME request id
      actions = [again];
    } else {
      actions = [button(code === 'OUT_OF_STOCK' ? t('sale.back_to_pools') : t('common.close'), { kind: 'primary', block: true, onclick: () => { closeModal(); if (code === 'OUT_OF_STOCK' && !buyer) location.assign('/buyer/pools.html'); } })];
    }
    const lines = [d.body, d.note].filter(Boolean);
    return [
      h('p', {}, tag('bad', t(`sale.tag.${tagKey}`), 'cross')),
      h('h2', {}, d.title),
      ...lines.map((l) => h('p', { class: 'muted' }, l)),
      code === 'INSUFFICIENT_STOCK' ? h('p', { class: 'total-box' }, `${qty(toBigInt(err.data.stock_g))} × ${formatPricePerQuintal(order.paisePerKg)} = `, h('strong', {}, formatPaise(totalPaise(toBigInt(err.data.stock_g), order.paisePerKg)))) : null,
      h('div', { class: 'row' }, actions),
    ];
  }

  const modal = openModal((close, swap) => { closeModal = close; replace = swap; return confirmView(); }, { locked: () => sending });
  return modal;
}
