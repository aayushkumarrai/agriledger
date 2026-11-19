# AgriLedger — Implementation Spec (hand-off to Claude Code)

Version 1.0 · 19 Nov 2026 · Team 05 · Theme 04
Written from `docs/DECISION_LOG.md` (all frontend and implementation decisions) and `docs/AgriLedger_Master_Document.md` (scope, requirements, engines).

---

## 0. How to use this document

**Order of authority (highest first):** `CLAUDE.md` → this spec → `docs/AgriLedger_Master_Document.md` → `db/schema.sql`. Where this spec changes the Master Document, §11 lists the change. If two sources still disagree, stop and ask the team.

**Status tags:** **[FINAL]** decided by the team. **[PROPOSED]** a sensible default that the team has not reviewed yet; follow it, keep it easy to change, and list it in your report. **[OPEN]** do not guess; ask.

**Do not invent features.** Master §3.3 (out of scope) is binding: no real payments, no bidding or auctions, no live price feed, no mobile app, no negotiated pricing, no SMS or email.

**Team and ownership.** Four engines, four owners. Keep code for each engine in its own module with a narrow interface, so each owner can explain and defend theirs at Gate 2:

| Engine | Owner | Module |
|---|---|---|
| E1 Audit Ledger (DSA) | Ayush Kumar Rai | `engines/e1_audit` |
| E2 Atomic Sale (DBMS) | Shubhang R | `engines/e2_sale` + `db/` |
| E3 Concurrency (OS) | Sakshi K | `engines/e3_queue` |
| E4 Request Orchestration (Web) | Jairaj Kavatagimath | `engines/e4_web` + `web/` |

---

## 1. Product in one paragraph

Many small farmers pool the same commodity into one shared stock (a **pool**). Buyers buy a quantity from the pool at one fixed price. Every sale atomically reduces stock and splits the money among the farmers in proportion to what each still owns. Three linked ledgers (physical stock, ownership, money) and a hash-chained audit trail make every figure checkable. Three kinds of user log in: **Pool Manager**, **Farmer**, **Buyer** (master §4).

A pool = **one commodity, one variety, one Range (1–3), one fixed price**. Buyers compete for stock, not on price: the first accepted request wins.

---

## 2. Repository layout [PROPOSED]

```
agriledger/
├── CLAUDE.md                     never-break rules (read first, every session)
├── README.md                     how to build, run and test (you write this)
├── docker-compose.yml            PostgreSQL 16 only; loads db/schema.sql then db/seed_reference.sql
├── CMakeLists.txt                top level; one target per engine + the server + tests
├── docs/
│   ├── IMPLEMENTATION_SPEC.md    this file
│   ├── AgriLedger_Master_Document.md
│   ├── DECISION_LOG.md
│   └── wireframes/               PNG of every wireframe board (visual reference)
├── db/
│   ├── schema.sql                v1.1, already written and tested
│   ├── seed_reference.sql        commodities, quality limits, MSP
│   ├── seed_demo.sql             demo users and pools (you write this, §9)
│   └── tests/                    worked_example.sql, negative.sh, setup_test_db.sh
├── engines/
│   ├── e1_audit/                 pure C++ library, no database, no HTTP
│   ├── e2_sale/                  libpqxx transactions
│   ├── e3_queue/                 per-pool FIFO, workers, retry
│   └── e4_web/                   Crow server: REST, WebSocket, auth, validation
├── web/                          static frontend, no build step
│   ├── index.html  login.html  manager/  farmer/  buyer/
│   ├── css/tokens.css  css/base.css  css/components.css
│   ├── js/api.js  js/format.js  js/live.js  js/i18n.js  js/pages/…
│   ├── i18n/en.json
│   └── fonts/                    Source Serif 4 and Noto Sans, self-hosted (works offline at the demo)
├── data/photos/                  uploaded lot photos (git-ignored)
└── tests/                        unit (GoogleTest), integration, concurrency, browser checks
```

---

## 3. Stack and prerequisites [mostly PROPOSED, master §10.1]

C++17, PostgreSQL 15+ (16 recommended), libpqxx, **Crow** (REST + WebSocket), nlohmann/json, OpenSSL (SHA-256), libsodium (argon2id), CMake, GoogleTest, Docker Compose for PostgreSQL. Frontend: static HTML, CSS and vanilla JavaScript served by the C++ server. **No frontend framework, no bundler, no CDN at run time** (fonts and scripts are local, so the demo works offline).

Before Stage 0, check which of these are installed (`g++ --version`, `cmake --version`, `psql --version`, `docker --version`) and tell the user what is missing instead of silently changing the plan.

