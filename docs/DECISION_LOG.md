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

## L. Answers to the Stage 0 report (19 Nov 2026) [FINAL unless marked]
1. Unprotected demo mode is NOT a flag on the E2 sale function. It lives only in the concurrency test harness, on its own scratch table in a separate schema (plain counter, no CHECKs, no locks, read-then-write), and is never compiled into the server. CLAUDE.md rule 3 reworded.
2. Percent display: one decimal, trailing zero trimmed, half-up. Master §6.1 superseded.
3. Reports use the five-line format from CLAUDE.md.
4. `display_unit` stored as 'quintal'; kg/quintal toggle per browser (localStorage).
5. Price input: whole rupees per quintal only; decimals rejected with a plain message.
6. Out of stock: no auto-close. Open pool with stock 0 = grey "Out of stock" tag, visible to buyers (greyed) for 24 h; a deposit makes it sellable again. Only the Manager closes (STATUS_CHANGED); a closed pool refuses deposits and sales; reopening needs a photo. Pools list wireframe tag "Closed · out of stock" becomes "Out of stock" (open, stock 0) and "Closed" (closed).
7. Price and status changes appear in the Manager's stock ledger feed as different rows, read from `pool_change`; they are not ledger rows.
8. Hash input for PRICE_CHANGED / STATUS_CHANGED: the `pool_change` fields in a fixed order; timestamps are UTC ISO-8601 with microseconds read back from the database after insert. **PROPOSED**, for Ayush (E1) and Shubhang (E2) to review. Written up in spec §5.3.
9. "I tested this lot" checkbox: UI-only required confirmation, no schema change.
10. Create pool: draft saved on the first photo or on "Save as draft", then photos upload to it.
11. Quantity precision: 3 decimals in kg, 5 in quintal; more is rejected with a plain message.
12. Test scripts take PGHOST, PGPORT, PGUSER, PGDATABASE from the environment; the app role gets a LOGIN user whose dev password is in a git-ignored `.env`. `db/schema.sql` is not edited.
13. Manager-placed sales go through E3 like buyer sales.
14. README and credits use the Master Document's spellings of names.
15. Crow pinned with CMake FetchContent; `libasio-dev` preferred (apt). Playwright is installed at Stage 6.

## M. Visual quality bar (20 Nov 2026) [FINAL]
- Refined and restrained, not bold or trendy; follow the wireframes and tokens exactly. Self-hosted fonts, one inline line-icon set (single stroke width, no emoji), hairline borders instead of shadows (one soft shadow only on pop-ups; this overrides the earlier "no shadows" rule and the wireframe pop-up look), photo slots with a fixed aspect ratio, realistic demo data.
- States for every control: hover, focus, pressed, disabled, loading, empty, error.
- Per screen: screenshots at 1200 px and 390 px, squint test, greyscale test, spacing audit against the 4/8/12/16/20/24/32 scale, number alignment; fix before moving on and list the fixes.

## N. Change round, 21 Nov (user's review list; decisions agreed)
- Wording: "Range" is shown as **Grade** (key names unchanged); "Reconciled" is **Records verified** / **Records need checking**; "The three ledgers" is **Pool history** with **Stock movements**, **Farmer shares**, **Payments**; buyer list button is **View**.
- No explanatory helper text on screens (process notes, "updates by itself", "button turns off after one tap", etc.); only labels, values, errors and refusals.
- Manager keeps the in-person buyer sale form (label: "For in-person buyers"); buyers buy on their own from any device.
- Agreed, not built yet: self sign-up for Farmer and Buyer with generated IDs and Manager approval (reverses D-16); Manager accounts stay invite-only; People page (approve, edit, deactivate); identity check recorded as document type, masked number, verified by and date (no card photos; research first); optional Market field and buyer filter (schema change approved); farmer statement as a bill, showing the buyer's name; minimal transparency items (payout maths, record seal, price history). Google sign-in and mobile OTP are out of scope (no SMS or outside calls) and listed as future work.

## O. Sign-up, approval and identity (21 Nov; reverses D-16)
- Farmers and Buyers sign up themselves (mobile number is the sign-in name; username still works for older accounts). Self sign-up starts PENDING; the Pool Manager approves on the People page. Pool Manager accounts are created only on the server.
- A pending farmer cannot receive deposits and a pending buyer cannot buy (enforced in E2, inside the transaction). Deactivate (never delete) keeps every ledger row and ends the person's sessions.
- Identity record: document type from a fixed list (farmers: APMC registration, land record, Aadhaar; buyers: trade licence, GST, PAN), last four characters only, who checked it and when. No card images and no full numbers are stored (privacy; Aadhaar rules, DPDP Act 2023). PROPOSED: the document lists and the "checked in person" approach, pending the research on what APMCs ask for.
- Generated public IDs: F-0002, B-0005, M-0001 (from the account number; unique).
- Pools have a Market (APMC) field; buyers can filter by it when there is more than one market. Several managers per market is out of scope.
- Not built: Google sign-in and mobile OTP (need outside services and SMS, out of scope).
- Schema change (approved by the user, 21 Nov): app_user (phone, public_code, approval_status, id_doc_type, id_last4, verified_by, verified_at), pool.market. Existing databases need `docker compose down -v` and a re-seed.

