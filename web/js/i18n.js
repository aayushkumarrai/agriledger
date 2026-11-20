// All visible text comes from i18n/en.json by key (CLAUDE.md rule 4). Another language is another JSON file.
let messages = {};

export function setMessages(obj) { messages = obj; }

export async function loadMessages(url = '/i18n/en.json', fetchImpl = fetch) {
  const res = await fetchImpl(url);
  if (!res.ok) throw new Error('messages could not be loaded');
  messages = await res.json();
  return messages;
}

function lookup(key) {
  let node = messages;
  for (const part of key.split('.')) {
    if (node === null || typeof node !== 'object' || !(part in node)) return undefined;
    node = node[part];
  }
  return typeof node === 'string' ? node : undefined;
}

// t('reason.INSUFFICIENT_STOCK.title', { stock: '12.0 quintal' }) -> "Only 12.0 quintal is available right now."
export function t(key, params = {}) {
  const text = lookup(key);
  if (text === undefined) {
    console.error(`missing message: ${key}`);
    return key;   // visible, so a missing key is noticed in testing
  }
  return text.replace(/\{(\w+)\}/g, (whole, name) => {
    if (!(name in params)) { console.error(`message ${key} needs {${name}}`); return whole; }
    return String(params[name]);
  });
}

export function hasMessage(key) { return lookup(key) !== undefined; }

// Fills elements marked data-i18n="key" (text) and data-i18n-attr="aria-label:key;title:key" (attributes).
export function applyI18n(root = document) {
  for (const el of root.querySelectorAll('[data-i18n]')) el.textContent = t(el.dataset.i18n);
  for (const el of root.querySelectorAll('[data-i18n-attr]')) {
    for (const pair of el.dataset.i18nAttr.split(';')) {
      const [attr, key] = pair.split(':');
      if (attr && key) el.setAttribute(attr.trim(), t(key.trim()));
    }
  }
}
