// One inline line-icon set: 24 x 24 grid, a single 1.75 stroke, round caps and joins, no fills, no emoji.
// Use as <svg class="icon" aria-hidden="true"><use href="#i-check"/></svg> after injectSprite() has run.
const PATHS = {
  check: '<path d="M5 12.5l4.5 4.5L19 7.5"/>',
  cross: '<path d="M6.5 6.5l11 11M17.5 6.5l-11 11"/>',
  clock: '<circle cx="12" cy="12" r="8.5"/><path d="M12 7.5V12l3 2"/>',
  plus: '<path d="M12 5.5v13M5.5 12h13"/>',
  'chevron-down': '<path d="M6.5 9.5l5.5 5.5 5.5-5.5"/>',
  'arrow-right': '<path d="M5 12h14M13.5 6.5L19 12l-5.5 5.5"/>',
  image: '<rect x="4" y="5" width="16" height="14" rx="2"/><circle cx="9" cy="10" r="1.5"/><path d="M4.5 17l4.5-4.5 3.5 3.5 2.5-2.5 4.5 4.5"/>',
  camera: '<path d="M4 8.5a1.5 1.5 0 0 1 1.5-1.5H8l1.3-2h5.4L16 7h2.5A1.5 1.5 0 0 1 20 8.5v9a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 17.5z"/><circle cx="12" cy="13" r="3.5"/>',
  alert: '<path d="M12 4.5l8.5 14.5h-17z"/><path d="M12 10.5v4"/><path d="M12 16.9v.1"/>',
  menu: '<path d="M5 7.5h14M5 12h14M5 16.5h14"/>',
  refresh: '<path d="M19 12a7 7 0 1 1-2.1-5"/><path d="M19 5v4.5h-4.5"/>',
  lock: '<rect x="5.5" y="11" width="13" height="8.5" rx="2"/><path d="M8.5 11V8.5a3.5 3.5 0 0 1 7 0V11"/>',
  upload: '<path d="M12 16V5.5M7.5 10L12 5.5l4.5 4.5M5 19h14"/>',
};

export const ICON_NAMES = Object.keys(PATHS);

export function spriteMarkup() {
  const symbols = ICON_NAMES.map((n) => `<symbol id="i-${n}" viewBox="0 0 24 24">${PATHS[n]}</symbol>`).join('');
  return `<svg xmlns="http://www.w3.org/2000/svg" class="sprite" aria-hidden="true" focusable="false">${symbols}</svg>`;
}

// Adds the sprite once to the page (hidden). Safe to call again.
export function injectSprite(doc = document) {
  if (doc.getElementById('i-check')) return;
  const holder = doc.createElement('div');
  holder.innerHTML = spriteMarkup();
  doc.body.prepend(holder.firstElementChild);
}

// Markup for one icon, for code that builds elements.
export function iconMarkup(name, extraClass = '') {
  if (!PATHS[name]) throw new Error(`unknown icon: ${name}`);
  return `<svg class="icon${extraClass ? ' ' + extraClass : ''}" aria-hidden="true" focusable="false"><use href="#i-${name}"/></svg>`;
}