## P. Visual polish round, 21-22 Nov (from the design critique; user approved the new token, sentence-case labels and no eyebrows)
- New token `--rule-strong #8A7C60` (3.8:1 on the card) for field, toggle and checkbox edges; the card rule colour stays for containers.
- Form and card labels are sentence case; capitals are kept for table headers only. Eyebrow lines above page titles are removed.
- Units are small muted sans. The buyer list button is outline and the whole card opens the pool; availability is smaller than price. "Log out" and "Close pool" are ghost buttons. The stock bar is thinner. Statement shows "Paid to me" first.
- "Verify integrity" is now **Check records**; after a check the Records card shows when it was checked and a short seal (first and last four characters of the newest record's hash). The seal uses the existing sans with even digits: a monospace face would add a text style beyond the audited type scale.
- Not adopted from the critique: 48 px spacing step, `--ink-2` and other extra tokens, In/Out ledger columns, wordmark change, clay-coloured "Close pool" (clay is for errors only).

## Q. Market prices from Agmarknet (25 Nov; the user approved this change of scope and the schema change)
- Source: the free data.gov.in dataset "Current Daily Price of Various Commodities from Various Markets (Mandi)" (Agmarknet), read with the user's own API key from `.env` (`DATA_GOV_API_KEY`). The agmarknet.gov.in report page is not scraped. The server runs `curl` with the key passed on stdin (so it is not in the process list). Python's default web client was refused by the service; curl works.
- The Pool Manager presses "Update market prices" (not automatic, at most once a minute). The quotes are saved with market, variety, the market's own grade (FAQ, Local ...), date, lowest, highest and most common price. Prices are whole rupees per quintal, stored as the same number of paise per kg. A failed update keeps the last saved prices.
- Shown: a Market prices card (median of the most common prices across markets for the pool's variety, and all varieties together, lowest and highest, and a table of quotes) on the Create pool, Pool Manager pool, buyer pool and farmer statement screens, with "Source: Agmarknet via data.gov.in" and the date.
- Our four commodities are matched to the dataset's names: Paddy(Common), Maize, Groundnut (kernels, "Shelled", are left out because pools are groundnut with pods), and both whole and split tur (as varieties of one commodity, "Tur (Arhar)", renamed from "Tur dal").
- Varieties are chosen from a list built from the quotes (plus "Other"). Agmarknet's grade is a market label and does NOT replace the pool's Grade 1 to 3 (which decides the quality limits). Grade labels now read "Grade 1 (best)", "Grade 2", "Grade 3 (basic)".
- The Pool Manager's price field stays blank: the market prices are shown while the Manager decides. The demo pools' prices start from the market median on the day the demo data is loaded; there is no longer a worked-example pool at a fixed price in the demo.
- Offline mode: `AGRILEDGER_MARKET_FEED_FILE` points at saved quotes (tests/fixtures/market_sample.json, real quotes from 25 Nov 2026). Tests never use the internet.
- Schema (approved): tables `market_price` and `variety_catalog`; commodity renamed in the seed data. Existing databases need `docker compose down -v` and a re-seed.

## R. Wording and automatic prices (26 Nov, the user's requests)
- "Grade" is now **Category** everywhere: "Category 1 (best)", "Category 2 (standard)", "Category 3 (basic)" (the word in brackets says where it sits; the number decides the quality limits). The market's own label stays "Market grade" in the price table, because it is a different thing.
- Filter default options read **Status**, **Type**, **Commodity**, **Market** instead of "All statuses" and so on.
- Market prices now also update by themselves: the server fetches when it starts and then every 6 hours (`AGRILEDGER_MARKET_AUTO_HOURS`, 0 turns it off), in addition to the button. A failure keeps the last saved prices. Reason: a Pool Manager who forgets the button would otherwise show stale prices.
- Stage 9 (final review) waits until the user says nothing else needs changing.

## S. Logo and the login and sign-up look (27 Nov, approved by the user)
- Logo: option C, an open ledger with a sprout (green rounded square, paper-coloured drawing), with the wordmark "Agri" in bold and "Ledger" in regular weight. Used in the login and sign-up brand panel, in every top bar, and as the browser-tab icon (`web/img/logo.svg`). A designer can later refine the same idea; only that file and `web/js/logo.js` would change.
- Login and Sign up use a split layout on wide screens: a leaf-green brand panel (logo, headline, one sentence, a faint lattice at 7%) beside the form; on phones the panel is a slim band above the form. The earlier idea of an animated record chain was tried and dropped (it did not look good). No video or photos.
- Headline: "Pooled together. Paid fairly." (replaces "Fair prices. Open records.", which sounded like a shop). The wording can be changed in `web/i18n/en.json` (`login.panel_title`, `login.panel_text`).