**Pinned dependencies [FINAL]:** Crow is fetched with CMake `FetchContent` at a pinned release (the tag is recorded in `CMakeLists.txt`). Crow needs standalone Asio; the system package `libasio-dev` is preferred and CMake falls back to a pinned Asio release only if the package is missing. Playwright is installed at Stage 6, not before.

---

## 4. Data model

`db/schema.sql` is the source of truth and is already loaded and tested (`db/tests/`). Do not change it without asking; if you find a defect, report it.

### 4.1 What changed from the Master Document's tables

- `pool`: `commodity_id`, `variety`, `range_no` (1–3), `packaging`, `storage_location`, `min_order_g`, `status` (`draft`/`open`/`closed`). `grade_label` is gone.
- `app_user.must_change_password`; `sale.request_id` (UUID, unique).
- New: `commodity`, `commodity_parameter` (quality limits by Range), `reference_price` (MSP, mandi price; paise per quintal), `pool_photo` (append-only), `pool_change` (append-only).
- New event types: `PRICE_CHANGED`, `STATUS_CHANGED` (each gets a ledger-chain entry; no ledger rows).

### 4.2 Rules the database already enforces (do not duplicate them as the only check; check in E4/E2 too for good messages)

A pool cannot become `open` without ≥ 1 photo · `range_no` 1–3 · a sale needs an `open` pool · a sale below `min_order_g` is refused unless it takes the whole remaining stock · `pool_photo`, `pool_change` and every ledger table are append-only (no update, delete, truncate, even for a superuser) · reference tables are read-only for the application role · `request_id` is unique.

### 4.3 Units [FINAL]

| Thing | Stored and sent over the API | Shown |
|---|---|---|
| Quantity | integer **grams** | quintal (default) or kg; toggle in the UI |
| Price | integer **paise per kg** | ₹ per quintal (default) and ₹ per kg. Note ₹ per quintal = the same number as paise per kg (100 kg × p paise = p rupees) |
| Money | integer **paise** | ₹ with two decimals |
| Reference prices | integer **paise per quintal** | ₹ per quintal |

1 quintal = 100 kg = 100,000 g. **No floating point anywhere** (NFR-02): not in C++, and not in JavaScript for money or quantity. Parse what the user types as a decimal string into integer grams; format integers into text with integer maths or `BigInt`. Indian digit grouping (₹1,25,000.00) via `Intl.NumberFormat('en-IN')` on `BigInt` [PROPOSED]. Quantity display: up to 2 decimals in quintal, up to 3 in kg, trailing zeros trimmed to at least one decimal.

### 4.4 Sale rules (master BR-04, BR-05)

`gross_paise = round_half_up(Q_g × price_paise_per_kg ÷ 1000)`, rounded once. Quantity and payouts are split among farmers by the **largest-remainder method** (ties → lowest `seller_id`), so they always add up exactly. The worked example in master §12 is the reference test: 600/800/600 kg pool, ₹25/kg, sale of 500 kg → 150/200/150 kg, ₹3,750 / ₹5,000 / ₹3,750, gross ₹12,500. The same numbers appear in the wireframes (pool P-12, paddy, Sona Masuri, Range 1).

### 4.5 Decisions made after Stage 0 [FINAL, from the team's answers of 19 Nov 2026]

- **Display unit.** `pool.display_unit` is stored as `'quintal'` for every pool. The kg / quintal toggle is per browser (`localStorage`), not stored on the pool or the user.
- **Price input.** Whole rupees per quintal only (mandi and MSP prices are quoted that way); that number is stored unchanged as paise per kg. A decimal is rejected with a plain message.
- **Quantity input.** Up to 3 decimals in kg, up to 5 decimals in quintal (both are exact grams). More decimals are rejected with a plain message.
- **Percentages** (shares) are shown with one decimal, trailing zero trimmed ("30%", "36.8%"), rounded half-up, computed with integers. This supersedes master §6.1 ("24.32 %").
- **Pool status** is `draft`, `open` or `closed`. **No auto-close.** An `open` pool with stock 0 shows a grey "Out of stock" tag; buyers still see it, greyed, for 24 hours after the sale that emptied it, and a deposit makes it sellable again. Only the Manager closes a pool (`STATUS_CHANGED`). A `closed` pool refuses deposits and sales; the Manager may reopen it (needs a photo, enforced by the database). The Pools list tag "Closed · out of stock" in the wireframe becomes **"Out of stock"** for an open pool with stock 0, and **"Closed"** for a closed pool.
- **Manager's stock ledger feed** also shows price and status changes, read from `pool_change`, as clearly different rows ("Price changed ₹2,500 to ₹2,400"). They are not ledger rows.
- **Deposit form.** The checkbox "I tested this lot and it meets the Range limits" is a required, UI-only confirmation. Nothing is stored; no schema change.
- **Create pool.** The draft is saved on the first photo or on "Save as draft"; photos are uploaded to that draft.
- **Manager-placed sales** go through E3 exactly like buyer sales. There is one production sale path.
- **Unprotected demo mode** (O6 comparison) lives only in the concurrency test harness (`tests/` and the E3 harness): its own scratch table in a separate schema, a plain stock counter, no CHECK constraints, no locks, read-then-write. It is never part of the E2 sale function and never compiled into the server.
- **Test scripts** read `PGHOST`, `PGPORT`, `PGUSER`, `PGDATABASE` (and `PGPASSWORD`) from the environment. The application connects as a LOGIN user that is a member of `agriledger_app`; its dev password lives in a git-ignored `.env`. `db/schema.sql` is not edited.
- **Names** in the README and credits use the spellings in the Master Document.

