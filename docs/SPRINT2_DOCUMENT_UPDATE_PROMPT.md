You are updating the Sprint 2 student handbook (Chapter 5, Gate 3 / Review 4) for Team 05, Theme 04, AgriLedger. I am attaching the current Word document. It was written early in Sprint 2, when only the database layer existed. Since then all four engines, the web screens and the tests have been built. Your job is to update the document so that it is true as of 22 November 2026.

HOW TO WORK

1. Use only the facts in this prompt and in the attached document. Do not invent numbers, outputs, commit hashes, names or dates. If something needs a screenshot or a value I have not given you, leave a clear note in square brackets that says what the student must attach or add, for example [attach screenshot of terminal output].
2. Keep the structure exactly: the same chapter heading, section numbers and titles, table columns, Engine IDs and Test IDs. Keep the existing wording wherever it is still true. Change only what is out of date.
3. Language: plain, humanised, short sentences, like a student explaining their own work to a reviewer. No symbols or decoration beyond what the document already uses. No arrows, no emojis, no marketing words.
4. Never delete a failed test or a defect. Keep the original wording and add the correction and the retest result.
5. A test marked Pass must never have an empty Actual result. Write what was observed, using the facts below.
6. Replace "Not yet implemented", "Planned", "Pending", "Not yet run", "Contract only; not run" and "to be added" with the real status wherever the facts below support it. Leave "[repository URL: add]" as it is.
7. Leave Section 16 (the Smart Campus Lab Scheduler example) completely unchanged.
8. When you finish, give me two short lists: every place where you left a square bracket note, and every statement you were not fully sure about.

PART 1. THE FACTS (the only source of truth)

Project state on 22 November 2026. All four engines are implemented, runnable and tested. Engine code is about 3,000 lines of C++17 across the four engine folders, the web screens are plain HTML, CSS and JavaScript, and the database is PostgreSQL 16 in a Docker container.

Commits (short hash, date, what it contains)
27b6aab, 19 Nov 2026: initial commit with the specification, schema v1.1, database test scripts and wireframes.
eb3e773, 19 Nov: Stage 0 answers applied to the spec, project rules and decision log.
af85129, 19 Nov: repository skeleton, PostgreSQL container, CMake targets and one-command tests.
a78c141, 19 Nov: E1 allocation, canonical hashing, chain verification and invariants.
07019e3, 19 Nov: E2 atomic operations on libpqxx.
2ed58dc, 19 Nov: E3 per-pool FIFO queue, retry, BUSY, and the concurrency harness.
3c0ab28, 19 Nov: E4 server (Crow), sessions, role checks, photo upload and live feed.
e78021b, 20 Nov: frontend foundation and static file serving.
fd3d8f5, 20 Nov: browser audits run as an Android phone profile.
492e802, 20 Nov: login and first-login password screens.
5a11471, 20 Nov: buyer screens and buy flow.
ba1efba, 20 Nov: farmer screens.
282eec3, 20 Nov: Pool Manager screens.
80ac9d9, 20 Nov: demo data seeded through the real server, end-to-end browser test, demo script.
7d65bf7, 21 Nov: wording changes and removal of helper text.
5e4f553, 21 Nov: farmer statement laid out as a bill, price history.
31bb0bc, 21 Nov: sign-up, approval, identity record, people tools and pool market (schema, E2, E4, tests).
973ca2d, 21 Nov: sign-up and People screens, demo data.
06d7c8a, 21 Nov: research notes on quality limits and identity documents (not verified, nothing loaded).
d60aae6, 21 Nov: visual polish round.

Test totals today
Database: worked_example.sql reports INV-1 to INV-5 all OK (ownership 30 / 40 / 30 percent, remaining 450 / 600 / 450 kg). negative.sh now has 42 checks and all 42 PASS (the earlier document said 34; 8 checks were added later for the frontend rules and the new account rules).
C++: 103 tests pass, 0 fail. By area: E1 unit tests 27, E3 queue tests 10, E4 integration tests 32, E2 integration tests 26, concurrency tests 7, static checks 1.
JavaScript: 40 tests pass.
Browser: screen checks for every role at 1200 px and at 390 px (Android phone profile) pass, and the end-to-end story passes at both sizes.
Static checks: no float or double in the engines, no unprotected mode in the engines, no literal UI strings in the web code. All pass.

