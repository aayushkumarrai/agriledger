// Demo data for the evaluators, created through the REAL server (E4 -> E2) so every ledger row, hash and receipt is genuine.
// Run through scripts/seed_demo.sh (it starts the server on the dev database). Usage: node scripts/seed_demo.mjs <base-url>
// A plain .sql file cannot do this: the audit hashes and the allocation are computed by the C++ engines (E1/E2).
import { randomUUID } from 'node:crypto';
import { spawn } from 'node:child_process';
import { Api, png } from '../tests/browser/fixtures.mjs';

const base = process.argv[2];
const TEMP = 'Temp-pass-1234';
export const DEMO_PASSWORD = 'Demo-pass-2026';
const bin = process.env.AGRILEDGER_BIN;

function createManager(username, name) {
  return new Promise((resolve, reject) => {
    const child = spawn(bin, ['create-manager', username, name], { env: process.env, stdio: ['pipe', 'ignore', 'inherit'] });
    child.stdin.end(TEMP + '\n');
    child.on('exit', (code) => (code === 0 ? resolve() : reject(new Error('create-manager failed: is the demo data already loaded? (reset with: docker compose down -v)'))));
  });
}
async function activate(username) {   // first login and the forced change, so demo users can log in straight away
  const api = new Api(base);
  await api.login(username, TEMP);
  await api.post('/auth/change-password', { current_password: TEMP, new_password: DEMO_PASSWORD });
}

await createManager('manager', 'Kavita Desai');
const m = new Api(base);
await m.login('manager', TEMP);
await m.post('/auth/change-password', { current_password: TEMP, new_password: DEMO_PASSWORD });

const people = [['farmer.a', 'Basavaraj Hiremath', 'FARMER', '9845000001'], ['farmer.b', 'Renukamma Patil', 'FARMER', '9845000002'], ['farmer.c', 'Mallikarjun Kulkarni', 'FARMER', '9845000003'],
  ['buyer.1', 'Mahantesh Traders', 'BUYER', '9845000004'], ['buyer.2', 'Gangadhar Agro Mart', 'BUYER', '9845000005']];
const ids = {};
for (const [username, name, type, phone] of people) {
  const r = await m.post('/users', { name, username, phone, type, temporary_password: TEMP });
  ids[username] = r.data.user_id;
  await activate(username);
}
// market prices first: the real ones when the server has a data.gov.in key, otherwise the saved sample file (see seed_demo.sh)
let priced = false;
try { await m.post('/market-prices/refresh', {}); priced = true; } catch (e) { console.log('Market prices could not be loaded; the demo pools use fixed prices:', String(e.message).slice(0, 80)); }
const commodities = (await m.get('/reference/commodities')).data;
// a pool's price starts from the market's median modal price for its variety (or the commodity); whole rupees per quintal
async function marketPrice(commodityName, variety, fallback) {
  if (!priced) return fallback;
  const id = commodities.find((c) => c.name === commodityName).commodity_id;
  const data = (await m.get(`/market-prices?commodity_id=${id}&variety=${encodeURIComponent(variety)}`)).data;
  const summary = data.variety || data.commodity;
  return summary ? summary.median_modal_paise_per_kg : fallback;
}
const commodity = (name) => commodities.find((c) => c.name === name).commodity_id;
const make = async (spec, colours, deposits) => {
  const r = await m.post('/pools', { commodity_id: commodity(spec.commodity), variety: spec.variety, range_no: spec.range, packaging: spec.packaging, storage_location: spec.storage, market: spec.market, min_order_g: spec.min, price_paise_per_kg: spec.price });
  const id = r.data.pool_id;
  for (const [i, c] of colours.entries()) await m.photo(id, png(400, 300, c, id * 10 + i));
  for (const [who, grams] of deposits) await m.post(`/pools/${id}/contributions`, { seller_id: ids[who], quantity_g: grams });
  await m.patch(`/pools/${id}`, { status: 'open' });
  return id;
};
// the main demo pool: paddy, Sona Masuri, Category 1, 600 + 800 + 600 kg, priced from today's market
const main = await make({ commodity: 'Paddy', variety: 'Sona Mahsuri', range: 1, packaging: '50 kg jute bags', storage: 'Navalgund godown, Dharwad', market: 'Hubballi APMC', min: 100000, price: await marketPrice('Paddy', 'Sona Mahsuri', 2500) }, [[196, 160, 78], [176, 142, 70]], [['farmer.a', 600000], ['farmer.b', 800000], ['farmer.c', 600000]]);
const second = await make({ commodity: 'Groundnut with pods', variety: 'Gejje', range: 2, packaging: '40 kg gunny bags', storage: 'Annigeri godown, Dharwad', market: 'Hubballi APMC', min: 100000, price: await marketPrice('Groundnut with pods', 'Gejje', 6783) }, [[184, 132, 84]], [['farmer.a', 1125500], ['farmer.b', 1462000]]);
const maizePrice = await marketPrice('Maize', 'Local', 2090);
const sold = await make({ commodity: 'Maize', variety: 'Local', range: 1, packaging: '50 kg HDPE bags', storage: 'Hubballi APMC yard', market: 'Hubballi APMC', min: 200000, price: maizePrice }, [[214, 178, 80]], [['farmer.b', 520500]]);
const b = new Api(base);
await b.login('buyer.1', DEMO_PASSWORD);
await b.post(`/pools/${sold}/sales`, { request_id: randomUUID(), quantity_g: 520500, confirmed_price_paise_per_kg: maizePrice });   // sells out
// one real sign-up waiting for the Manager's approval (shows the People page and the pending state)
await new Api(base).post('/auth/signup', { type: 'FARMER', name: 'Shivappa Naik', phone: '9845000006', id_doc_type: 'LAND_RECORD', id_last4: '4821', password: DEMO_PASSWORD });
console.log(`Demo data ready: P-${main} (paddy, open, 2,000 kg), P-${second} (groundnut, open), P-${sold} (maize, sold out).`);