---

## 5. Engines and interfaces

Follow master §7 (architecture), §8 (ledgers, hashing, reconciliation), §9 (algorithms) and §11 (concurrency). Summary of the sale path:

`Buyer → E4 (auth, validate) → E3 (per-pool FIFO queue) → E2 (one transaction, row lock) ⇄ E1 (allocation, hash) → COMMIT → NOTIFY → E4 listener → WebSocket`

### 5.1 E2 transaction order for a sale [FINAL steps, master §11; additions marked]

1. If a sale with this `request_id` already exists → return its original result (`duplicate: true`). Never create a second sale.
2. `BEGIN`; `SELECT … FROM pool WHERE pool_id = ? FOR UPDATE`.
3. *(addition)* Pool `status` must be `open`, else reject `POOL_NOT_OPEN`.
4. Re-read stock. `stock = 0` → `OUT_OF_STOCK`; `Q > stock` → `INSUFFICIENT_STOCK` (return remaining stock).
5. *(addition)* `Q < min_order_g` and `Q ≠ stock` → `BELOW_MIN_ORDER` (return min order).
6. *(addition)* Request's `confirmed_price_paise_per_kg` ≠ pool price → `PRICE_CHANGED` (return the current price).
7. Lock `pool_seller_balance` rows in `seller_id` order; E1 computes allocations, payouts and the audit entry; insert sale, payouts, the three ledger rows, audit entry; update balances and stock; run reconciliation INV-1…INV-5; `NOTIFY`; `COMMIT`.
8. Lock timeout, deadlock or serialization failure → rollback and retry (E3 policy: up to 3 times, short backoff), then reject `BUSY`.

### 5.2 Other E2 operations

Each is one transaction that locks the pool row, writes its event, ledger rows (if any) and audit entry, and notifies:

| Operation | Event | Notes |
|---|---|---|
| create pool | `POOL_CREATED` | status `draft`, stock 0 |
| record contribution | `CONTRIBUTION` | farmer, quantity > 0; allowed while `draft` or `open`; refused with `POOL_NOT_OPEN` when `closed` |
| add photo | none (row in `pool_photo`) | uploader, path, SHA-256 |
| change price | `PRICE_CHANGED` | writes `pool_change` (field `PRICE`, old/new as paise-per-kg text); affects future sales only |
| change status | `STATUS_CHANGED` | writes `pool_change` (field `STATUS`); opening or reopening needs ≥ 1 photo; only the Manager closes a pool (no auto-close, §4.5) |
| create user | none | Manager only; sets `must_change_password = true` |

[OPEN] whether `commodity`, `variety` and `range_no` may change after creation. Default: **no** (not exposed in the API).

### 5.3 Audit entry hashing [PROPOSED: for Ayush (E1) and Shubhang (E2) to review]

`entry_hash = lowercase hex SHA-256 of the UTF-8 text:`
`pool_id | sequence_no | event_id | event_type | created_at_utc | canonical_rows | previous_hash`
(joined with `|`; integers in plain decimal; the first entry of a pool uses `previous_hash` = 64 zeros).

- `created_at_utc` is **`event.created_at` read back from the database after the event row is inserted**, formatted as UTC ISO-8601 with exactly 6 fractional digits, for example `2026-11-17T11:12:00.000000Z`.
- `canonical_rows` is the concatenation of these segments, in this order, each row ending in `;`. Database-generated ids (`inv_id`, `own_id`, `fin_id`) are not hashed.
  1. inventory rows in insertion order: `I,<movement_type>,<delta_g>,<resulting_stock_g>;`
  2. ownership rows sorted by `seller_id`: `O,<seller_id>,<delta_g>,<resulting_balance_g>;`
  3. financial rows, `SALE_GROSS` first, then payouts sorted by `seller_id`: `F,<entry_type>,<seller_id or ->,<amount_paise>;`
  4. the `pool_change` row, if the event has one (`PRICE_CHANGED`, `STATUS_CHANGED`): `C,<field>,<old_value>,<new_value>,<changed_by>;`
