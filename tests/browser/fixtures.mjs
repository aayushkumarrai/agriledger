// Realistic demo data, created through the real HTTP API (the same way people will): accounts, pools, photos, deposits, a sale.
import { spawn } from 'node:child_process';
import { deflateSync } from 'node:zlib';
import { randomUUID } from 'node:crypto';
import { SERVER_BIN } from './lib.mjs';

export const TEMP = 'Temp-pass-1234';
export const PASS = 'Sup3rSecret-pass';

class Api {
  constructor(base) { this.base = base; this.cookie = ''; }
  async call(method, path, body, form) {
    const headers = {};
    if (this.cookie) headers.Cookie = this.cookie;
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    const res = await fetch(this.base + path, { method, headers, body: form ?? (body === undefined ? undefined : JSON.stringify(body)) });
    const set = res.headers.get('set-cookie');
    if (set && set.startsWith('agri_session=')) this.cookie = set.split(';')[0];
    let data = {};
    try { data = await res.json(); } catch { /* no body */ }
    if (!res.ok && !this.tolerate) throw new Error(`${method} ${path} -> ${res.status} ${JSON.stringify(data)}`);
    return { status: res.status, data };
  }
  get(p) { return this.call('GET', p); }
  post(p, b) { return this.call('POST', p, b ?? {}); }
  patch(p, b) { return this.call('PATCH', p, b); }
  async login(u, pw) { this.cookie = ''; return this.call('POST', '/auth/login', { username: u, password: pw }); }
  async activate(u) {   // first login with the temporary password, then the forced change
    await this.login(u, TEMP);
    await this.post('/auth/change-password', { current_password: TEMP, new_password: PASS });
  }
  async photo(poolId, png) {
    // Node's FormData is sent chunked; browsers and curl send a Content-Length, so build the body the way they do
    const boundary = `----agriFixture${Date.now().toString(16)}`;
    const body = Buffer.concat([Buffer.from(`--${boundary}\r\nContent-Disposition: form-data; name="photo"; filename="lot.png"\r\nContent-Type: image/png\r\n\r\n`), png, Buffer.from(`\r\n--${boundary}--\r\n`)]);
    const res = await fetch(`${this.base}/pools/${poolId}/photos`, { method: 'POST', headers: { Cookie: this.cookie, 'Content-Type': `multipart/form-data; boundary=${boundary}` }, body });
    const data = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(`photo upload -> ${res.status} ${JSON.stringify(data)}`);
    return { status: res.status, data };
  }
}

// A real, small PNG (so the browser draws it): a speckled texture in a base colour, different for every seed.
function png(width, height, base, seed) {
  const crcTable = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k += 1) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
  const crc = (buf) => { let c = 0xffffffff; for (const b of buf) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (type, data) => { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const body = Buffer.concat([Buffer.from(type), data]); const sum = Buffer.alloc(4); sum.writeUInt32BE(crc(body)); return Buffer.concat([len, body, sum]); };
  const raw = Buffer.alloc((width * 3 + 1) * height);
  let state = seed;
  const next = () => { state = (state * 1103515245 + 12345) & 0x7fffffff; return state; };
  for (let y = 0; y < height; y += 1) {
    raw[y * (width * 3 + 1)] = 0;
    for (let x = 0; x < width; x += 1) {
      const wobble = (next() % 61) - 30;
      const stripe = ((x + y * 2) % 17 < 3) ? -22 : 0;
      for (let c = 0; c < 3; c += 1) raw[y * (width * 3 + 1) + 1 + x * 3 + c] = Math.max(0, Math.min(255, base[c] + wobble + stripe));
    }
  }
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0); header.writeUInt32BE(height, 4); header[8] = 8; header[9] = 2;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', header), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

export function createManager(env, username, name) {
  return new Promise((resolve, reject) => {
    const child = spawn(SERVER_BIN, ['create-manager', username, name], { env, stdio: ['pipe', 'ignore', 'inherit'] });
    child.stdin.end(TEMP + '\n');
    child.on('exit', (code) => (code === 0 ? resolve() : reject(new Error(`create-manager failed (${code})`))));
  });
}

