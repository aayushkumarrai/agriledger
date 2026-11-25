You are updating the Sprint 2 student handbook (Chapter 5, Gate 3 / Review 4) for Team 05, Theme 04, AgriLedger. I am attaching the current Word document. It was written early in Sprint 2, when only the database layer existed. Since then all four engines, the web screens, the tests and several new features have been built. Your job is to update the document so that it is true as of 25 November 2026.

HOW TO WORK

1. Use only the facts in this prompt and in the attached document. Do not invent numbers, outputs, commit hashes, names or dates. If something needs a screenshot or a value I have not given you, leave a clear note in square brackets that says what the student must attach or add, for example [attach screenshot of terminal output].
2. Keep the structure exactly: the same chapter heading, section numbers and titles, table columns, Engine IDs and Test IDs. Keep the existing wording wherever it is still true. Change only what is out of date.
3. Language: plain, humanised, short sentences, like a student explaining their own work to a reviewer. No marketing words.
4. SYMBOLS: use only ordinary keyboard characters. Do not use em dashes or en dashes (use a plain hyphen or reword the sentence), arrows of any kind, tick marks, bullet glyphs, emojis, decorative quotation marks, ellipsis characters, or any other special symbol that looks like a machine slip. Keep only the symbols the original document already uses, such as the rupee sign in amounts and the percent sign. In tables write "to" instead of an arrow. Do not leave stray asterisks, hash signs or other markdown marks in the Word text. Write numbers and units the way the original does.
5. Never delete a failed test or a defect. Keep the original wording and add the correction and the retest result.
6. A test marked Pass must never have an empty Actual result. Write what was observed, using the facts below.
7. Replace "Not yet implemented", "Planned", "Pending", "Not yet run", "Contract only; not run" and "to be added" with the real status wherever the facts below support it.
8. Leave Section 16 (the Smart Campus Lab Scheduler example) completely unchanged.
9. The repository link is https://github.com/aayushkumarrai/agriledger . Write it wherever the document has "[repository URL: add]" or "[repository URL]".
10. When you finish, give me two short lists: every place where you left a square bracket note, and every statement you were not fully sure about.

LANGUAGE AND TECHNICAL DETAIL

Write every explanation in natural, readable language, the way a student would explain their own work to a teacher: short sentences, no filler and no showy words. Do not water down the technical content while doing this. Wherever the document needs precision, keep the exact terminology and show the real thing: table and column names, constraint names, function and class names, test names, endpoint paths, status codes, commands, file paths, and short lines or snippets of code or terminal output, copied exactly from the facts above or from the repository and never reworded or invented. Put such text in the same style the original document uses for code (for example the look of the existing test log), and put it on its own line when it is longer than a few words. A good pattern is one plain sentence that says what happened and why it matters, followed by the exact name, command or line as evidence. Use technical terms when they are the correct word (row lock, append-only trigger, hash chain, largest remainder, reconciliation, transaction rollback, FIFO queue, CHECK constraint) and explain a term in plain words only the first time it appears, if a reader outside the team might not know it. Never simplify numbers, units or identifiers into loose descriptions. If a real code line is long, quote only the relevant part and name the file it comes from. Descriptive cells, defect stories, notes and paragraphs should read naturally; tables of facts, IDs, commands and results should stay exact and compact.

PART 1. THE FACTS (the only source of truth)

What exists on 25 November 2026
All four engines are implemented, runnable and tested. About 3,700 lines of C++17 in the engine folders, plain HTML, CSS and JavaScript for the screens, PostgreSQL 16 in Docker, and the code is on GitHub. New since the first draft of the handbook: farmer and buyer sign-up with approval by the Pool Manager and an identity record; a People page; a Market field on pools; daily market prices from Agmarknet (through the free data.gov.in service) with a variety list; the pool quality band renamed to Category 1 to 3; the farmer statement laid out as a bill with buyer, receipt, record seal and print; price history; a logo and a new login and sign-up look; summary strips, an account menu, a phone tab bar and a footer; and real lot photographs in the demo data.

Words that changed on screen (use these words in the document where it names screen items)
The pool quality band is now called Category (Category 1 best, Category 2 standard, Category 3 basic). The quality limits panel is Category based. "Reconciled" is now "Records verified" and "Totals do not match" is now "Records need checking". "Verify integrity" is now "Check records". "The three ledgers" is now "Pool history" with the tabs Stock movements, Farmer shares and Payments. The e-NAM name for the same bands is Range I to III; where the document speaks about the e-NAM source it may keep the word Range, and Section 2 should say once that the app shows e-NAM Range I, II and III as Category 1, 2 and 3.

Commits (short hash, date, what it contains)
0570450, 19 Nov 2026: initial commit with the specification, schema, database test scripts and wireframes.
b57a9eb, 19 Nov: Stage 0 answers applied to the spec, project rules and decision log.
b685b54, 19 Nov: repository skeleton, PostgreSQL container, CMake targets and one-command tests.
5ab62e1, 19 Nov: E1 allocation, canonical hashing, chain verification and invariants.
00be38e, 19 Nov: E2 atomic operations on libpqxx.
7fc514f, 19 Nov: E3 per-pool FIFO queue, retry, BUSY and the concurrency harness.
d51df22, 19 Nov: E4 server (Crow), sessions, role checks, photo upload and live feed.
b7c68c2, 20 Nov: frontend foundation and static file serving.
bdd26ad, 20 Nov: browser audits run as an Android phone profile.
3e2ce26, 20 Nov: login and first-login password screens.
fd069a1, 20 Nov: buyer screens and buy flow.
45368d2, 20 Nov: farmer screens.
683b631, 20 Nov: Pool Manager screens.
8ffcd8c, 20 Nov: demo data through the real server, end-to-end browser test, demo script.
8dea02e, 21 Nov: wording changes and removal of helper text.
d5abaf6, 21 Nov: farmer statement as a bill and price history.
60cbad6, 21 Nov: sign-up, approval, identity record, people tools and pool market (schema, E2, E4, tests).
f3e3600, 21 Nov: sign-up and People screens, demo data.
3df13b8, 21 Nov: research notes on quality limits and identity documents (not verified, nothing loaded).
3dbf5a8, 21 Nov: visual polish round.
66794e2, 22 Nov: first version of the handbook update prompt.
97975a3, 25 Nov: market prices, backend (schema, E2 import and summary, E4 fetch and routes, tests).
1f3f9b5, 25 Nov: market prices on the screens, variety list, Update market prices button, demo data from market prices.
f60d8fb, 25 Nov: Category wording, filter labels, automatic price updates while the server runs.
b7c752e, 25 Nov: logo and the split login and sign-up look.
5223ee7, 25 Nov: login headline.
66ce956, 25 Nov: summary strips, status lines and the account menu.
fc2fed7, 25 Nov: real demo lot photographs with credits.
1ac6224, 25 Nov: phone tab bar and footer.
78a3b22, 25 Nov: date fixes in the project rules and decision log (latest commit).

