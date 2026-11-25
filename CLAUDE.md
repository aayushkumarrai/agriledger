# AgriLedger — rules for Claude Code

Team 05 mini project: shared-inventory and proportional-payout system. C++17, PostgreSQL, Crow, vanilla JS.
Read `docs/IMPLEMENTATION_SPEC.md` first. It wins over the Master Document where they differ; this file wins over both.

## Never break these
1. **Integers only.** Quantity in grams, money in paise, price in paise per kg. No `float`/`double` in C++; no floating point for money or quantity in JavaScript either (parse text to integers, format with integers or `BigInt`).
2. **Ledgers and audit tables are append-only.** Never write UPDATE, DELETE or TRUNCATE against them, never edit a past row; a correction is a new event. Do not weaken the triggers or the application role's grants.
3. **Every sale is one transaction** that locks the pool row first (see spec §5.1), runs reconciliation INV-1…5 before commit, and rolls back fully on any failure. There is exactly one production sale path (E4 → E3 → E2), and Manager-placed sales use it too. The "unprotected" mode for the concurrency demo exists only inside the test harness (`tests/` and the E3 harness), on its own scratch table in a separate schema; it is never part of the E2 sale function and never compiled into the server.
4. **Never show codes or numbers about errors.** The server sends reason codes; the browser shows plain sentences from `web/i18n/en.json`. No literal UI strings in JS or HTML.
5. **Check roles on the server for every request.** Hiding a button is not security. Buyers never receive farmer names, shares or payouts.
6. **Do not invent features.** Out of scope: real payments, bidding or auctions, mobile app, negotiated prices, SMS or email, and any live price feed that sets or changes a selling price. One exception, approved by the user on 25 Nov: the Pool Manager can press "Update market prices" to save Agmarknet's daily prices (via data.gov.in) as dated reference data, shown beside prices with the source line; they never set a pool's price. Ask instead of guessing; mark anything you assume as PROPOSED in your report.
7. Do not change `db/schema.sql` or the wireframe-defined screens without asking.

## Decided rules (spec §4.5)
- Pool status: no auto-close. An open pool with stock 0 shows a grey "Out of stock" tag; a deposit makes it sellable again; only the Manager closes or reopens a pool; a closed pool refuses deposits and sales.
- Price is entered in whole rupees per quintal only. Quantity: at most 3 decimals in kg or 5 in quintal. Percentages: one decimal, trailing zero trimmed, half-up. `display_unit` is always `'quintal'`; the kg/quintal toggle lives in the browser.
- Names in the README and credits use the spellings in the Master Document.

## Design rules (frontend)
- Follow `docs/wireframes/*.png` and spec §8. Do not add elements, hues, gradients or emoji. Hairline 1 px borders, no shadows, except **one soft shadow on pop-ups only** (decided 20 Nov; it supersedes the earlier "no shadows").
- Visual bar: refined and restrained, never bold or trendy. Self-hosted fonts only. One inline line-icon set with a single stroke width (`web/js/icons.js`). Photo slots have a fixed aspect ratio. Every control has hover, focus, pressed, disabled and (where it applies) loading states; every screen has empty and error states.
- After each screen: screenshot at 1200 px and 390 px, then run the squint test, the greyscale test, the spacing audit against the scale and the number-alignment check (`npm run test:browser`); fix what they find and list the fixes in the report.
- Tokens only (`web/css/tokens.css`): paper #F4EEDF, card #FBF7EC, rule #D8CDB4, ink #2C2418, muted #6B5D45, **leaf green #3B5B2E is the one accent**, clay red #9C3F2A for errors/mismatch only. Fonts: Source Serif 4 (headings), Noto Sans (body), self-hosted.
- Spacing from the scale 4/8/12/16/20/24/32 only. Tap targets ≥ 44 px. Visible focus ring. Tabular numerals, numbers right-aligned in tables.
- One primary button per screen. Priority order: act first (price, stock, quantity, total, button), proof second, history last.
- Colour never carries meaning alone (text + icon). Every screen has loading, empty, error and normal states.
- Words: farmer, deposit, Category 1–3 (shown as "Category"; formerly "Range", then "Grade"; renamed 25 Nov by the user), pool. No explanatory helper text on screens: only labels, values and error or refusal messages. Never label payouts "simulated" on screen.

## How to work
- Work stage by stage (spec §12). Use plan mode for stages 2–7. Wait for the user's approval between stages.
- Before saying a screen is done, open it with Playwright at 1200 px and 390 px and compare it with its wireframe.
- Before saying code is done, run its tests. Keep `db/tests/negative.sh` all PASS and `worked_example.sql` all OK.
- Keep each engine in its own module with a narrow interface (E1 `engines/e1_audit`, E2 `engines/e2_sale`, E3 `engines/e3_queue`, E4 `engines/e4_web`); each owner must be able to explain theirs.
- Commit small, with clear messages. Do not commit `data/photos/` or secrets.
- Report in five lines: done, tests, changed or assumed, needs a decision, next.
