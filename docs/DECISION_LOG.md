# AgriLedger — Frontend & Implementation Decision Log (running)

Status: planning conversation of 19 Nov 2026, all topics closed except those in section K. The hand-off package for Claude Code (docs/IMPLEMENTATION_SPEC.md + CLAUDE.md) is generated from this log. The Master Document remains the authority for scope, requirements and engines; items marked **[CHANGE]** differ from it and are listed in the spec's "Amendments to the Master Document".

## A. Audience, look and feel
- Audience: demo evaluators first, but farmer screens are built so a real farmer could use them (big numbers, few words).
- Language: English first; every label in one translation file (`web/i18n/en.json`) so Kannada can be added later.
- Devices: responsive web, desktop first; Pool Manager photo upload must work on a phone (camera). No mobile app (master §3.3 #3).
- Units: stored as integer grams / paise. Display toggle kg <-> quintal, **quintal default**. Money shown as ₹ with two decimals.
- Look: authentic, ledger-book feel, not "AI-made". ONE accent. Palette: paper #F4EEDF, card #FBF7EC, rule #D8CDB4, soil ink #2C2418, muted #6B5D45, leaf green #3B5B2E (only accent), clay red #9C3F2A (errors/mismatch only). Pending = grey + icon. Tints: green #E3E9D6, grey #ECE5D2, red #F1DDD3. Fonts: Source Serif 4 (headings), Noto Sans (body, covers Kannada). No gradients, glass or emoji. Thin ruled lines, tabular numerals.
- The wireframe canvas (12 boards: style sample, login, Pool Manager, Farmer, Buyer, phone) is the visual reference: https://claude.ai/artifact/PKvSj5oBUBEVbbg6wc4gcX
- Payouts are NOT labelled "simulated" on screen; the team explains this orally to evaluators.

## B. What a buyer sees
- Pool = one commodity, ONE variety, one Range (grade), one price. No mixing of varieties.
- Buyer-facing fields: commodity, variety, Range + its limits, price (₹/quintal and ₹/kg), live available stock, minimum order, packaging, storage location, number of contributing farmers (never names), pool ID, 1-3 photos, derived dates (first deposit, last deposit, last sale).
- DROPPED: `crop_year` (first/last deposit dates tell freshness) and `origin` (a pool mixes villages; replaced by storage_location).
- Photos mandatory (min 1, max 3 shown = most recent) before a pool can be opened. Uploaded ONLY by the Pool Manager (mandi model). Append-only, kept with timestamp; files on disk, DB stores path + SHA-256.
- Quality: Option A — store only the pool's Range (1/2/3); the screen shows that Range's limits from the seeded `commodity_parameter` table. No measured sample values in v1.
- Demo commodities (dry-storage): paddy, maize, tur dal, groundnut with pods.
- **Quality numbers: the team decided (19 Nov) to keep the collected numbers for now, assumed correct.** Only paddy Range 1 is filled (moisture 17%, admixture 10%, immature/shrivelled 3%, foreign matter 2%, damaged/weevilled 4%). All other limits are empty and `verified = false` until a teammate copies them from the CURRENT official e-NAM / DMI document.

## C. Pricing and competition
- Fixed price per pool set by the Pool Manager; buyers compete for STOCK (first accepted request wins: per-pool FIFO queue + DB row lock). No bidding in v1 (master §3.3 #4); a possible later phase.
- Pool Manager can change a pool's price for future sales. Every price or status change is an audited event in the hash chain, stored in `pool_change`.
- Reference lines beside the price: MSP (seeded; paddy 2026-27 = ₹2,441/quintal) and latest modal mandi price (seeded snapshot, dated). Display only, never blocks a sale. **Needs guide confirmation** because master §3.3 #4 excludes a market price feed; this is static seeded data, not a feed.

## D. Accounts and access
- No self-registration (D-16): Pool Manager creates farmer and buyer accounts with a temporary password; first login forces a password change.
- Buyers log in and place their own sales; the Pool Manager can also place a sale for a buyer. Who placed it is recorded in `event.actor_user_id`.
- One generic error on a wrong login. UI says "farmer"; tables keep "seller".

## E. Screen inventory (~17 screens, all wireframed)
- Everyone: Login, first-login password change, Logout.
- Pool Manager: Pools list; Pool detail (live stock, price + MSP/market lines, records check + Verify integrity, photos, farmers, three live ledgers); Create pool; Record deposit; Place sale for a buyer; Accounts; Farmer statement.
- Farmer: My pools; pool summary + my statement.
- Buyer: Available pools (out-of-stock pools stay visible, greyed, for a day); Pool detail + buy form; confirmation and result pop-ups; My purchases.

## F. Flows and messages (agreed)
- Login -> role home (Manager: pools list; Farmer: My pools; Buyer: available pools).
- Buyer purchase: quantity (total updates live) -> **Review order** -> confirmation pop-up -> result. The same confirmation applies to "buy remaining".
- Accepted: receipt (number, quantity, price, total, time, collection place).
- Out of stock: "This pool is out of stock." Some stock left: say how much and offer "Buy remaining X quintal" (through the confirmation). Price changed: nothing sold; show old -> new price, new total, "Review and confirm again". Busy: "Many buyers are ordering right now. Please try again in a moment." with a Try again button (retry).
- **No error numbers or codes are ever shown.** Internal reason codes map to plain sentences (spec §6.3).
- Buyers get only their own purchase statement (no per-farmer payouts). The Pool Manager's sale response carries payouts.
- Tamper detection: the Pool Manager DOES see it. The "Records check" shows OK or a failure in plain words naming the broken record (e.g. "Record 14 in the sales ledger does not match"); raw hashes are not shown.
- Live updates (WebSocket): stock, price, status, new ledger rows, reconciliation result; filtered by role; on reconnect re-fetch, then resume.

## G. Buy-request safety (agreed)
- Request carries the price the buyer confirmed; the server rejects with PRICE_CHANGED if it moved.
- Request carries a client-generated unique `request_id` (`sale.request_id` UNIQUE); the Confirm button disables after one tap; a duplicate returns the original result, never a second sale.

## H. Schema changes (APPLIED in `db/schema.sql` v1.1; tested, 34 checks pass)
- `pool`: commodity_id, variety, range_no (1-3), packaging, storage_location, min_order_g, status (draft/open/closed); `grade_label` removed.
- `app_user.must_change_password`; `sale.request_id UUID UNIQUE`.
- New tables: `commodity`, `commodity_parameter` (range1/2/3_max, source, verified), `reference_price`, `pool_photo` (append-only), `pool_change` (append-only). Event types + PRICE_CHANGED, STATUS_CHANGED.
- DB-enforced rules: pool cannot be open without a photo; sale needs an open pool; sale below min order refused unless it takes the whole remainder; photo and pool_change rows immutable; pool_change field must match its event type; application role has SELECT only on reference data.
- Still to update: ER and schema diagram images, master document (see spec amendments).

## I. Implementation sequencing
Freeze contracts -> repo skeleton -> engines in parallel (E2 SQL functions; E1 allocation + hashing; E3 queue vs fake E2; E4 API + frontend) -> integration. Stage prompts are at the end of the spec.

## J. Screen -> API
See spec §6 (full table with request/response shapes).

## K. Open items
- Guide approval: reference-price display (C) and FR-08–FR-10 (D-22). Master §3.3 #6 and FR-01 also need an amendment note (Range 1-3 instead of a free-text grade label).
- Gate 2 date (D-20); user will share the Gate 2 document and fill it in this chat.
- Verify quality limits against the current official document (B). Mandi modal price seed value still to be chosen.
- Contracts in spec §6 and §7 are PROPOSED defaults for the team to review.
- Whether pool commodity / variety / Range may change after creation (proposal: no).
- How a queue timeout is reported (proposal: BUSY, HTTP 503).