// Returns everything the screen tests need to know: logins and ids.
export async function seed(base, env) {
  const unique = Date.now().toString(36);
  const names = { manager: 'Kavita Desai', farmers: ['Basavaraj Hiremath', 'Renukamma Patil', 'Mallikarjun Kulkarni'], buyers: ['Mahantesh Traders', 'Gangadhar Agro Mart'] };
  const out = { manager: { username: `kavita.${unique}`, name: names.manager }, farmers: [], buyers: [], pools: {}, unique };
  await createManager(env, out.manager.username, out.manager.name);
  const m = new Api(base);
  await m.activate(out.manager.username);

  for (const [i, name] of names.farmers.entries()) {
    const username = `farmer${i + 1}.${unique}`;
    const r = await m.post('/users', { name, username, type: 'FARMER', temporary_password: TEMP });
    out.farmers.push({ username, name, id: r.data.user_id });
  }
  for (const [i, name] of names.buyers.entries()) {
    const username = `buyer${i + 1}.${unique}`;
    const r = await m.post('/users', { name, username, type: 'BUYER', temporary_password: TEMP });
    out.buyers.push({ username, name, id: r.data.user_id });
  }
  // a farmer who has never logged in stays "waiting for first login"; the others set their own password
  for (const p of [...out.farmers.slice(0, 2), ...out.buyers]) await new Api(base).activate(p.username);
  out.waiting = out.farmers[2];

  out.managerApi = m;
  const commodities = (await m.get('/reference/commodities')).data;
  const id = (name) => commodities.find((c) => c.name === name).commodity_id;
  const make = async (spec, photoColours, deposits, open = true) => {
    const r = await m.post('/pools', { commodity_id: id(spec.commodity), variety: spec.variety, range_no: spec.range, packaging: spec.packaging, storage_location: spec.storage, market: spec.market || 'Hubballi APMC', min_order_g: spec.min, price_paise_per_kg: spec.price });
    const poolId = r.data.pool_id;
    for (const [i, colour] of photoColours.entries()) await m.photo(poolId, png(400, 300, colour, poolId * 10 + i));
    for (const [farmer, grams] of deposits) await m.post(`/pools/${poolId}/contributions`, { seller_id: out.farmers[farmer].id, quantity_g: grams });
    if (open && photoColours.length) await m.patch(`/pools/${poolId}`, { status: 'open' });
    return poolId;
  };
  out.make = make;
  out.pools.paddy = await make({ commodity: 'Paddy', variety: 'Sona Masuri', range: 1, packaging: '50 kg jute bags', storage: 'Navalgund godown, Dharwad', min: 100000, price: 2500 }, [[196, 160, 78], [176, 142, 70]], [[0, 600000], [1, 800000], [0, 600000]]);
  out.pools.groundnut = await make({ commodity: 'Groundnut with pods', variety: 'JL-24', range: 2, packaging: '40 kg gunny bags', storage: 'Annigeri godown, Dharwad', min: 100000, price: 6783 }, [[184, 132, 84]], [[0, 1000000], [1, 1500000], [2, 1750000]]);
  out.pools.maize = await make({ commodity: 'Maize', variety: 'Pioneer 30V92', range: 1, packaging: '50 kg HDPE bags', storage: 'Hubballi APMC yard', min: 200000, price: 2090 }, [[214, 178, 80]], [[1, 500000]]);
  out.pools.tur = await make({ commodity: 'Tur dal', variety: 'Gulbarga red', range: 1, packaging: '50 kg jute bags', storage: 'Gulbarga godown', min: 100000, price: 7550 }, [], [], false);

  // a sale through the real queue and transaction, so the ledgers and statements have history
  const b1 = new Api(base); await b1.login(out.buyers[0].username, PASS);
  await b1.post(`/pools/${out.pools.paddy}/sales`, { request_id: randomUUID(), quantity_g: 500000, confirmed_price_paise_per_kg: 2500 });
  await b1.post(`/pools/${out.pools.maize}/sales`, { request_id: randomUUID(), quantity_g: 500000, confirmed_price_paise_per_kg: 2090 });   // sells out
  await m.patch(`/pools/${out.pools.paddy}`, { price_paise_per_kg: 2500 }).catch(() => {});
  return out;
}

export { Api, png };