Test totals today
Database: worked_example.sql reports INV-1 to INV-5 all OK (ownership 30 / 40 / 30 percent, remaining 450 / 600 / 450 kg). negative.sh has 47 checks and all 47 PASS (the earlier handbook said 34; 8 were added for the frontend rules and the new account rules, and 5 for the market price rules).
C++: 107 tests pass, 0 fail. By area: E1 unit tests 27 (allocation 9, chain 8, hashing 4, invariants 6), E3 queue tests 10, E4 integration tests 36, E2 integration tests 26, concurrency tests 7, static checks 1.
JavaScript: 40 tests pass.
Browser: screen checks for every role plus login, sign-up and the People page, at 1200 px and at 390 px (Android phone profile) all pass, and the end-to-end story passes at both sizes.
Static checks: no float or double in the engines, no unprotected mode in the engines, no literal UI strings in the web code. All pass.

How to run things (use these in Section 8)
Database tests: scripts/test.sh db
All C++ tests: scripts/test.sh cpp (prints "100% tests passed, 0 tests failed out of 107")
One engine only: ctest -R E1 (E1 tests), ctest -R E2Test, ctest -R "E3Queue|ConcurrencyTest", ctest -R E4Test, run from the build folder with the test database variables that scripts/test.sh sets
JavaScript tests: node --test tests/js/
Screens: scripts/screens_test.sh all
End to end: scripts/e2e_test.sh
Run the site: scripts/seed_demo.sh once, then scripts/run_server.sh, then open http://localhost:8080 (demo password for every account is in the README)

Real concurrency result (from ConcurrencyTest)
Protected path (E3 queue plus E2 row lock): 50 buyers each asking for 1 quintal from a pool with 10 quintal, repeated for 20 runs. In every run exactly 10 requests were accepted and 40 rejected, accepted plus rejected equalled 50, and stock never went below zero. Oversold in 0 of 20 runs.
Unprotected comparison (test harness only, its own scratch table): oversold in 20 of 20 runs, worst case 40 quintal sold beyond the stock.

