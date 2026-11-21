# AgriLedger demo script (about 10 minutes)

Setup: `scripts/seed_demo.sh`, then `scripts/run_server.sh`, then open http://localhost:8080. Every password is `Demo-pass-2026`
(logins are in the README). Use two browser windows for steps 3 and 4.

1. **Buyer view (Buyer 1, `buyer.1`).** Open "Available pools". Point out the live stock, price per quintal, Range, and that farmers appear only as a count.
   Open P-12 (paddy): photo, quality limits with the "check against the official document" note, MSP reference price. Switch kg / quintal.
2. **Buy.** Enter 5 quintal, Review order, Confirm. Show the receipt (total ₹12,500.00, no farmer names or payouts). Double-tap Confirm: only one purchase is made.
3. **Live feed and the race (two windows).** Window A: Buyer 2 (`buyer.2`) on P-12, Window B: Pool Manager (`manager`) on the same pool.
   Buy in A and watch stock and the three ledgers change in B without a reload. Then, as the Manager, place a sale for a buyer for the last stock
   at the same moment Buyer 2 confirms: exactly one succeeds, the other gets a plain sentence ("out of stock"), and stock never goes below zero.
4. **Price change mid-confirmation.** Buyer 2 opens Review order on P-13 (groundnut) and waits. The Manager changes the price. On Confirm, Buyer 2 sees
   "The price has changed", the old and new price and the new total, and nothing is bought until they confirm again.
5. **Farmer view (`farmer.a`).** "My pools" shows share and remaining stock; "My statement" shows each deposit, sale and payout, split in proportion to what each farmer contributed
   (largest-remainder rounding, so the paise always add up exactly).
6. **Sign-up and People (`manager`).** Open **People**: Shivappa Naik signed up and waits as *Pending verification*, with an ID, a mobile number and the document
   (type and last four characters only). Press **Approve**. Show Edit, Deactivate (records stay) and Add person. On the login page, **New here? Sign up** shows the same flow for a new person.
7. **Manager tools (`manager`).** Create a pool (draft, photo, market, Open for sale), record a farmer's deposit, change a price, close and reopen a pool.
8. **Tamper and prove it.** In a terminal:
   `docker compose exec db psql -U postgres -d agriledger -c "SET session_replication_role = replica; UPDATE financial_ledger SET amount_paise = amount_paise + 1 WHERE ctid = (SELECT ctid FROM financial_ledger WHERE entry_type = 'SELLER_PAYOUT' LIMIT 1);"`
   Back as the Manager, open that pool and press **Verify integrity**: it says which record no longer matches, and the Pools list badge turns red with words.

Talking points: integers only (grams, paise); append-only ledgers; every sale is one database transaction behind a per-pool queue; roles are checked on the server.