- A text value containing `,` `;` or `|` is refused by E1 (none occurs in the current data).
- **Verification** (E1 walks the chain; E2 supplies the rows read from the database, rebuilt with the same E1 serialiser): per entry, in this order: `GAP` (sequence is not 1, 2, 3 …), `LINK_BROKEN` (`previous_hash` is not the previous entry's hash, or not 64 zeros for entry 1), `HASH_MISMATCH` (recomputed hash differs). E2 then adds `INVARIANT` when INV-1…5 fail. The first broken entry is reported.


---

## 6. REST API contract [PROPOSED defaults on top of master §10.2]

All bodies are JSON (photo upload is `multipart/form-data`). Quantities in grams, prices in paise per kg, money in paise. Auth: random 256-bit session token in an **HttpOnly, SameSite=Strict cookie**; expires after 8 hours [PROPOSED]. Every request is checked for a valid session and for the right role **on the server**.

### 6.1 Endpoints

| Method and path | Who | Purpose |
|---|---|---|
| `POST /auth/login`, `POST /auth/logout` | all | Login returns `{role, must_change_password}`. Wrong username or password: one generic 401. |
| `POST /auth/change-password` | all | Required when `must_change_password` is true; clears the flag |
| `POST /users` · `GET /users` | Manager | Create farmer/buyer account with a temporary password; list accounts |
| `GET /reference/commodities` | all | Commodities with their Range limits (`commodity_parameter`) |
| `GET /reference/prices` | all | Latest MSP and mandi modal price per commodity (paise per quintal, label, date) |
| `GET /pools` | all | List. Buyers see `open` pools with stock > 0, plus `open` pools whose stock reached 0 in the last 24 h (greyed, "Out of stock"). Never `draft` or `closed`. The Manager's list also carries the farmer count and the records-check status. |
| `GET /pools/{id}` | all | Detail (see 6.2) |
| `POST /pools` | Manager | Create pool (draft) |
| `PATCH /pools/{id}` | Manager | `{price_paise_per_kg}` and/or `{status}` |
| `POST /pools/{id}/photos` · `GET /pools/{id}/photos` | Manager upload · all view | JPEG/PNG, max 5 MB, type checked by content not name; the 3 newest are shown to buyers |
| `POST /pools/{id}/contributions` | Manager | `{seller_id, quantity_g}` |
| `POST /pools/{id}/sales` | Buyer (own), Manager (for a buyer) | See 6.3 |
| `GET /pools/{id}/ledgers/inventory\|ownership\|financial` | Manager | Paged rows |
| `GET /pools/{id}/reconciliation` | all | Status only for non-managers |
| `POST /pools/{id}/integrity-check` | Manager | See 6.4 |
| `GET /me/pools` | Farmer | Their pools: share, grain left, paid to me, latest activity |
| `GET /farmers/{id}/statement?pool={id}` | Manager; that Farmer | Contributions, share of sales, payouts, remaining |
| `GET /me/purchases` | Buyer | Their own receipts only |
| `WS /live` | all | See §7 |

### 6.2 Pool detail: who sees what

Everyone: id, commodity, variety, range_no, the Range's limits, price, stock, min order, packaging, storage location, status, contributing-farmer **count**, first/last deposit dates, last sale time, up to 3 photos, MSP and mandi reference. **Farmer names and shares are never sent to buyers.** A Farmer additionally sees their own share (grams, percent, remaining, paid). The Manager additionally sees the farmer list and ledgers.

### 6.3 Sale contract

```json
POST /pools/12/sales
{ "request_id": "3f0c…uuid",            // generated in the browser when the confirmation opens
  "quantity_g": 500000,
  "confirmed_price_paise_per_kg": 2500,  // the price the buyer saw and confirmed
  "buyer_id": 7 }                        // Manager only; for a Buyer the server uses the session user
```

Accepted → `200`:

```json
{ "status": "ACCEPTED", "duplicate": false, "sale_id": 91, "receipt_no": "R-0091",
  "pool_id": 12, "quantity_g": 500000, "unit_price_paise_per_kg": 2500, "gross_paise": 1250000,
  "created_at": "2026-11-17T11:12:00Z", "storage_location": "…",
  "stock_after_g": 1500000,
  "payouts": [ {"seller_id":2,"alloc_g":150000,"paise":375000} ]   // Manager only; never sent to Buyers
}
```

Rejected → `409`: `{ "status":"REJECTED", "reason": CODE, …data }`

| Reason code | Extra data | What the buyer sees (plain words, from `en.json`) |
|---|---|---|
| `OUT_OF_STOCK` | – | "This pool is out of stock." Another buyer purchased the last of it. Nothing was charged. |
| `INSUFFICIENT_STOCK` | `stock_g` | "Only {X} is available right now." + button **Buy remaining {X}** (opens the same confirmation) |
| `PRICE_CHANGED` | `current_price_paise_per_kg` | "The price has changed." From ₹A to ₹B per quintal, new total, nothing bought, button **Review and confirm again** |
| `BELOW_MIN_ORDER` | `min_order_g` | "The minimum order for this pool is {X}." |
| `POOL_NOT_OPEN` | – | "This pool is not open for sale." |
| `BUSY` (HTTP `503`, `Retry-After`) | – | "Many buyers are ordering right now. Please try again in a moment." + **Try again** |

Other statuses: `400` bad input · `401` "Your session has ended. Please log in again." · `403` role not allowed · `404` · anything else "Something went wrong. Please try again." **The server sends codes and data; the browser turns them into sentences. Codes and numbers about errors are never shown to users.** A repeated `request_id` returns the original result with `"duplicate": true`.

A buyer purchase statement (My purchases, receipt) contains only that buyer's own sale: date, pool, quantity, price, total, receipt number.

### 6.4 Integrity check

`POST /pools/{id}/integrity-check` → `{ "ok": true, "checked_entries": 6 }` or
`{ "ok": false, "checked_entries": 6, "first_broken": { "sequence_no": 4, "event_type": "SALE", "reason": "HASH_MISMATCH" | "LINK_BROKEN" | "GAP" | "INVARIANT" } }`.
The screen says it in words, e.g. "Record 4 (a sale) does not match." Raw hashes are never displayed. Event types in plain words: `SALE` sale · `CONTRIBUTION` deposit · `PRICE_CHANGED` price change · `STATUS_CHANGED` status change · `POOL_CREATED` pool creation.

---

## 7. Live feed (WebSocket `/live`) [PROPOSED]

E2 issues `NOTIFY ledger_events, '<event_id>:<pool_id>'` inside the transaction (delivered only on commit). E4's single listener connection fetches the event and sends JSON to subscribed sockets, filtered by role.

```json
{ "type": "stock_changed",   "pool_id": 12, "stock_g": 1500000, "at": "…" }
{ "type": "price_changed",   "pool_id": 12, "old_price_paise_per_kg": 2500, "new_price_paise_per_kg": 2400 }
{ "type": "status_changed",  "pool_id": 12, "status": "open" }
{ "type": "ledger_rows",     "pool_id": 12, "ledger": "inventory|ownership|financial", "rows": [ … ] }   // Manager: all; Farmer: only own ownership/financial rows
{ "type": "reconciliation",  "pool_id": 12, "ok": true }
{ "type": "my_purchase",     "pool_id": 12, "sale": { …receipt… } }                                       // to the buyer who bought only
```

Price and status changes (from `pool_change`) reach the Manager as `ledger_rows` for `inventory` with rows marked `"kind":"change"`; the screen shows them as clearly different rows. They are not ledger rows.

Buyers receive only stock, price, status and their own purchases. On disconnect the page shows a small "reconnecting" note, reconnects with back-off, **re-fetches current state, then resumes**. A rolled-back sale never notifies.

---

## 8. Frontend

The wireframes (`docs/wireframes/*.png`, live canvas linked in the decision log) are the visual reference; this section is the reading order and the rules behind them. Match the wireframes; do not add elements, colours or decoration that are not in them.

### 8.1 Design tokens (`web/css/tokens.css`)

```css
:root{
  --paper:#F4EEDF;  --card:#FBF7EC;  --rule:#D8CDB4;  --field:#FFFDF6;  --field-border:#BFB293;
  --ink:#2C2418;    --muted:#6B5D45;
  --leaf:#3B5B2E;   --leaf-tint:#E3E9D6;  --leaf-text:#2F4A24;  --leaf-edge:#B9C79F;   /* the ONE accent */
  --clay:#9C3F2A;   --clay-tint:#F1DDD3;  --clay-text:#8A3623;  --clay-edge:#DDB3A4;   /* errors and mismatches only */
  --grey-tint:#ECE5D2; --grey-text:#5C4F38; --grey-edge:#CFC4A8;                        /* pending, closed, out of stock */
  --font-head:'Source Serif 4',Georgia,serif;  --font-body:'Noto Sans',system-ui,sans-serif;
  --radius:4px;  --radius-sm:3px;
  --s1:4px; --s2:8px; --s3:12px; --s4:16px; --s5:20px; --s6:24px; --s8:32px;            /* spacing scale: nothing else */
}
```

Contrast checked (WCAG ratios): ink on paper 13.2, muted on paper 5.5, leaf on paper 6.7, paper on leaf 7.2, clay on paper 5.7. All body text ≥ 4.5.

### 8.2 Rules

1. **One accent.** Leaf green marks the primary action, the active nav item and "open/ok". Clay red appears only for errors and mismatches. Everything else is ink, muted brown or paper. No other hues, gradients, shadows, glass or emoji.
2. **Colour never carries meaning alone.** Every status tag has text, and an icon for good/bad (tick, cross, clock).
3. **Type scale:** page title 32 px serif; section title 20–22 px serif; body 15 px; small 13 px; label 12 px uppercase, letter-spacing .06 em; key numbers 26–36 px serif. All numbers use `font-variant-numeric: tabular-nums`; numbers in tables are right-aligned.
4. **Spacing** only from the scale above; page padding 32 px (16 px on phone); cards padding 16–24 px; 1 px `--rule` borders; one **2 px ink border** marks the single most important box on a screen (the price-and-buy box).
5. **Tap targets ≥ 44 px** (48 px on phone). Visible focus ring (2 px leaf outline, 2 px offset) on every control. Labels above fields, never placeholders as labels.
6. **Visual priority** (top to bottom, left to right): what the user came to do and the numbers they act on first (price, stock, quantity, total, the one primary button), then proof and context (quality limits, photos, reference prices), then history (ledgers). One primary button per screen, bottom of its card.
7. **Every screen has four states:** loading (skeleton lines in `--grey-tint`), empty (one plain sentence), error (clay tag + plain sentence + retry), and normal.
8. Numbers update in place when live events arrive; no layout jump, no animation beyond a 300 ms fade of the changed value.
9. Responsive: desktop first (1200 px design width); below 720 px layouts collapse to one column. The **Pool Manager photo upload** and the farmer and buyer screens must be fully usable on a phone (`<input type="file" accept="image/*" capture="environment">`).
10. Words: UI says **farmer** (tables: seller), **deposit** (tables: contribution), **Range 1–3** (never "grade"), **pool**. All text in `web/i18n/en.json`, referenced by key; no literal strings in JS or HTML.
11. Never label payouts "simulated" on screen.

### 8.3 Screens and reading order (all wireframed)

| Screen | Top to bottom |
|---|---|
| **Login** | name · username · password · one button · one generic error line. First login: "Set a new password" (new + repeat). |
| **Manager: Pools** | table: pool, commodity · variety · Range, price, stock, status tag (Open, Draft · needs a photo, Out of stock for an open pool with stock 0, Closed), records-check badge; **Create pool** button |
| **Manager: Pool detail** | title and status → live stock and price (with MSP and mandi reference lines, **Change price**) → **Records check** with **Verify integrity** → photos (**Add photo**) → farmers and shares → inventory, ownership, financial ledgers (live) |
| **Manager: Create pool** | commodity, variety, Range → price, minimum order, packaging, storage location → Range limits panel (read-only, from reference data, note "must be checked against the current official document") → 1–3 photo slots → **Open for sale** (disabled until a photo exists); **Save as draft**. The draft is saved on the first photo or on Save as draft; photos upload to that draft |
| **Manager: Deposit / Sale / Accounts** | record a deposit (pool, farmer, quantity, and a required UI-only checkbox "I tested this lot…" that is not stored); place a sale for a buyer (same confirmation as buyers); create account (name, username, role, temporary password) + account list |
| **Manager: Farmer statement** | farmer and pool picker → summary tiles → statement table |
| **Farmer: My pools** | cards: pool, share, grain left, paid to me, latest activity → **View my statement** |
| **Farmer: Statement** | summary tiles (deposited, sold, paid, left) → table of deposits and sales |
| **Buyer: Available pools** | commodity filter and unit toggle → cards: photo, commodity · variety, Range, price, available, farmers count, min order, storage → **View and buy**; out-of-stock cards greyed with a disabled button |
| **Buyer: Pool detail** | title and status → photos and "About this lot" → Range limits → **price and stock box (2 px border)** with MSP/mandi line, quantity, live total, **Review order** |
| **Buyer: Confirmation and results** | pop-ups: Confirm purchase (pool, quantity, price, total, **Go back** / **Confirm purchase**) → Accepted receipt, or one of the messages in §6.3. **Confirm purchase** disables after one tap. |
| **Buyer: My purchases** | table of the buyer's own receipts |

### 8.4 Buy flow, step by step

1. Buyer enters a quantity (decimal text → exact grams, no floats); total updates live from the displayed price.
2. **Review order** opens the confirmation with the quantity, price and total. A fresh `request_id` is generated now.
3. **Confirm purchase** → `POST …/sales` with the quantity, `confirmed_price_paise_per_kg` and `request_id`; the button disables.
4. On any rejection, show the message from §6.3. **Buy remaining** and **Review and confirm again** reopen the confirmation with new numbers and a **new** `request_id`. **Try again** after `BUSY` re-sends the **same** `request_id`.
5. On acceptance show the receipt and refresh My purchases.

---

## 9. Seed and demo data

`db/seed_reference.sql` (done): commodities, parameter names, paddy Range 1 limits (the only filled numbers; everything else is NULL and `verified = false`), MSP for common paddy 2026-27 (₹2,441 per quintal). **[OPEN]** a teammate verifies the limits against the current official e-NAM / DMI document and fills the rest; the screens must show a limit row only when its number exists, and the Range panel carries the "check against the official document" note until `verified` is true. Mandi modal price: seed one clearly labelled placeholder, e.g. "[market], [date]", until the team supplies a real one.

`db/seed_demo.sql` (you write): 1 Manager, 3 farmers (Farmer A, B, C), 2 buyers; pool P-12 paddy · Sona Masuri · Range 1 at ₹2,500 per quintal with deposits 600/800/600 kg and one photo, **open**; one further open pool and one sold-out pool for the buyer list. Use the real E2 operations to create it so the chain is genuine. Demo passwords go in `README.md`, and `must_change_password` is false for demo users.

---

## 10. Testing

| Level | What | Owner |
|---|---|---|
| Database | `db/tests/worked_example.sql` (INV-1…5 all OK) and `db/tests/negative.sh` (34 checks, all PASS). Keep green; add a check for every new rule. | E2 |
| Unit (GoogleTest) | allocation incl. master §12 rounding example; hash and chain; queue ordering; unit/money parsing and formatting | E1, E3, E4 |
| Integration | role matrix over every endpoint (T-FR-08); sale reasons from §6.3; duplicate `request_id`; price-changed race; failure injection per step (T-FR-09) | E4 |
| Concurrency | master §11 scenarios; 50 concurrent buyers × 20 runs: zero oversold, accepted + rejected = total; **unprotected vs DB-isolated demo mode** (O6); the unprotected mode exists only in the test harness (own scratch table in a separate schema, plain counter, no CHECKs, no locks, read-then-write) and is never compiled into the server | E3 |
| Tamper | edit a row as superuser, run integrity check, it names the exact entry (T-NFR-03) | E1, E2 |
| Browser | Playwright against running server: each screen vs its wireframe at 1200 px and 390 px; buy flow; double-tap; live update in two windows | E4 |
| Static | grep for `float`/`double` in `engines/`; grep for literal UI strings in `web/` | all |

---

## 11. Amendments to the Master Document (tell the guide; master not yet edited)

1. §3.3 #6 and FR-01: a pool's grade is now **Range 1–3** (e-NAM style) stored as `range_no`, not a free-text label.
2. §3.3 #4: seeded **reference prices** (MSP, mandi modal) are *displayed*; there is no price feed and no bidding. Needs the guide's confirmation.
3. New pool fields, photos, `pool_change`, `request_id`, new event types, new DB rules (§4).
4. New API endpoints (§6.1) and the sale contract (§6.3).
5. FR-08–FR-10 and the ledger design still need the guide's approval (D-22).
6. Diagrams (ER, schema) must be redrawn for the new tables (D-24).
7. §6.1 percentages: one decimal, trailing zero trimmed (spec §4.5), not two decimals.
8. Pool status rules (no auto-close, Out of stock tag), whole-rupee price input and the display-unit rule: spec §4.5.
9. Audit hash serialisation for events without ledger rows: spec §5.3.

---

## 12. Build stages and prompts

Work in stages. At the end of each stage: run the tests, commit with a clear message, give the user a five-line report (done, tests, changed or assumed, needs a decision, next), and **wait for approval** before the next stage, unless the user has authorised a run of stages; then stop at the boundary they named. Use plan mode for stages 2–7.

### Stage 0: read and confirm (start prompt)

> Read `CLAUDE.md`, then `docs/IMPLEMENTATION_SPEC.md`, then skim `docs/AgriLedger_Master_Document.md` and `docs/DECISION_LOG.md`, and look at the images in `docs/wireframes/`. Do not write code yet. Reply with: (1) your understanding of the product in 6 lines; (2) the four engines and which module each one lives in; (3) the five rules you must never break; (4) every place where two documents disagree, or where you would otherwise have to guess; (5) which tools from §3 are missing on this machine. Then wait.

### Stage 1: repo skeleton and database

> Create the repo layout from spec §2, `docker-compose.yml` for PostgreSQL 16 that loads `db/schema.sql` and `db/seed_reference.sql`, a top-level `CMakeLists.txt` with one library target per engine plus the server and a GoogleTest target, `.gitignore`, and a `README.md` (build, run, test). Run `db/tests/worked_example.sql` and `db/tests/negative.sh` against the container and make them run from one command. Do not change `db/schema.sql`. Commit.

### Stage 2: E1 allocation and audit hashing

> Plan, then implement `engines/e1_audit` as a pure C++17 library (no database, no HTTP, no floats): largest-remainder allocation for quantity and payouts, `gross_paise` rounding, canonical serialisation, SHA-256 entry hash with `previous_hash`, and chain verification that reports the first broken entry. Write GoogleTest cases for the master §12 worked example and rounding example, and for tampering. Commit.

### Stage 3: E2 atomic operations

> Plan, then implement `engines/e2_sale` with libpqxx: create pool, contribution, add photo, change price, change status, create user, and the sale transaction exactly as spec §5.1 (including request_id, status, min order and price checks), calling E1. Reconciliation INV-1…5 inside every transaction; `NOTIFY` on commit. Tests against the real database: the worked example, every reason code, a duplicate `request_id`, an injected failure after each step. Extend `db/tests/negative.sh` for anything new. Commit.

### Stage 4: E3 queue

> Plan, then implement `engines/e3_queue`: per-pool FIFO queue with mutex and condition variable, worker threads, retry (3 times, short backoff, then `BUSY`), and the concurrency harness with an unprotected mode and the DB-isolated mode. Test master §11 scenarios and 50 concurrent buyers × 20 runs (zero oversold; accepted + rejected = total). Use a fake E2 first, then the real one. Commit.

### Stage 5: E4 server

> Plan, then implement `engines/e4_web` with Crow: sessions (HttpOnly cookie, argon2id), role checks on every route, input validation, all endpoints in spec §6, photo upload with type and size checks, the integrity-check endpoint, and the WebSocket feed in spec §7 using one LISTEN connection. Integration tests: the role matrix over every endpoint, every sale reason code, and live events for two sockets. Commit.

### Stage 6: frontend foundation

> Plan, then build `web/css/tokens.css`, `base.css`, `components.css`, `js/api.js`, `js/format.js` (exact integer parsing and formatting, unit toggle, Indian grouping), `js/live.js` (reconnect, re-fetch, resume), `js/i18n.js` with `i18n/en.json`, self-hosted fonts, and a style-guide page that reproduces `docs/wireframes/Main.png`. Unit-test `format.js`. Show the user the style-guide page in the browser through Playwright and compare it with the wireframe. Commit.

### Stage 7: screens, one role at a time

> Plan, then build the screens for one role per run, in this order: Login and first login; Buyer (pools, pool detail, confirmation and results, My purchases); Farmer; Pool Manager. For each screen match the wireframe in `docs/wireframes/` and spec §8. After building each screen open it with Playwright at 1200 px and 390 px, compare it with its wireframe, and fix differences before moving on. Check all four states (loading, empty, error, normal). Commit per role.

### Stage 8: demo data and end-to-end

> Write `db/seed_demo.sql` through the real E2 operations (spec §9). Write the Playwright end-to-end test: manager creates a pool and photo and opens it; two buyers race for the last stock in two browser windows; price change mid-confirmation; double tap; tamper then Verify integrity. Write a 1-page `DEMO_SCRIPT.md` for the evaluators.

### Stage 9: review

> Run the `code-review` and `security-guidance` plugins over the whole repo and fix what they find. Re-run every test. Update `README.md`. Give the user a final report including any gaps against the spec.

---

## 13. Open items (also in `docs/DECISION_LOG.md` §K)

Guide approval for reference prices and FR-08–FR-10 · Gate 2 date (D-20), and the Gate 2 document the user will share · verify quality limits against the current official document · choose the mandi modal price seed · whether pool commodity, variety and Range can change after creation (default no) · master document and diagrams to be updated · review of the hash serialisation (§5.3) by the E1 and E2 owners.