How to run things (use these in Section 8)
Database tests: scripts/test.sh db
All C++ tests: scripts/test.sh cpp (prints "100% tests passed, 0 tests failed out of 103")
One engine only: ctest -R E1 (E1 tests), ctest -R E2Test, ctest -R "E3Queue|ConcurrencyTest", ctest -R E4Test, all run from the build folder with the test database variables that scripts/test.sh sets
JavaScript tests: node --test tests/js/
Screens: scripts/screens_test.sh all
End to end: scripts/e2e_test.sh
Run the site: scripts/seed_demo.sh once, then scripts/run_server.sh, then open http://localhost:8080

Real concurrency result (from ConcurrencyTest)
Protected path (E3 queue plus E2 row lock): 50 buyers each asking for 1 quintal from a pool with 10 quintal, repeated for 20 runs. In every run exactly 10 requests were accepted and 40 rejected, accepted plus rejected equalled 50, and stock never went below zero. Oversold in 0 of 20 runs.
Unprotected comparison (test harness only, its own scratch table): oversold in 20 of 20 runs, worst case 40 quintal sold beyond the stock.

Real API samples (captured from the running server)
Login success: request body with username and password gives 200 with role, must_change_password, name and approval, for example role buyer, name Mahantesh Traders, approval APPROVED.
Login wrong password: 401 with error INVALID_CREDENTIALS (one generic answer).
Sale accepted: POST /pools/13/sales with request_id, quantity_g 100000, confirmed_price_paise_per_kg 6783 gives 200 with status ACCEPTED, sale_id 2, receipt_no R-0002, gross_paise 678300, quantity_g 100000, unit_price_paise_per_kg 6783, stock_after_g 2487500, duplicate false.
Same request_id sent again: 200 with the same sale_id, receipt_no and amounts and duplicate true (nothing sold twice).
Price changed (confirmed price 1 against current 6783): 409 with status REJECTED, reason PRICE_CHANGED, current_price_paise_per_kg 6783.
Too much (900000000 g requested): 409 with status REJECTED, reason INSUFFICIENT_STOCK, stock_g 2487500.
Buyer calling a Pool Manager endpoint (POST /pools/13/integrity-check): 403 with error FORBIDDEN.
Negative quantity: 400 with error INVALID_FIELD, nothing written.
Pool Manager integrity check: 200 with ok true, checked_entries 5 and a head_hash (the newest record's hash, shown on screen as a short seal).
For the live feed message shape and the internal calls between E3, E2 and E1, quote the contracts in docs/IMPLEMENTATION_SPEC.md sections 6 and 7 and name the test that exercises them; do not invent sample output for those.

Mapping of every test ID in the document to a real test (use these names in Actual result and in the evidence columns)
TC-E2-14 sale with a failure injected after each step: E2Test.FailureAfterAnyStepRollsBackEverything. Result Pass: every step failure rolled back fully, stock and balances unchanged.
TC-E2-15 each reason code: E2Test.InsufficientStockReturnsRemainingStock, OutOfStockWhenNothingIsLeft_PoolStaysOpen, PriceChangedReturnsCurrentPrice, BelowMinOrderUnlessItTakesTheWholeRemainder, PoolNotOpenForDraftAndClosed, and at HTTP level E4Test.SaleReasonCodesAndPlainBodies. Result Pass: correct code and data returned, nothing written.
TC-E2-16 corrupt one balance then sell: E2Test.CorruptedBalanceFailsReconciliationAndRollsBack. Result Pass: reconciliation failed and the sale rolled back.
TC-E1-01 worked allocation: E1Allocation.WorkedExample. Pass.
TC-E1-02 rounding example (100 g over three equal balances): E1Allocation.RoundingExample, plus TiesGoToLowestIdWhateverTheInputOrder. Pass.
TC-E1-03 very large values: E1Allocation.HugeValuesUseWideIntermediates. Pass.
TC-E1-04 intact chain verifies: E1Chain.IntactChainVerifies. Pass.
TC-E1-05 altered ledger row then verify: E1Chain.AlteredLedgerRowIsNamedExactly (also E2Test.TamperedLedgerRowIsNamedAtTheExactEntry). Pass.
TC-E1-06 zero quantity or no balances: E1Allocation.RejectsInvalidInput. Pass.
TC-E1-07 farmer statement for the worked example: E4Test.ViewsMatchTheWorkedExample (checks deposit 600 kg and remaining 450 kg for the first farmer) and the browser statement checks. Pass.
TC-E3-01 stock 10 kg, two buyers 8 kg each: ConcurrencyTest.Scenario_TenKgTwoBuyersEightKgEach. Pass.
TC-E3-02 stock 20 kg, two buyers 8 kg each: ConcurrencyTest.Scenario_TwentyKgTwoBuyersEightKgEach. Pass.
TC-E3-03 failure in buyer one's transaction: ConcurrencyTest.Scenario_FailureInBuyerOnesTransaction_BuyerTwoSeesUnchangedStock. Pass.
TC-E3-04 50 buyers, 20 runs: ConcurrencyTest.Protected_FiftyBuyersTwentyRuns_ZeroOversold and E3Queue.FakeSaleStress_50BuyersTwentyRuns_ZeroOversold. Pass with the real numbers above.
TC-E3-05 lock timeout injected: ConcurrencyTest.StalledPoolLock_RetriesThenBusy_ThenRecovers and E3Queue.RetriesStopAfterThreeAttemptsAndReportBusy. Pass: retried up to 3 times, then BUSY, and recovered.
TC-E3-06 unprotected comparison: ConcurrencyTest.Unprotected_HarnessOnlyCounter_Oversells. Pass: oversold in 20 of 20 runs.
TC-E4-01 valid login: E4Test.LoginSessionCookieAndForcedPasswordChange. Pass: 200 with role and session cookie.
TC-E4-02 wrong password: the live sample above (401 INVALID_CREDENTIALS) and E4Test.LoginIsThrottledAfterRepeatedFailures. Pass.
TC-E4-03 role matrix: E4Test.RoleMatrixOverEveryEndpoint. Pass: wrong role gets 403 on every endpoint, no state change.
TC-E4-04 malformed sale body: E4Test.SaleInputValidation and the live sample (400 INVALID_FIELD). Pass.
TC-E4-05 same request_id twice: E4Test.DuplicateRequestIdOverHttp and the live sample (duplicate true). Pass.
TC-E4-06 two open dashboards, one sale: E4Test.LiveFeedIsFilteredByRoleAndNeverReportsARolledBackSale and the browser check that a number changes without a reload. Pass for delivery without a reload. State honestly that the two second target was not timed separately.
TC-E4-07 buyer sees no farmer names, shares or payouts: E4Test.BuyersNeverSeeFarmerNamesSharesOrPayouts. Pass.
TC-E4-08 forced server error shows plain reason only: E4Test.SaleReasonCodesAndPlainBodies and the browser tests that force a server error and show a plain sentence with no code. Pass.
Add these new rows at the end of the E4 group (new features since the first draft), each Pass:
TC-E4-09 sign-up creates a pending account and approval unlocks deposits and purchases: E4Test.SignUpMakesAPendingAccount_ApprovalUnlocksDepositsAndPurchases.
TC-E4-10 sign-up refuses a Pool Manager role, a wrong document type, a short password, a bad mobile number and a duplicate mobile number: E4Test.SignUpRefusesBadInput_NoSelfMadeManagers_NoDuplicateMobileNumbers.
TC-E4-11 people tools are Pool Manager only and deactivation keeps history: E4Test.PeopleToolsAreForThePoolManagerOnly_DeactivateKeepsHistory.
TC-E4-12 check records names the exact tampered record: E4Test.IntegrityCheckNamesTheTamperedEntry and the end-to-end tamper test.
Rows TC-E2-01 to TC-E2-13 stay as they are, except that where they quote negative.sh as 34 checks, write that it now has 42 checks and all pass.

Defects that really happened (for Section 11). Keep DEF-01 and DEF-02 as written; update DEF-02's retest to: re-checked at TC-E3-06, the unprotected mode exists only in the test harness on its own scratch table, ConcurrencyTest.Unprotected_HarnessOnlyCounter_Oversells passes, and the static check confirms no unprotected code in the engines. Fix commit for DEF-01: 27b6aab is where the test scripts first appear; use the commit the student confirms and mark it with a square bracket note. Then add these rows in order:
DEF-03, E4 static files. Problem: pages loaded over one kept-alive connection received an extra 404 because the static file handler ended the response twice. Correction: the handler now ends the response once. Retest: E4Test.ManyRequestsOnOneConnectionAllGetTheirOwnAnswer passes. Fix commit e78021b.
DEF-04, frontend quantity parser. Problem: typing 2,5 was read as 25 (a comma treated as a thousands separator). Correction: strict thousands-comma rules, a comma used as a decimal is refused with a plain message. Retest: the format tests in tests/js pass. Fix commit e78021b.
DEF-05, login screen. Problem: the login screen showed the word null because an empty value was appended to the page. Correction: a null-safe page builder, and an audit that fails any page showing null, undefined or NaN text. Retest: screen checks pass. Fix commit 492e802.
DEF-06, buyer pool limits. Problem: quality limits were listed alphabetically instead of in the reference table order. Correction: ordered by table order. Retest: buyer screen checks pass. Fix commit 5a11471.
DEF-07, Pool Manager forms. Problem: when a live update redrew the page while a request was in flight, the success or error message was written to the old form and the screen kept the previous message. Correction: the page now tracks the current message element. Retest: the screen check passed three times in a row. Fix commit 282eec3.
DEF-08, sign-up duplicate number. Problem: a duplicate mobile number was reported as a duplicate username because the number is also the sign-in name. Correction: explicit checks that name which of the two is taken. Retest: E4Test.SignUpRefusesBadInput_NoSelfMadeManagers_NoDuplicateMobileNumbers passes. Fix commit 31bb0bc.
DEF-09, people tools. Problem: the Pool Manager could set one person's mobile number to a value another person already used as a sign-in name. Correction: the edit is refused with the duplicate message. Retest: E4Test.PeopleToolsAreForThePoolManagerOnly_DeactivateKeepsHistory passes. Fix commit 973ca2d.
DEF-10, sign-up page. Problem: the sign-up page did not load because of a typing mistake in its script path. Correction: path fixed. Retest: the screen check for sign-up passes at both sizes. Fix commit 973ca2d.
Replace the last row ("No other defects recorded yet") with: no other defects recorded; the register is complete up to 22 Nov 2026. Replace the closing evidence line with: Evidence filename / repository link: defect rows above; fix commits as listed; the retest tests are named in each row.

PART 2. WHAT TO CHANGE, SECTION BY SECTION

Section 5.1 Team and Engine Register. Keep everything. Leave the repository URL as [repository URL: add]. In the Main function column keep the text.

Part B, Section 1 Sprint 2 Plan and Backlog. Change every Status to Done, and in the Done when column keep the original wording but add one short sentence of evidence each, using the facts above:
BL-01 allocation and rounding: E1Allocation.WorkedExample and RoundingExample pass.
BL-02 hash chain: the E1Chain tests pass.
BL-03 farmer statement from ledger rows: E4Test.ViewsMatchTheWorkedExample passes.
BL-04 schema v1.1: worked_example.sql INV-1 to INV-5 OK and negative.sh 42 PASS (the original text says 34; update).
BL-05 C++ operations: the E2 integration tests pass (26).
BL-06 atomic sale: sale of 500 kg gives 150 / 200 / 150 kg and 375,000 / 500,000 / 375,000 paise; E2Test.WorkedExample and FailureAfterAnyStepRollsBackEverything pass.
BL-07 per-pool FIFO queue: the E3Queue tests pass (10).
BL-08 concurrency harness: protected path oversold 0 of 20 runs, unprotected oversold 20 of 20 runs.
BL-09 login and roles: E4Test.RoleMatrixOverEveryEndpoint and the login tests pass.
BL-10 REST endpoints and validation: E4Test.SaleInputValidation and the other E4 tests pass.
BL-11 notify and live feed: E4Test.LiveFeedIsFilteredByRoleAndNeverReportsARolledBackSale passes.
BL-12 dashboard screens: the screens for all three roles plus login and sign-up are built and checked at 1200 px and 390 px, and the buy flow works end to end.
For BL-12 also rewrite the Done when text: remove "may extend into Sprint 3" and write that the screens follow the wireframes, with the changes agreed between 20 and 22 Nov 2026 recorded in docs/DECISION_LOG.md (Grade wording, no helper text, sign-up and People screens, Check records), and that the buy flow works end to end.
The closing line "Evidence filename / repository link" should name the repository folders (engines, web, tests, db/tests, scripts), the docs/IMPLEMENTATION_SPEC.md section 12 stage list, and keep [repository URL] for the remaining link.

Section 2 Sprint 1 Design Handoff. Keep the existing text. Update only the last column, Any approved change:
E1: the proposed hash format for events without ledger rows and the fixed UTC timestamp format were implemented as proposed; still waiting for the E1 and E2 owners' formal review.
E2: schema v1.1 plus changes agreed on 21 Nov 2026: new account fields (mobile number, generated public ID, approval status, identity document type and last four characters, who verified and when) and a market field on pools. Recorded in docs/DECISION_LOG.md sections N and O. Guide confirmation still pending.
E3: implemented as designed; the unprotected comparison mode exists only in the test harness on a scratch table.
E4: new endpoints added for sign-up, approval and people tools, a who-am-I endpoint and the integrity check returning the newest record's hash. Self sign-up reverses the earlier decision D-16 (no self-registration); Pool Manager accounts are still created only on the server.

In the same section, in the E3 row, change the Implementation dependency text "The E2 sale operation (stub until ready)" to: the real E2 sale operation, used directly with no stub. Leave the other three dependency cells and all Approved design reference cells unchanged. Then end the section with this evidence line, replacing the existing one: Evidence filename / repository link: docs/IMPLEMENTATION_SPEC.md, docs/AgriLedger_Master_Document.md, db/schema.sql and docs/DECISION_LOG.md; Gate 2 pack: to attach.

Section 3 Engine-wise Implementation Register. Keep the existing E2 rows, but update the last column (Pending) of each as follows. Schema v1.1 row: keep "diagrams to be redrawn", and replace "Apply to the team database" with a statement that it is applied to the project database by Docker Compose and that the schema was extended on 21 Nov 2026 (account fields and pool market). Reference data seed row: keep "other crops' limits to be verified and filled" because it is still true, and add that research notes are in docs/research and nothing unverified was loaded. Database test scripts row: change the behaviour text to the worked example plus 42 negative checks, and the Pending text to none (checks were added for each new rule). Then replace every "Not yet implemented" row with a real row:
E1 allocation and hashing: engines/e1_audit, largest-remainder allocation, SHA-256 canonical hashing, chain verification, invariants INV-1 to INV-5; commit a78c141; 27 unit tests pass. Pending: formal review of the hash format by the owners.
E2 C++ operations: engines/e2_sale, create pool, deposit, photo, price and status change, atomic sale with reconciliation and NOTIFY, accounts and views; commit 07019e3, extended in 31bb0bc; 26 integration tests pass. Pending: none for Gate 3.
E3 queue: engines/e3_queue, per-pool FIFO lanes, three attempts then BUSY, concurrency harness; commit 2ed58dc; 10 unit tests and 7 concurrency tests pass.
E4 server: engines/e4_web, login, sessions, role checks, REST, photo upload, WebSocket live feed, sign-up and people tools; commit 3c0ab28, extended in 31bb0bc; 32 integration tests pass.
Web screens: web folder, Login, Sign up, Buyer, Farmer and Pool Manager screens; commits e78021b to 282eec3, 973ca2d and d60aae6; 40 JavaScript tests and the browser checks pass.
Demo data and end-to-end test: commit 80ac9d9.
Replace "[add hash]" in the E2 database rows with 27b6aab (schema and seed and test scripts) and keep the "Pending" text only where it is still true.

Section 4 Algorithm and Data Structure Implementation. Keep the expected values. Fill the observed parts:
E1 largest remainder: observed by E1Allocation.WorkedExample (150000 / 200000 / 150000 g and 375000 / 500000 / 375000 paise); edge case tie goes to the lowest seller id, E1Allocation.TiesGoToLowestIdWhateverTheInputOrder; rounding case 100 g gives 34 / 33 / 33 and 250 paise gross, E1Allocation.RoundingExample.
E1 hash chain: observed by the E1Chain tests; an altered row is named at its exact sequence number; gaps are detected.
E2 row lock: observed by E2Test.RowLockPreventsOversellWithoutAnyQueue and the worked example reconciliation; duplicate request_id refused by the unique key, E2Test.DuplicateRequestIdReturnsTheOriginalResultAndNeverSellsTwice.
E3 queue: observed by ConcurrencyTest.Scenario_TenKgTwoBuyersEightKgEach (one accepted, one rejected) and E3Queue.RetriesStopAfterThreeAttemptsAndReportBusy.
E4 session token and password hashing: 256-bit random session token in an HttpOnly cookie that lasts 8 hours, argon2id through libsodium; wrong password gives one generic 401; observed by E4Test.LoginSessionCookieAndForcedPasswordChange and the live sample.
For the E4 row also fill the edge case column: an expired or missing session gets 401 (E4Test.LogoutEndsTheSession_ExpiredSessionsAreRefused) and a wrong role gets 403 (E4Test.RoleMatrixOverEveryEndpoint). The E4 password hashing row continues on the next page of the document; fill it too: argon2id through libsodium, observed that a wrong password returns one generic 401 and the password is never returned or logged; edge case: repeated wrong passwords are throttled (E4Test.LoginIsThrottledAfterRepeatedFailures).
Replace the paragraph that says E1 is not implemented with: E1 is implemented; the unit test E1Allocation.WorkedExample asserts the values above and passes [attach screenshot of the test output]. Keep the expected values quoted in that paragraph. Replace its closing evidence line with: Evidence filename / repository link: tests/unit/e1_allocation_test.cpp, tests/unit/e1_hashing_test.cpp, docs/AgriLedger_Master_Document.md section 12; execution trace: [attach screenshot of ctest -R E1 output].

Section 5 API / Interface and Data Exchange. In the Real / mock column write Real for every row. Fill Actual response from the real samples above:
Dashboard to E4, POST /auth/login: the login success and wrong password samples.
E4 to E3, submit sale request: the accepted sample, the price changed and insufficient stock samples. Change the example pool and numbers to the real ones above, or keep the document's example and add the real ones beneath it.
E3 to E2, execute_sale: real internal call; result codes tested by TC-E2-15; no stub is used any more.
E2 to E1, allocate and build audit entry: real internal call; tested by TC-E1-01 and the E2 worked example.
E2 to E4, NOTIFY then WebSocket: real; tested by E4Test.LiveFeedIsFilteredByRoleAndNeverReportsARolledBackSale; quote the message shape from the spec, do not invent output.
Replace the closing evidence line with: Evidence filename / repository link: docs/IMPLEMENTATION_SPEC.md sections 6 and 7 (contracts), tests/integration/e4_test.cpp; run output is in Section 10 and the samples were captured from the running server on 22 Nov 2026.

Section 6 Database, State and Transaction Implementation. Keep all rows and their observed text. In the Proof column of the first six rows (create pool, three deposits, photo and open, sale of 500 kg, refused sales, edit or delete a ledger row) add that the same behaviour is now also checked through the C++ operations: E2Test.WorkedExample, E2Test.StatusRules_PhotoRequiredClosedRefusesDepositsReopenNeedsPhoto, E2Test.PoolNotOpenForDraftAndClosed, E2Test.BelowMinOrderUnlessItTakesTheWholeRemainder, E2Test.DuplicateRequestIdReturnsTheOriginalResultAndNeverSellsTwice and E2Test.TamperedLedgerRowIsNamedAtTheExactEntry. Change the last row (sale transaction with an injected failure) to Actual observed: full rollback, stock and balances unchanged, E2Test.FailureAfterAnyStepRollsBackEverything passes; Proof: that test and TC-E2-14. Where text says 34 negative checks, write 42.

Section 7 Concurrency, Security and Data Movement. Keep each row's Implementation text. Replace each "Planned" proof with real proof:
Simultaneous updates: TC-E3-01 to TC-E3-04 and TC-E3-06 pass; 50 buyers by 20 runs, protected path oversold 0 of 20, unprotected oversold 20 of 20.
Transaction rollback: TC-E2-14 and TC-E2-16 pass.
Role-based access: TC-E2-04, TC-E2-13 and TC-E4-03 pass.
Malformed input: TC-E2-02, TC-E2-03, TC-E2-05 and TC-E4-04 pass.
Safe error handling: TC-E4-08 passes.
Duplicate request: TC-E2-10 and TC-E4-05 pass.
Tamper evidence: TC-E2-04, TC-E2-11, TC-E1-04, TC-E1-05 pass, and the end-to-end test tampers with a stored row and the screen names the record.
Passwords and sessions: TC-E4-01 and TC-E4-02 pass; login is also throttled after repeated failures and per address (E4Test.LoginIsThrottledAfterRepeatedFailures and LoginIsAlsoThrottledPerAddressAcrossManyUsernames).
Zero-copy: keep the justification unchanged.
Update the closing evidence line to mention the tests folder and scripts/test.sh.

Section 8 Build, Run and Git Evidence. Keep the E2 database row, changing 34 PASS to 42 PASS and its commit to 27b6aab. Replace the other rows:
E1: run ctest -R E1 after scripts/test.sh cpp; observed 27 tests pass; commit a78c141.
E2 C++ operations: ctest -R E2Test; 26 tests pass; commit 07019e3 and 31bb0bc.
E3: ctest -R "E3Queue|ConcurrencyTest"; 17 tests pass (10 plus 7); commit 2ed58dc.
E4: scripts/run_server.sh then open http://localhost:8080, plus ctest -R E4Test; 32 tests pass; commit 3c0ab28 and later.
Add a last row for the whole project: scripts/test.sh cpp prints "100% tests passed, 0 tests failed out of 103"; scripts/screens_test.sh all and scripts/e2e_test.sh pass; latest commit d60aae6.
Replace the closing evidence line with: Evidence filename / repository link: db/tests/setup_test_db.sh, scripts/test.sh, scripts/run_server.sh, scripts/seed_demo.sh and README.md (run steps for every engine); [repository URL] for the repository link.

Section 9 Testing, Mandatory Test Case Register. Fill every Actual result and set every Pass / Fail using the mapping in Part 1. Change every Pending to Pass. Add the four new E4 rows. Leave the template paragraph that starts "Example: TC-E1-03 | E1 scheduler" exactly as it is; it is a guide, not an entry. Leave the header repeated at the top of each page. Replace the closing evidence line with: Evidence filename / repository link: db/tests/negative.sh and db/tests/worked_example.sql (TC-E2-01 to TC-E2-13); tests/unit, tests/integration, tests/concurrency and tests/browser (all other tests); command scripts/test.sh.

Section 10 Testing, Test Execution Evidence. Keep the TC-E2-01 row and the pasted test log; change "34 checks" to "42 checks" and add one line saying the earlier log shows the first 34 checks and the later ones are in the same script. Replace the "Not yet run" row with one row per engine group: E1 tests, E2 C++ tests, E3 and concurrency tests, E4 tests, JavaScript tests, screen checks, end-to-end test. For each, How test was run is the command from Part 1, Evidence is [attach screenshot of terminal output], and Date and commit is 22 Nov 2026 with the latest commit d60aae6. Replace the closing evidence line with: Evidence filename / repository link: terminal output pasted in this section; [attach screenshots of each command listed above]. Keep the TEST LOG (terminal output) block unchanged, and add the real concurrency summary line as text evidence: "unprotected counter oversold in 20 of 20 runs (worst: 40 quintal sold beyond the stock); the protected path oversold in 0 of 20".

Section 11 Defect, Correction and Retest Register. Apply the defect list in Part 1.

Section 12 Engine Readiness and Remaining Work. Rewrite the rows:
E1 works now: allocation, hashing, chain verification, invariants, 27 tests pass. Mocked or incomplete: none. Not yet tested: nothing known. Sprint 3 dependency: formal review of the hash format by the owners.
E2 works now: schema, operations, atomic sale with reconciliation, notify, accounts and views, 26 integration tests and 42 database checks pass. Mocked or incomplete: none. Not yet tested: nothing known. Sprint 3 dependency: guide confirmation of the new fields.
E3 works now: queue, workers, retry, BUSY, concurrency harness, 17 tests pass. Mocked or incomplete: none. Not yet tested: a separate latency target. Sprint 3 dependency: none.
E4 works now: server, sign-up, login, role checks, REST, live feed, photo upload, dashboard screens for all roles, 32 integration tests and browser checks pass. Mocked or incomplete: Google sign-in and mobile OTP are not built (out of scope: no outside services or SMS). Not yet tested: only the Android Chrome profile has been tested; iPhone-specific checks are deferred. Sprint 3 dependency: final review.

Section 13 Requirements, Code and Partial Test Mapping. Keep the FR and NFR list. For each row write the real code reference, the test IDs, Result and Gap or action:
FR-01 create pool: E2 create pool operation; TC-E2-06, E4Test.PoolCreationAndStatusValidation; Pass; none.
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
NFR-04 load: the 50 buyers by 20 runs concurrency run; Partly done; Gap: a separate response-time target has not been defined; define and measure it.

Section 14 Weekly Progress Record. Keep the Week 10 text. Rewrite the Student completed evidence column for Weeks 11 to 13 as completed work, and tell the student in a square bracket to confirm that the weeks match their calendar, because all engine code was committed between 19 and 22 Nov 2026:
Week 11: E1 allocation and hashing (a78c141), E2 C++ operations (07019e3) and E3 queue (2ed58dc) completed with their tests.
Week 12: E4 server, live feed, role tests, concurrency tests (3c0ab28), screens for all roles (e78021b to 282eec3), defects found and retested (Section 11).
Week 13: end-to-end test and demo data (80ac9d9), sign-up and People features and visual polish (31bb0bc to d60aae6), Gate 3 evidence pack; each student demonstrates their own engine [students to confirm].

Section 15 Gate 3 / Review 4 Checklist. Change the answers:
Approved Sprint 1 design referenced: Yes, tables 2; changes recorded in the decision log; guide confirmation pending.
Working core engine increment runnable and tested: Yes, all four engines are implemented and tested (103 C++ tests pass).
Engine run steps and actual outputs: Yes, Section 8 and Section 10 [attach screenshots].
Normal, boundary and failure partial tests executed: Yes, Section 9.
Additional state, concurrency, security and API tests: Yes, Sections 7 and 9.
Actual results, failures, corrections and retests recorded: Yes, Section 11 has DEF-01 to DEF-10.
Git history and requirement to test mapping: Yes, Section 13 and the commit list in Section 8 [add repository URL].
Mocks, deferred work and Sprint 3 dependencies listed: Yes, Sections 12 and 14.
Each student can explain and demonstrate their own engine: leave as "To be confirmed by each student".

Section 16: unchanged.

Section 12, closing line. Keep "Evidence filename / repository link: This document; repository: [repository URL]" and add: tests and scripts as listed in Sections 8 and 9.

COVERAGE CHECKLIST (do not finish until every item is done)
Page group one: Chapter 5 title and subtitle; 5.1 team table, student table and engine table (unchanged).
Part B, Section 1: all twelve backlog rows BL-01 to BL-12 (Status and Done when), and the evidence line.
Section 2: four engine rows (only the last column and the E3 dependency cell) and the evidence line.
Section 3: the existing E2 rows (updated Pending text), the new E1, E2, E3, E4, web screens and demo rows, and the evidence line.
Section 4: E1 largest remainder, E1 hash chain, E2 row lock, E3 queue, E4 session token, E4 password hashing, the algorithm trace paragraph and its evidence line.
Section 5: all five interface rows (Real / mock, Actual response) and the evidence line.
Section 6: all seven rows and the evidence line.
Section 7: all nine checks (including zero-copy, unchanged) and the evidence line.
Section 8: all five engine rows, the new whole-project row and the evidence line.
Section 9: every row from TC-E2-01 to TC-E4-12 with Actual result and Pass / Fail, the unchanged example paragraph, and the evidence line.
Section 10: the evidence table (TC-E2-01, TC-E2-02 to 13, and the new rows), the unchanged TEST LOG block with the added concurrency line, and the evidence line.
Section 11: DEF-01 and DEF-02 kept, DEF-03 to DEF-10 added, the closing row and the evidence line.
Section 12: all four engine rows and the evidence line.
Section 13: all fourteen rows FR-01 to FR-10 and NFR-01 to NFR-04.
Section 14: Weeks 10 to 13.
Section 15: all nine checklist rows.
Section 16: unchanged.

AT THE END
Share the updated Word document (.docx) as the output, with the same layout, fonts, table styling, headers and footers as the attached original, and with every change made inside it. Save it under a new name so the original stays untouched. Do not paste the document text into the chat and do not summarise it instead of editing it. Then give me the two lists from rule 8 as a short message.
