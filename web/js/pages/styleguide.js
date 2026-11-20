// The style-guide board (reproduces docs/wireframes/Main.png). Also proves the number, unit and message modules work together.
import { initPage, renderFormats, bindUnitToggles, fillTokenHex, markReady } from '../ui.js';
import { t } from '../i18n.js';
import { getUnit, parseQuantity, formatQuantityInput, formatPaise, totalPaise } from '../format.js';

const PRICE_PAISE_PER_KG = 2500n;   // sample price: 2,500 paise per kg = ₹2,500 per quintal
let grams = 500000n;                // the quantity typed in the demo field, kept as exact grams

const qty = document.getElementById('sg-qty');
const total = document.getElementById('sg-total');
const error = document.getElementById('sg-qty-error');
const errorText = document.getElementById('sg-qty-error-text');

function showTotal() {
  total.value = formatPaise(totalPaise(grams, PRICE_PAISE_PER_KG));
}

function readQuantity() {
  const unit = getUnit();
  const result = parseQuantity(qty.value, unit);
  if (result.ok) {
    grams = result.grams;
    qty.removeAttribute('aria-invalid');
    error.hidden = true;
    showTotal();
    return;
  }
  const key = result.reason === 'too_precise' ? `input.quantity_too_precise_${unit}` : `input.quantity_${result.reason}`;
  errorText.textContent = t(key);
  qty.setAttribute('aria-invalid', 'true');
  error.hidden = false;
}

await initPage();
fillTokenHex();
renderFormats(document);
qty.value = formatQuantityInput(grams, getUnit());
showTotal();
bindUnitToggles((unit) => {
  renderFormats(document, unit);
  qty.value = formatQuantityInput(grams, unit);   // the same quantity in the other unit
  qty.removeAttribute('aria-invalid');
  error.hidden = true;
});
qty.addEventListener('input', readQuantity);
markReady();
