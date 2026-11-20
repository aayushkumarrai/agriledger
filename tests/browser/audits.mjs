// The checks that run inside the page (Playwright serialises this function, so it must stay self-contained).
// Returns { failures: [...], notes: [...] }. A failure fails the build; a note is information for the reviewer.
export function pageAudit({ viewportWidth, allowManyPrimary }) {
  const failures = [];
  const notes = [];
  const fail = (check, detail) => failures.push({ check, detail });

  const root = document.documentElement;
  const tokenStyle = getComputedStyle(root);
  const hexToRgb = (hex) => { const h = hex.trim().replace('#', ''); return `rgb(${parseInt(h.slice(0, 2), 16)}, ${parseInt(h.slice(2, 4), 16)}, ${parseInt(h.slice(4, 6), 16)})`; };
  const tokenNames = ['--paper', '--card', '--rule', '--field', '--field-border', '--ink', '--muted', '--leaf', '--leaf-tint', '--leaf-text', '--leaf-edge',
    '--clay', '--clay-tint', '--clay-text', '--clay-edge', '--grey-tint', '--grey-text', '--grey-edge'];
  const palette = new Set(tokenNames.map((n) => hexToRgb(tokenStyle.getPropertyValue(n))));
  const allowedAlpha = new Set(['rgba(44, 36, 24, 0.4)', 'rgba(44, 36, 24, 0.14)']);   // pop-up backdrop and its shadow
  const scale = new Set([0, 4, 8, 12, 16, 20, 24, 32]);

  const all = [...document.body.querySelectorAll('*')].filter((el) => !el.closest('svg') || el.tagName.toLowerCase() === 'svg');
  const visible = (el) => { const r = el.getBoundingClientRect(); const s = getComputedStyle(el); return r.width > 0 && r.height > 0 && s.visibility !== 'hidden' && s.display !== 'none'; };
  const label = (el) => `${el.tagName.toLowerCase()}${el.className && typeof el.className === 'string' ? '.' + el.className.trim().split(/\s+/).join('.') : ''}`;
  const parse = (c) => { const m = c.match(/rgba?\(([^)]+)\)/); if (!m) return null; const p = m[1].split(',').map((x) => parseFloat(x)); return { r: p[0], g: p[1], b: p[2], a: p.length > 3 ? p[3] : 1 }; };

  // 1. spacing audit: margin, padding and gap only from 0 / 4 / 8 / 12 / 16 / 20 / 24 / 32 px
  for (const el of all) {
    if (!visible(el)) continue;
    const s = getComputedStyle(el);
    const isFrame = el.classList.contains('page') || el.classList.contains('topbar__inner');   // auto side margins resolve to a pixel value
    const props = ['padding-top', 'padding-right', 'padding-bottom', 'padding-left', 'margin-top', 'margin-right', 'margin-bottom', 'margin-left', 'row-gap', 'column-gap'];
    for (const p of props) {
      const v = s.getPropertyValue(p);
      if (v === 'normal' || v === 'auto' || v === '') continue;
      const px = parseFloat(v);
      if (Number.isNaN(px)) continue;
      if (isFrame && (p === 'margin-left' || p === 'margin-right')) continue;
      if (p.startsWith('margin') && px < 0 && px >= -2 && (el.closest('.nav') || el.matches('.visually-hidden'))) continue;   // the 2 px rule overlap under the active nav item
      if (el.matches('.visually-hidden') || el.matches('.sprite')) continue;
      if (!scale.has(px)) fail('spacing', `${label(el)} ${p} = ${v} is not on the 4/8/12/16/20/24/32 scale`);
    }
  }

  // 2. tokens only: every colour in use is a token (or transparent, or the pop-up shadow/backdrop)
  for (const el of all) {
    if (!visible(el)) continue;
    const s = getComputedStyle(el);
    const colours = [['color', s.color], ['background-color', s.backgroundColor]];
    for (const side of ['top', 'right', 'bottom', 'left']) if (parseFloat(s.getPropertyValue(`border-${side}-width`)) > 0) colours.push([`border-${side}-color`, s.getPropertyValue(`border-${side}-color`)]);
    if (s.outlineStyle !== 'none' && parseFloat(s.outlineWidth) > 0) colours.push(['outline-color', s.outlineColor]);
    for (const [prop, value] of colours) {
      const c = parse(value);
      if (!c || c.a === 0) continue;
      if (!palette.has(`rgb(${c.r}, ${c.g}, ${c.b})`) && !allowedAlpha.has(value)) fail('tokens', `${label(el)} ${prop} = ${value} is not a design token`);
    }
    // one soft shadow, on pop-ups only; hatching is the only gradient
    if (s.boxShadow !== 'none' && !el.classList.contains('modal')) fail('shadow', `${label(el)} has a shadow; only pop-ups may`);
    if (s.backgroundImage.includes('gradient') && !el.classList.contains('photo--empty')) fail('gradient', `${label(el)} uses a gradient`);
    const fam = s.fontFamily;
    if (el.textContent.trim() && !/Noto Sans|Source Serif 4/.test(fam)) fail('fonts', `${label(el)} uses ${fam}`);
  }

  // 3. contrast of text against its background (WCAG AA: 4.5, or 3 for large text)
  const luminance = ({ r, g, b }) => { const f = (v) => { v /= 255; return v <= 0.03928 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; }; return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b); };
  const backgroundOf = (el) => { for (let e = el; e; e = e.parentElement) { const c = parse(getComputedStyle(e).backgroundColor); if (c && c.a > 0.99) return c; } return parse(hexToRgb(tokenStyle.getPropertyValue('--paper'))); };
  const worst = [];
  for (const el of all) {
    if (!visible(el) || el.closest('[disabled], [aria-disabled="true"], .is-disabled')) continue;
    const own = [...el.childNodes].some((n) => n.nodeType === 3 && n.textContent.trim());
    if (!own) continue;
    const s = getComputedStyle(el);
    const fg = parse(s.color);
    const bg = backgroundOf(el);
    const l1 = luminance(fg), l2 = luminance(bg);
    const ratio = (Math.max(l1, l2) + 0.05) / (Math.min(l1, l2) + 0.05);
    const size = parseFloat(s.fontSize), bold = parseInt(s.fontWeight, 10) >= 700;
    const need = size >= 24 || (size >= 18.66 && bold) ? 3 : 4.5;
    worst.push(ratio);
    if (ratio < need) fail('contrast', `${label(el)} text contrast ${ratio.toFixed(2)} < ${need} ("${el.textContent.trim().slice(0, 30)}")`);
  }
  notes.push(`lowest text contrast ${Math.min(...worst).toFixed(2)}`);

  // 4. tap targets: 44 px (48 on a phone); the unit toggle is drawn 36 px with an 8 px invisible extension
  const need = viewportWidth < 720 ? 48 : 44;
  for (const el of document.querySelectorAll('button, a.btn, .nav a, input:not([type=checkbox]):not([type=hidden]):not(.visually-hidden), select, .photo--add, [role=button]')) {
    if (!visible(el) || el.closest('[data-demo-small]')) continue;
    const r = el.getBoundingClientRect();
    const extra = el.classList.contains('toggle__opt') && viewportWidth >= 720 ? 8 : 0;
    const wide = Math.max(r.width, el.classList.contains('toggle__opt') ? 44 : 0);
    if (r.height + extra < need - 0.5 || wide < 44 - 0.5) fail('tap-target', `${label(el)} is ${Math.round(r.width)} x ${Math.round(r.height + extra)} px, needs ${need}`);
  }
  if (viewportWidth >= 720 && !document.querySelector('.backdrop')) {   // the 8 px invisible extension must be clickable, not clipped by a parent (a pop-up covering the page is not a clip)
    for (const opt of document.querySelectorAll('.toggle__opt')) {
      if (!visible(opt) || opt.disabled) continue;
      opt.scrollIntoView({ block: 'center' });   // elementFromPoint only sees what is inside the window
      const r = opt.getBoundingClientRect();
      const hit = document.elementFromPoint(r.left + r.width / 2, r.top - 3);
      if (!hit || hit.closest('.toggle__opt') !== opt) fail('tap-target', `${label(opt)}: the area above the drawn button does not reach it (covered by ${hit ? label(hit) : 'nothing'}), so its tap target is only ${Math.round(r.height)} px`);
    }
  }
  window.scrollTo(0, 0);
  for (const row of document.querySelectorAll('.check')) { if (visible(row) && row.getBoundingClientRect().height < need - 0.5) fail('tap-target', `${label(row)} row is ${Math.round(row.getBoundingClientRect().height)} px tall`); }

  // 5. number alignment: a column of numbers is right-aligned with tabular figures, in the header too
  const numeric = /^[−-]?(₹)?[\d,]+(\.\d+)?%?$/;
  for (const table of document.querySelectorAll('table')) {
    if (!visible(table)) continue;
    const rows = [...table.querySelectorAll('tr')];
    const columns = Math.max(...rows.map((r) => r.children.length));
    for (let c = 0; c < columns; c += 1) {
      const cells = rows.map((r) => r.children[c]).filter(Boolean);
      const data = cells.filter((x) => x.tagName === 'TD');
      if (!data.length || !data.every((x) => numeric.test(x.textContent.trim()))) continue;
      for (const cell of cells) {
        const s = getComputedStyle(cell);
        if (s.textAlign !== 'right') fail('alignment', `column ${c + 1}: ${label(cell)} "${cell.textContent.trim().slice(0, 20)}" is ${s.textAlign}, numbers must be right-aligned`);
        if (!s.fontVariantNumeric.includes('tabular-nums')) fail('alignment', `column ${c + 1}: ${label(cell)} lacks tabular figures`);
      }
      const decimals = new Set(data.map((x) => (x.textContent.trim().split('.')[1] || '').replace('%', '').length));
      if (decimals.size > 1 && !data.every((x) => x.textContent.trim().endsWith('%'))) fail('alignment', `column ${c + 1} mixes decimal places, so the decimal points will not line up`);
    }
  }
  for (const n of document.querySelectorAll('.key-number, .tile__value > *')) { if (visible(n) && !getComputedStyle(n).fontVariantNumeric.includes('tabular-nums')) fail('alignment', `${label(n)} lacks tabular figures`); }

  // 6. fonts: every face this page really uses is a self-hosted face and has loaded (a face is fetched only when text needs it)
  const used = new Set();
  for (const el of all) {
    if (!visible(el) || !el.textContent.trim() || ![...el.childNodes].some((n) => n.nodeType === 3 && n.textContent.trim())) continue;
    const s = getComputedStyle(el);
    used.add(`${s.fontWeight} ${s.fontSize} ${s.fontFamily}`);
  }
  for (const spec of used) if (!document.fonts.check(spec)) fail('fonts', `${spec} is not loaded`);
  const families = new Set([...used].map((u) => (/Source Serif 4/.test(u) ? 'serif' : 'sans')));
  if (!(families.has('serif') && families.has('sans'))) notes.push('only one font family on this page');
  for (const el of all) {   // the rupee sign must come from the self-hosted face its element uses
    if (!visible(el) || ![...el.childNodes].some((n) => n.nodeType === 3 && n.textContent.includes('\u20B9'))) continue;
    const s = getComputedStyle(el);
    if (!document.fonts.check(`${s.fontWeight} ${s.fontSize} ${s.fontFamily}`, '\u20B9')) { fail('fonts', `the rupee sign in ${label(el)} is not rendered from the self-hosted font`); break; }
  }
  notes.push(`${used.size} font styles in use`);

  // 7. photos keep one aspect ratio; icons keep one stroke width; at most one primary button per screen
  for (const p of document.querySelectorAll('.photo')) {
    if (!visible(p)) continue;
    const r = p.getBoundingClientRect();
    const want = p.classList.contains('photo--wide') ? 16 / 9 : 4 / 3;   // two fixed ratios: 4:3 everywhere, 16:9 on list cards
    if (Math.abs(r.width / r.height - want) > 0.08) fail('photo', `${label(p)} is ${Math.round(r.width)} x ${Math.round(r.height)}, not ${want > 1.5 ? '16:9' : '4:3'}`);
    if (r.width < 100) fail('photo', `${label(p)} has collapsed to ${Math.round(r.width)} px wide`);
    const img = p.querySelector('img');
    if (img && (!img.complete || img.naturalWidth === 0)) fail('photo', 'a photo slot image did not load');
  }
  const strokes = new Set([...document.querySelectorAll('svg.icon')].filter(visible).map((i) => getComputedStyle(i).strokeWidth));
  if (strokes.size > 1) fail('icons', `more than one stroke width in use: ${[...strokes].join(', ')}`);
  const primaries = [...document.querySelectorAll('.btn--primary')].filter((b) => visible(b) && !b.closest('[data-demo]'));
  if (!allowManyPrimary && primaries.length > 1) fail('one-primary', `${primaries.length} primary buttons on one screen`);

  // 8. status tags: words always, an icon for bad
  for (const tag of document.querySelectorAll('.tag')) {
    if (!tag.textContent.trim()) fail('tags', 'a status tag has no words');
    if (tag.classList.contains('tag--bad') && !tag.querySelector('svg')) fail('tags', 'a red tag has no icon');
  }

  // 8b. an element marked hidden must really be hidden (a component's display rule once overrode the attribute)
  for (const el of document.querySelectorAll('[hidden]')) if (getComputedStyle(el).display !== 'none') fail('hidden', `${label(el)} has the hidden attribute but is displayed`);

  // 8c. short labels and table cells must not break across lines ("Farmer / A")
  for (const el of document.querySelectorAll('td, .tag, .btn, .toggle__opt, .label, .swatch__name')) {
    if (!visible(el)) continue;
    const text = el.textContent.trim();
    if (!text || text.length > 16) continue;
    const lines = new Set();
    const walker = document.createTreeWalker(el, NodeFilter.SHOW_TEXT);
    for (let node = walker.nextNode(); node; node = walker.nextNode()) {   // only the text itself: one rect per line it occupies
      if (!node.textContent.trim()) continue;
      const range = document.createRange();
      range.selectNodeContents(node);
      for (const r of range.getClientRects()) if (r.width > 0) lines.add(Math.round(r.top / 4));
    }
    if (lines.size > 1) fail('short-text-wraps', `${label(el)} "${text}" breaks across lines`);
  }

  // 8d. a value that was never filled in must not be printed ("null", "undefined", "NaN", "[object Object]")
  const stray = /(^|[^\w.])(null|undefined|NaN|\[object Object\])([^\w]|$)/.exec(document.body.innerText);
  if (stray) fail('stray-value', `the page prints "${stray[2]}"`);

  // 9. layout: nothing scrolls sideways (a stacked table's header is clipped away on a phone, so it is not counted)
  if (root.scrollWidth > viewportWidth + 1) fail('overflow', `page is ${root.scrollWidth} px wide in a ${viewportWidth} px window`);
  for (const el of all) { if (visible(el)) { const r = el.getBoundingClientRect(); if (r.right > viewportWidth + 1 && !el.closest('.sprite') && !el.closest('.table--stack thead')) { fail('overflow', `${label(el)} sticks out to ${Math.round(r.right)} px`); break; } } }

  return { failures, notes };
}