Real API samples (captured from the running server during development)
Login success: a request with username and password gives 200 with role, must_change_password, name and approval, for example role buyer, name Mahantesh Traders, approval APPROVED.
Login wrong password: 401 with error INVALID_CREDENTIALS (one generic answer).
Sale accepted: POST /pools/13/sales with request_id, quantity_g 100000, confirmed_price_paise_per_kg 6783 gave 200 with status ACCEPTED, sale_id 2, receipt_no R-0002, gross_paise 678300, quantity_g 100000, unit_price_paise_per_kg 6783, stock_after_g 2487500, duplicate false.
Same request_id sent again: 200 with the same sale_id, receipt_no and amounts and duplicate true (nothing sold twice).
Price changed (confirmed price 1 against current 6783): 409 with status REJECTED, reason PRICE_CHANGED, current_price_paise_per_kg 6783.
Too much (900000000 g requested): 409 with status REJECTED, reason INSUFFICIENT_STOCK, stock_g 2487500.
Buyer calling a Pool Manager endpoint (POST /pools/13/integrity-check): 403 with error FORBIDDEN.
Negative quantity: 400 with error INVALID_FIELD, nothing written.
Pool Manager integrity check: 200 with ok true, checked_entries 5 and a head_hash (the newest record's hash, shown on screen as a short seal).
Market prices read: GET /market-prices?commodity_id=1&variety=Sona%20Mahsuri on 25 Nov 2026 gave as_of 2026-11-25, source "Agmarknet via data.gov.in", a commodity summary for all paddy varieties (21 quotes, median of the most common prices 3481 rupees per quintal) and a variety summary for Sona Mahsuri (1 quote from 1 market, median 3900, lowest 3720, highest 3900), plus the list of quotes with market, variety, market grade, lowest, highest and most common price. Prices are whole rupees per quintal, stored as the same number of paise per kg.
Market prices update: POST /market-prices/refresh as the Pool Manager answers 200 with the number of quotes saved; a Farmer or Buyer gets 403; no key set up gives 503 PRICE_FEED_NOT_SET_UP; a second press within a minute gives 429 TOO_SOON; a failed fetch gives 502 PRICE_FEED_UNAVAILABLE and the last saved prices stay.
Sign-up: POST /auth/signup creates a pending Farmer or Buyer account and signs the person in; a Pool Manager cannot be created this way (400); a repeated mobile number gives 409 DUPLICATE_PHONE; more than 10 attempts an hour from one address gives 429.
For the live feed message shape and the internal calls between E3, E2 and E1, quote the contracts in docs/IMPLEMENTATION_SPEC.md sections 6 and 7 and name the test that exercises them; do not invent sample output for those.

Where the market prices come from
The free data.gov.in dataset "Current Daily Price of Various Commodities from Various Markets (Mandi)", which publishes Agmarknet's daily file. The server reads it with the team's own API key from the .env file (never committed) using the curl program, with the key passed on standard input so it does not appear in the process list. The Pool Manager presses Update market prices, and the server also fetches by itself when it starts and every 6 hours while it runs. Tests use a saved sample file and never use the internet. This was a change of scope approved on 25 Nov (decision log section Q).

Mapping of every test ID in the document to a real test (use these names in Actual result and in the evidence columns)
TC-E2-14 sale with a failure injected after each step: E2Test.FailureAfterAnyStepRollsBackEverything. Pass: every step failure rolled back fully, stock and balances unchanged.
TC-E2-15 each reason code: E2Test.InsufficientStockReturnsRemainingStock, OutOfStockWhenNothingIsLeft_PoolStaysOpen, PriceChangedReturnsCurrentPrice, BelowMinOrderUnlessItTakesTheWholeRemainder, PoolNotOpenForDraftAndClosed, and at HTTP level E4Test.SaleReasonCodesAndPlainBodies. Pass.
TC-E2-16 corrupt one balance then sell: E2Test.CorruptedBalanceFailsReconciliationAndRollsBack. Pass.
TC-E2-17 new database rules (add this row): the 13 checks added to db/tests/negative.sh for account fields (mobile number format, approval status, identity document list, verified-by pairing, duplicate mobile number, generated public ID) and for market price rules (maximum below minimum, zero price, duplicate quote, unknown commodity, delete refused). Pass: all rejected, 47 of 47 checks PASS.
TC-E1-01 worked allocation: E1Allocation.WorkedExample. Pass.
TC-E1-02 rounding example (100 g over three equal balances): E1Allocation.RoundingExample and TiesGoToLowestIdWhateverTheInputOrder. Pass.
TC-E1-03 very large values: E1Allocation.HugeValuesUseWideIntermediates. Pass.
TC-E1-04 intact chain verifies: E1Chain.IntactChainVerifies. Pass.
TC-E1-05 altered ledger row then verify: E1Chain.AlteredLedgerRowIsNamedExactly (also E2Test.TamperedLedgerRowIsNamedAtTheExactEntry). Pass.
TC-E1-06 zero quantity or no balances: E1Allocation.RejectsInvalidInput. Pass.
TC-E1-07 farmer statement for the worked example: E4Test.ViewsMatchTheWorkedExample (deposit 600 kg and remaining 450 kg for the first farmer) and the browser statement checks. Pass.
TC-E3-01 stock 10 kg, two buyers 8 kg each: ConcurrencyTest.Scenario_TenKgTwoBuyersEightKgEach. Pass.
TC-E3-02 stock 20 kg, two buyers 8 kg each: ConcurrencyTest.Scenario_TwentyKgTwoBuyersEightKgEach. Pass.
TC-E3-03 failure in buyer one's transaction: ConcurrencyTest.Scenario_FailureInBuyerOnesTransaction_BuyerTwoSeesUnchangedStock. Pass.
TC-E3-04 50 buyers, 20 runs: ConcurrencyTest.Protected_FiftyBuyersTwentyRuns_ZeroOversold and E3Queue.FakeSaleStress_50BuyersTwentyRuns_ZeroOversold. Pass with the real numbers above.
TC-E3-05 lock timeout injected: ConcurrencyTest.StalledPoolLock_RetriesThenBusy_ThenRecovers and E3Queue.RetriesStopAfterThreeAttemptsAndReportBusy. Pass: retried up to 3 times, then BUSY, and recovered.
TC-E3-06 unprotected comparison: ConcurrencyTest.Unprotected_HarnessOnlyCounter_Oversells. Pass: oversold in 20 of 20 runs.
TC-E4-01 valid login: E4Test.LoginSessionCookieAndForcedPasswordChange. Pass: 200 with role and session cookie.
TC-E4-02 wrong password: the live sample (401 INVALID_CREDENTIALS) and E4Test.LoginIsThrottledAfterRepeatedFailures. Pass.
TC-E4-03 role matrix: E4Test.RoleMatrixOverEveryEndpoint. Pass: wrong role gets 403 on every endpoint, no state change.
TC-E4-04 malformed sale body: E4Test.SaleInputValidation and the live sample (400 INVALID_FIELD). Pass.
TC-E4-05 same request_id twice: E4Test.DuplicateRequestIdOverHttp and the live sample (duplicate true). Pass.
TC-E4-06 two open dashboards, one sale: E4Test.LiveFeedIsFilteredByRoleAndNeverReportsARolledBackSale and the browser check that a number changes without a reload. Pass for delivery without a reload. State honestly that the two second target was not timed separately.
TC-E4-07 buyer sees no farmer names, shares or payouts: E4Test.BuyersNeverSeeFarmerNamesSharesOrPayouts. Pass.
TC-E4-08 forced server error shows plain reason only: E4Test.SaleReasonCodesAndPlainBodies and the browser tests that force a server error and show a plain sentence with no code. Pass.
Add these rows after TC-E4-08 (features built since the first draft), each Pass:
TC-E4-09 sign-up creates a pending account and approval unlocks deposits and purchases: E4Test.SignUpMakesAPendingAccount_ApprovalUnlocksDepositsAndPurchases.
TC-E4-10 sign-up refuses a Pool Manager role, a wrong document type, a short password, a bad mobile number and a duplicate mobile number: E4Test.SignUpRefusesBadInput_NoSelfMadeManagers_NoDuplicateMobileNumbers.
TC-E4-11 sign-up is limited per address: E4Test.SignUpIsThrottledPerAddress.
TC-E4-12 people tools are Pool Manager only and deactivation keeps history: E4Test.PeopleToolsAreForThePoolManagerOnly_DeactivateKeepsHistory.
TC-E4-13 check records names the exact tampered record: E4Test.IntegrityCheckNamesTheTamperedEntry and the end-to-end tamper test.
TC-E4-14 a pool's market is stored and shown: E4Test.PoolMarketIsStoredAndShown.
TC-E4-15 market prices are updated by the Pool Manager only and summarised with whole numbers: E4Test.MarketPricesAreUpdatedByTheManagerOnly_AndSummarisedWithIntegers.
TC-E4-16 a second update replaces the day's quotes and a failed update keeps the last ones: E4Test.ASecondUpdateReplacesTheDaysQuotes_AFailedUpdateKeepsTheLastOnes.
TC-E4-17 no key means "not set up", and updates are rate limited: E4Test.MarketPriceUpdatesAreNotSetUpWithoutAKey_AndAreRateLimited.
TC-E4-18 prices update by themselves when switched on: E4Test.MarketPricesUpdateByThemselvesWhenSwitchedOn.
TC-E4-19 every screen at 1200 px and 390 px, in all states, with contrast, spacing, text wrapping and number alignment checks: scripts/screens_test.sh all.
TC-E4-20 end-to-end story (Pool Manager creates a pool with a photo, two buyers race for the last stock and exactly one wins, price change during confirmation, double tap sends one request, tamper then Check records names the record), at both screen sizes: scripts/e2e_test.sh.
Rows TC-E2-01 to TC-E2-13 stay as they are, except that where they quote negative.sh as 34 checks, write that it now has 47 checks and all pass.

Defects that really happened (for Section 11). Keep DEF-01 and DEF-02 as written; update DEF-02's retest to: re-checked at TC-E3-06, the unprotected mode exists only in the test harness on its own scratch table, ConcurrencyTest.Unprotected_HarnessOnlyCounter_Oversells passes, and the static check confirms no unprotected code in the engines. For DEF-01 the fix commit is the commit that first added db/tests/negative.sh to the repository (0570450); mark it with a square bracket note asking the student to confirm. Then add these rows in order:
DEF-03, E4 static files. Problem: pages loaded over one kept-alive connection received an extra 404 because the static file handler ended the response twice. Correction: the handler now ends the response once. Retest: E4Test.ManyRequestsOnOneConnectionAllGetTheirOwnAnswer passes. Fix commit b7c68c2.
DEF-04, frontend quantity parser. Problem: typing 2,5 was read as 25 (a comma treated as a thousands separator). Correction: strict thousands-comma rules, and a comma used as a decimal is refused with a plain message. Retest: the format tests in tests/js pass. Fix commit b7c68c2.
DEF-05, login screen. Problem: the login screen showed the word null because an empty value was appended to the page. Correction: a null-safe page builder, and an audit that fails any page showing null, undefined or NaN text. Retest: screen checks pass. Fix commit 3e2ce26.
DEF-06, buyer pool limits. Problem: quality limits were listed alphabetically instead of in the reference table order. Correction: ordered by table order. Retest: buyer screen checks pass. Fix commit fd069a1.
DEF-07, Pool Manager forms. Problem: when a live update redrew the page while a request was in flight, the success or error message was written to the old form and the screen kept the previous message. Correction: the page now tracks the current message element. Retest: the screen check passed three times in a row. Fix commit 683b631.
DEF-08, sign-up duplicate number. Problem: a repeated mobile number was reported as a repeated username because the number is also the sign-in name. Correction: explicit checks that name which of the two is taken. Retest: TC-E4-10 passes. Fix commit 60cbad6.
DEF-09, people tools. Problem: the Pool Manager could set one person's mobile number to a value another person already used as a sign-in name. Correction: the edit is refused with the duplicate message. Retest: TC-E4-12 passes. Fix commit f3e3600.
DEF-10, sign-up page. Problem: the sign-up page did not load because of a typing mistake in its script path. Correction: path fixed. Retest: the screen check for sign-up passes at both sizes. Fix commit f3e3600.
DEF-11, market prices service. Problem: the first requests to data.gov.in from a script failed with an HTTP error, because the service refuses the default web client of the scripting language. Correction: the server uses the curl program with a plain user agent. Retest: a live request returned 200 with real quotes, and TC-E4-15 to TC-E4-18 pass. Fix commit 97975a3.
DEF-12, market prices table. Problem: the table of quotes wrapped short names across lines when it sat in a narrow column, which the text wrapping audit caught. Correction: the card now spans the full width. Retest: screen checks pass at both sizes. Fix commit 1f3f9b5.
DEF-13, category choice on a phone. Problem: the longer labels such as Category 1 (best) wrapped inside the three-way choice on a 390 px screen. Correction: shorter option labels inside the choice (1 (best), 2 (standard), 3 (basic)). Retest: screen checks pass. Fix commit 1f3f9b5.
DEF-14, login headline. Problem: the first way of centring the headline used automatic margins that the spacing audit rejected as not on the spacing scale. Correction: centred with flexible layout instead. Retest: login and sign-up screen checks pass. Fix commit 5223ee7.
DEF-15, phone tab bar. Problem: the first version used a negative margin and a bottom padding of 72 px that the spacing audit rejected. Correction: removed the offset and used a spacer element. Retest: screen checks pass at both sizes. Fix commit 1ac6224.
DEF-16, a test timing problem. Problem: the screen test sometimes counted a deliberately refused request (429, too soon) as an error because the browser logged it a moment after the page showed its message. Correction: the test waits briefly before it stops allowing that status. Retest: three runs in a row passed. Fix commit 66ce956.
Replace the last row ("No other defects recorded yet") with: no other defects recorded; the register is complete up to 25 Nov 2026.

PART 2. WHAT TO CHANGE, SECTION BY SECTION

Section 5.1 Team and Engine Register. Keep everything. Change the Date to 25 November 2026 and write the repository link in the Repository URL row.

Part B, Section 1 Sprint 2 Plan and Backlog. Change every Status to Done, and in the Done when column keep the original wording but add one short sentence of evidence each, using the facts above:
BL-01 allocation and rounding: E1Allocation.WorkedExample and RoundingExample pass.
BL-02 hash chain: the E1Chain tests pass.
BL-03 farmer statement from ledger rows: E4Test.ViewsMatchTheWorkedExample passes, and the statement is laid out as a bill with buyer, receipt and record seal.
BL-04 schema v1.1: worked_example.sql INV-1 to INV-5 OK and negative.sh 47 PASS (the original text says 34; update).
BL-05 C++ operations: the E2 integration tests pass (26).
BL-06 atomic sale: a sale of 500 kg gives 150 / 200 / 150 kg and 375,000 / 500,000 / 375,000 paise; E2Test.WorkedExample and FailureAfterAnyStepRollsBackEverything pass.
BL-07 per-pool FIFO queue: the E3Queue tests pass (10).
BL-08 concurrency harness: protected path oversold 0 of 20 runs, unprotected oversold 20 of 20 runs.
BL-09 login and roles: E4Test.RoleMatrixOverEveryEndpoint and the login tests pass.
BL-10 REST endpoints and validation: E4Test.SaleInputValidation and the other E4 tests pass.
BL-11 notify and live feed: E4Test.LiveFeedIsFilteredByRoleAndNeverReportsARolledBackSale passes.
BL-12 dashboard screens: the screens for all three roles plus login, sign-up and the People page are built and checked at 1200 px and 390 px, and the buy flow works end to end. Rewrite its Done when text: remove "may extend into Sprint 3" and say the screens follow the wireframes, with the changes agreed between 20 and 25 Nov 2026 recorded in docs/DECISION_LOG.md (Category wording, no helper text, sign-up and People screens, Check records, summary strips, phone tab bar).
After BL-12 add three rows for work done beyond the original backlog, each with Status Done. Do not invent new BL numbers: in the Task / FR ID column of each of these rows write exactly "Added after design". The three rows are: sign-up, approval and identity record (engines E2 and E4, owner Shubhang and Jairaj, evidence TC-E4-09 to TC-E4-12); market prices from Agmarknet (engines E2 and E4, evidence TC-E4-15 to TC-E4-18, owner left for the student to fill in [student to add owner]); and the phone tab bar, footer, logo and login look (E4 web screens, evidence TC-E4-19, owner left for the student to fill in [student to add owner]).
The closing line "Evidence filename / repository link" should name the repository folders (engines, web, tests, db/tests, scripts), the docs/IMPLEMENTATION_SPEC.md section 12 stage list, and the repository link.

Section 2 Sprint 1 Design Handoff. Keep the existing text. Update only the last column, Any approved change, and the E3 dependency cell:
E1: the proposed hash format for events without ledger rows and the fixed UTC timestamp format were implemented as proposed; still waiting for the E1 and E2 owners' formal review.
E2: schema v1.1 plus changes agreed between 21 and 25 Nov 2026: new account fields (mobile number, generated public ID, approval status, identity document type and last four characters, who verified and when), a market field on pools, and two tables for daily market prices and the variety list. The commodity "Tur dal" was renamed "Tur (Arhar)". Recorded in docs/DECISION_LOG.md sections N, O and Q. Guide confirmation still pending. Add one sentence: the app shows e-NAM Range I, II and III as Category 1, 2 and 3.
E3: implemented as designed; the unprotected comparison mode exists only in the test harness on a scratch table. Change the dependency cell "The E2 sale operation (stub until ready)" to: the real E2 sale operation, used directly with no stub.
E4: new endpoints for sign-up, approval and people tools, a who-am-I endpoint, the integrity check returning the newest record's hash, and market prices (read for every signed-in role, update for the Pool Manager). Self sign-up reverses the earlier decision D-16 (no self-registration); Pool Manager accounts are still created only on the server. A live price source was out of scope in the master document; it was approved on 25 Nov 2026 as dated reference data only, never setting a pool's price.
End the section with this evidence line, replacing the existing one: Evidence filename / repository link: docs/IMPLEMENTATION_SPEC.md, docs/AgriLedger_Master_Document.md, db/schema.sql and docs/DECISION_LOG.md; Gate 2 pack: to attach.

Section 3 Engine-wise Implementation Register. Keep the existing E2 rows, but update the last column (Pending) of each. Schema v1.1 row: keep "diagrams to be redrawn", replace "Apply to the team database" with a statement that it is applied to the project database by Docker Compose and that the schema was extended between 21 and 25 Nov 2026. Reference data seed row: keep "other crops' limits to be verified and filled" because it is still true, and add that research notes are in docs/research and nothing unverified was loaded. Database test scripts row: change the behaviour text to the worked example plus 47 negative checks, and the Pending text to none. Then replace every "Not yet implemented" row with a real row, and add the extra rows after them:
E1 allocation and hashing: engines/e1_audit, largest-remainder allocation, SHA-256 canonical hashing, chain verification, invariants INV-1 to INV-5; commit 5ab62e1; 27 unit tests pass. Pending: formal review of the hash format by the owners.
E2 C++ operations: engines/e2_sale, create pool, deposit, photo, price and status change, atomic sale with reconciliation and NOTIFY, accounts, approval, market price import and the views; commits 00be38e, 60cbad6 and 97975a3; 26 integration tests pass. Pending: none for Gate 3.
E3 queue: engines/e3_queue, per-pool FIFO lanes, three attempts then BUSY, concurrency harness; commit 7fc514f; 10 unit tests and 7 concurrency tests pass.
E4 server: engines/e4_web, login, sessions, role checks, REST, photo upload, WebSocket live feed, sign-up, people tools and market price fetching; commits d51df22, 60cbad6 and 97975a3; 36 integration tests pass.
Web screens: web folder, Login, Sign up, Buyer, Farmer and Pool Manager screens, summary strips, account menu, phone tab bar, footer and logo; commits b7c68c2 to 683b631, f3e3600 and 1f3f9b5 to 1ac6224; 40 JavaScript tests and the browser checks pass.
Demo data and end-to-end test: commit 8ffcd8c, extended in 1f3f9b5 and fc2fed7 (real lot photographs, credits in scripts/demo-photos/CREDITS.md).
Replace "[add hash]" in the E2 database rows with 0570450 (schema, seed and test scripts) and keep the "Pending" text only where it is still true.
Replace the closing evidence line with: Evidence filename / repository link: the repository folders named in the rows above and docs/DECISION_LOG.md.

Section 4 Algorithm and Data Structure Implementation. Keep the expected values. Fill the observed parts:
E1 largest remainder: observed by E1Allocation.WorkedExample (150000 / 200000 / 150000 g and 375000 / 500000 / 375000 paise); edge case tie goes to the lowest seller id, E1Allocation.TiesGoToLowestIdWhateverTheInputOrder; rounding case 100 g gives 34 / 33 / 33 and 250 paise gross, E1Allocation.RoundingExample.
E1 hash chain: observed by the E1Chain tests; an altered row is named at its exact sequence number; gaps are detected.
E2 row lock: observed by E2Test.RowLockPreventsOversellWithoutAnyQueue and the worked example reconciliation; duplicate request_id refused by the unique key, E2Test.DuplicateRequestIdReturnsTheOriginalResultAndNeverSellsTwice.
E3 queue: observed by ConcurrencyTest.Scenario_TenKgTwoBuyersEightKgEach (one accepted, one rejected) and E3Queue.RetriesStopAfterThreeAttemptsAndReportBusy.
E4 session token and password hashing: a 256-bit random session token in an HttpOnly cookie that lasts 8 hours, argon2id through libsodium; a wrong password returns one generic 401; edge case an expired or missing session gets 401 (E4Test.LogoutEndsTheSession_ExpiredSessionsAreRefused) and a wrong role gets 403 (E4Test.RoleMatrixOverEveryEndpoint). The password hashing row continues on the next page of the document; fill it too: the password is never returned or logged, and repeated wrong passwords are throttled (E4Test.LoginIsThrottledAfterRepeatedFailures).
Replace the paragraph that says E1 is not implemented with: E1 is implemented; the unit test E1Allocation.WorkedExample asserts the values above and passes [attach screenshot of the test output]. Keep the expected values quoted in that paragraph. Replace its closing evidence line with: Evidence filename / repository link: tests/unit/e1_allocation_test.cpp, tests/unit/e1_hashing_test.cpp, docs/AgriLedger_Master_Document.md section 12; execution trace: [attach screenshot of ctest -R E1 output].

Section 5 API / Interface and Data Exchange. In the Real / mock column write Real for every row. Fill Actual response from the real samples above:
Dashboard to E4, POST /auth/login: the login success and wrong password samples.
E4 to E3, submit sale request: the accepted sample, the price changed and insufficient stock samples. Keep the document's example and add the real ones beneath it.
E3 to E2, execute_sale: real internal call; result codes tested by TC-E2-15; no stub is used any more.
E2 to E1, allocate and build audit entry: real internal call; tested by TC-E1-01 and the E2 worked example.
E2 to E4, NOTIFY then WebSocket: real; tested by E4Test.LiveFeedIsFilteredByRoleAndNeverReportsARolledBackSale; quote the message shape from the spec, do not invent output.
Add two rows: Dashboard to E4, market prices (GET /market-prices and POST /market-prices/refresh), Real, with the market prices read and update samples above; and Dashboard to E4, sign-up (POST /auth/signup), Real, with the sign-up facts above.
Replace the closing evidence line with: Evidence filename / repository link: docs/IMPLEMENTATION_SPEC.md sections 6 and 7 (contracts), tests/integration/e4_test.cpp; the samples were captured from the running server.

Section 6 Database, State and Transaction Implementation. Keep all rows and their observed text. In the Proof column of the first six rows add that the same behaviour is now also checked through the C++ operations: E2Test.WorkedExample, E2Test.StatusRules_PhotoRequiredClosedRefusesDepositsReopenNeedsPhoto, E2Test.PoolNotOpenForDraftAndClosed, E2Test.BelowMinOrderUnlessItTakesTheWholeRemainder, E2Test.DuplicateRequestIdReturnsTheOriginalResultAndNeverSellsTwice and E2Test.TamperedLedgerRowIsNamedAtTheExactEntry. Change the last row (sale transaction with an injected failure) to Actual observed: full rollback, stock and balances unchanged, E2Test.FailureAfterAnyStepRollsBackEverything passes; Proof: that test and TC-E2-14. Where text says 34 negative checks, write 47. Add one row: E2, import of market prices, expected change: quotes stored once per market, variety, grade and day, a repeat updates the same quote; actual: E4Test.ASecondUpdateReplacesTheDaysQuotes_AFailedUpdateKeepsTheLastOnes passes; proof: that test and TC-E4-16.

Section 7 Concurrency, Security and Data Movement. Keep each row's Implementation text. Replace each "Planned" proof with real proof:
Simultaneous updates: TC-E3-01 to TC-E3-04 and TC-E3-06 pass; 50 buyers by 20 runs, protected path oversold 0 of 20, unprotected oversold 20 of 20.
Transaction rollback: TC-E2-14 and TC-E2-16 pass.
Role-based access: TC-E2-04, TC-E2-13 and TC-E4-03 pass; the new account approval rules are TC-E4-09 and TC-E4-12.
Malformed input: TC-E2-02, TC-E2-03, TC-E2-05, TC-E2-17 and TC-E4-04 pass.
Safe error handling: TC-E4-08 passes.
Duplicate request: TC-E2-10 and TC-E4-05 pass.
Tamper evidence: TC-E2-04, TC-E2-11, TC-E1-04, TC-E1-05 pass, and the end-to-end test tampers with a stored row and the screen names the record (TC-E4-20).
Passwords and sessions: TC-E4-01 and TC-E4-02 pass; login is also throttled after repeated failures and per address (E4Test.LoginIsThrottledAfterRepeatedFailures and LoginIsAlsoThrottledPerAddressAcrossManyUsernames), and sign-up is limited per address (TC-E4-11).
Zero-copy: keep the justification unchanged.
Add one row, Outside service (market prices), engine E4: the key lives only in the .env file, the fetch runs curl with the key on standard input, answers are read as integers only, a failed or slow service changes nothing and the last saved prices stay, and updates are limited to one a minute; proof TC-E4-15 to TC-E4-18.
Update the closing evidence line to mention the tests folder and scripts/test.sh.

Section 8 Build, Run and Git Evidence. Keep the E2 database row, changing 34 PASS to 47 PASS and its commit to 0570450. Replace the other rows:
E1: run ctest -R E1 after scripts/test.sh cpp; observed 27 tests pass; commit 5ab62e1.
E2 C++ operations: ctest -R E2Test; 26 tests pass; commits 00be38e, 60cbad6 and 97975a3.
E3: ctest -R "E3Queue|ConcurrencyTest"; 17 tests pass (10 plus 7); commit 7fc514f.
E4: scripts/run_server.sh then open http://localhost:8080, plus ctest -R E4Test; 36 tests pass; commits d51df22 and later.
Add a last row for the whole project: scripts/test.sh cpp prints "100% tests passed, 0 tests failed out of 107"; scripts/screens_test.sh all and scripts/e2e_test.sh pass; latest commit 78a3b22; repository link.
Replace the closing evidence line with: Evidence filename / repository link: db/tests/setup_test_db.sh, scripts/test.sh, scripts/run_server.sh, scripts/seed_demo.sh and README.md (run steps for every engine); the repository link.

Section 9 Testing, Mandatory Test Case Register. Fill every Actual result and set every Pass / Fail using the mapping in Part 1. Change every Pending to Pass. Add the new rows TC-E2-17 and TC-E4-09 to TC-E4-20. Leave the template paragraph that starts "Example: TC-E1-03 | E1 scheduler" exactly as it is; it is a guide, not an entry. Leave the header repeated at the top of each page. Replace the closing evidence line with: Evidence filename / repository link: db/tests/negative.sh and db/tests/worked_example.sql (TC-E2-01 to TC-E2-13 and TC-E2-17); tests/unit, tests/integration, tests/concurrency and tests/browser (all other tests); command scripts/test.sh. In the FR ID / test type column of the new test rows, do not invent requirement labels. For the rows that have no approved requirement (TC-E2-17, TC-E4-09 to TC-E4-12 and TC-E4-15 to TC-E4-19) write "Added after design" followed by the test type. Where an approved requirement truly applies, use it: TC-E4-13 uses NFR-03, TC-E4-14 uses FR-01, and TC-E4-20 uses FR-01 to FR-10.

Section 10 Testing, Test Execution Evidence. Keep the TC-E2-01 row and the pasted test log; change "34 checks" to "47 checks" and add one line saying the earlier log shows the first 34 checks and the later ones are in the same script. Replace the "Not yet run" row with one row per engine group: E1 tests, E2 C++ tests, E3 and concurrency tests, E4 tests, JavaScript tests, screen checks, end-to-end test. For each, How test was run is the command from Part 1, Evidence is [attach screenshot of terminal output], and Date and commit is 25 Nov 2026 with the latest commit 78a3b22. Keep the TEST LOG (terminal output) block unchanged, and add the real concurrency summary line as text evidence: "unprotected counter oversold in 20 of 20 runs (worst: 40 quintal sold beyond the stock); the protected path oversold in 0 of 20". Replace the closing evidence line with: Evidence filename / repository link: terminal output pasted in this section; [attach screenshots of each command listed above].

Section 11 Defect, Correction and Retest Register. Apply the defect list in Part 1 and replace the closing evidence line with: Evidence filename / repository link: defect rows above; fix commits as listed; the retest tests are named in each row.

Section 12 Engine Readiness and Remaining Work. Rewrite the rows:
E1 works now: allocation, hashing, chain verification, invariants, 27 tests pass. Mocked or incomplete: none. Not yet tested: nothing known. Sprint 3 dependency: formal review of the hash format by the owners.
E2 works now: schema, operations, atomic sale with reconciliation, notify, accounts and approval, market price import and the views, 26 integration tests and 47 database checks pass. Mocked or incomplete: none. Not yet tested: nothing known. Sprint 3 dependency: guide confirmation of the new fields and tables.
E3 works now: queue, workers, retry, BUSY, concurrency harness, 17 tests pass. Mocked or incomplete: none. Not yet tested: a separate response time target. Sprint 3 dependency: none.
E4 works now: server, sign-up, login, role checks, REST, live feed, photo upload, market prices, dashboard screens for all roles, 36 integration tests and browser checks pass. Mocked or incomplete: Google sign-in and mobile OTP are not built (out of scope: no outside sign-in service and no SMS). Not yet tested: only the Android Chrome profile (Chromium) has been tested; iPhone specific checks are deferred; the market price feed was checked against the live service by hand and by saved samples in tests. Sprint 3 dependency: final review, and the guide's confirmation of the market price scope change.
Keep the closing line "Evidence filename / repository link: This document; repository: ..." and add the repository link and: tests and scripts as listed in Sections 8 and 9.

Section 13 Requirements, Code and Partial Test Mapping. Keep the FR and NFR list. For each row write the real code reference, the test IDs, Result and Gap or action:
FR-01 create pool: E2 create pool operation; TC-E2-06, E4Test.PoolCreationAndStatusValidation, TC-E4-14; Pass; none.
FR-02 deposit: E2 record contribution; TC-E2-01, TC-E2-03; Pass; none.
FR-03 allocation and ledgers: E1 allocation and E2 ownership ledger; TC-E1-01, TC-E2-01; Pass; none.
FR-04 sale transaction: E2 sale; TC-E2-07, TC-E2-10, TC-E2-14; Pass; none.
FR-05 proportional payouts: E1; TC-E1-01, TC-E1-02; Pass; none.
FR-06 concurrent sales: E2 row lock and E3 queue; TC-E2-02, TC-E3-01 to TC-E3-04; Pass; none.
FR-07 farmer statement: E2 statement view and E4 route; TC-E1-07; Pass; none.
FR-08 login and roles: E4 and the database role; TC-E2-04, TC-E2-13, TC-E4-01 to TC-E4-03; Pass; none.
FR-09 reconciliation: E2 reconciliation inside the transaction; TC-E2-01, TC-E2-16; Pass; none.
FR-10 live feed: E2 NOTIFY and E4 WebSocket; TC-E4-06; Pass for delivery without reload; Gap: the two second target is not timed separately.
NFR-01 concurrency: E3 and E2 locking; TC-E3-04, TC-E3-06; Pass; none.
NFR-02 integers only: all engines; TC-E1-03 and scripts/static_checks.sh (no float or double in the engines); Pass; none.
NFR-03 tamper evidence: E1 chain and E2 triggers; TC-E2-04, TC-E2-05, TC-E2-11, TC-E1-04, TC-E1-05; Pass; none.
NFR-04 load: the 50 buyers by 20 runs concurrency run; Partly done; Gap: a separate response time target has not been defined; define and measure it.
After NFR-04 add three rows for changes made after the approved design. Do not invent new requirement numbers: in the FR ID column of each of these rows write exactly "Added after design", and say in the row that the change is recorded in docs/DECISION_LOG.md. The three rows are: sign-up, approval and identity record (E2 and E4; TC-E4-09 to TC-E4-12; Pass; guide confirmation pending); market prices from Agmarknet (E2 and E4; TC-E4-15 to TC-E4-18; Pass; guide confirmation of the scope change pending); and the Category 1 to 3 naming and the screen changes (web screens; TC-E4-19; Pass; none).

Section 14 Weekly Progress Record. Keep the Week 10 text. Rewrite the Student completed evidence column for Weeks 11 to 13 as completed work, and tell the student in a square bracket to confirm that the weeks match their calendar, because all engine code was committed on 19 Nov 2026, the screens on 20 Nov, and the later features between 21 and 25 Nov 2026:
Week 11: E1 allocation and hashing (5ab62e1), E2 C++ operations (00be38e) and E3 queue (7fc514f) completed with their tests.
Week 12: E4 server, live feed, role tests, concurrency tests (d51df22), screens for all roles (b7c68c2 to 683b631), defects found and retested (Section 11).
Week 13: end-to-end test and demo data (8ffcd8c), sign-up and People features (60cbad6, f3e3600), market prices (97975a3, 1f3f9b5), screen polish, logo, tab bar and footer (3dbf5a8 to 1ac6224), the repository on GitHub, and the Gate 3 evidence pack; each student demonstrates their own engine [students to confirm].

Section 15 Gate 3 / Review 4 Checklist. Change the answers:
Approved Sprint 1 design referenced: Yes, table 2; changes recorded in the decision log; guide confirmation pending.
Working core engine increment runnable and tested: Yes, all four engines are implemented and tested (107 C++ tests pass).
Engine run steps and actual outputs: Yes, Section 8 and Section 10 [attach screenshots].
Normal, boundary and failure partial tests executed: Yes, Section 9.
Additional state, concurrency, security and API tests: Yes, Sections 7 and 9.
Actual results, failures, corrections and retests recorded: Yes, Section 11 has DEF-01 to DEF-16.
Git history and requirement to test mapping: Yes, Section 13, the commit list in Section 8 and the repository link.
Mocks, deferred work and Sprint 3 dependencies listed: Yes, Sections 12 and 14.
Each student can explain and demonstrate their own engine: leave as "To be confirmed by each student".

Section 16: unchanged.

Section 12, closing line. Keep as written in the Section 12 instruction above.

COVERAGE CHECKLIST (do not finish until every item is done)
Chapter 5 title and subtitle; 5.1 team table (date and repository link updated), student table and engine table (unchanged otherwise).
Part B, Section 1: all rows BL-01 to BL-12 plus the three new rows labelled "Added after design" (Status and Done when), and the evidence line.
Section 2: four engine rows (last column, the E3 dependency cell, the Category sentence) and the evidence line.
Section 3: the existing E2 rows (updated Pending text), the new E1, E2, E3, E4, web screens and demo rows, and the evidence line.
Section 4: E1 largest remainder, E1 hash chain, E2 row lock, E3 queue, E4 session token, E4 password hashing, the algorithm trace paragraph and its evidence line.
Section 5: the five interface rows plus the two new rows (Real / mock, Actual response) and the evidence line.
Section 6: the seven rows, the new market price row, and the evidence line.
Section 7: the nine checks (including zero-copy, unchanged), the new outside service row, and the evidence line.
Section 8: the five engine rows, the new whole-project row and the evidence line.
Section 9: every row from TC-E2-01 to TC-E4-20 with Actual result and Pass / Fail, the unchanged example paragraph, and the evidence line.
Section 10: the evidence table (TC-E2-01, TC-E2-02 to 13, and the new rows), the unchanged TEST LOG block with the added concurrency line, and the evidence line.
Section 11: DEF-01 and DEF-02 kept, DEF-03 to DEF-16 added, the closing row and the evidence line.
Section 12: the four engine rows and the evidence line.
Section 13: the fourteen rows FR-01 to NFR-04 plus the three rows labelled "Added after design".
Section 14: Weeks 10 to 13.
Section 15: the nine checklist rows.
Section 16: unchanged.
Finally scan the whole document once for forbidden symbols (rule 4), for explanations that read stiffly instead of naturally, and for code or terminology that was loosened where it should be exact (the Language and Technical Detail paragraph), and for any leftover "Not yet implemented", "Planned", "Pending", "Not yet run" or "to be added".

AT THE END
Share the updated Word document (.docx) as the output, with the same layout, fonts, table styling, headers and footers as the attached original, and with every change made inside it. Save it under a new name so the original stays untouched. Do not paste the document text into the chat and do not summarise it instead of editing it. Then give me the two lists from rule 10 as a short message.
