# AgriLedger — Master Project Document

**Version 1.0 · 19 November 2026 · Team 05 · Theme 04 (Transaction Based Inventory Management System)**
**Guide:** Neha Ma'am · **Programme:** Mini Project, Computer Science & Engineering

---

## 0. Rules for any AI assistant reading this document

1. **This document is the single source of truth.** If your own assumptions, an older handbook, or a general "best practice" conflict with it, this document wins.
2. **Do not invent features.** Anything not in Sections 3–5 is out of scope. The out-of-scope list (§3.3) is binding.
3. **Use the exact IDs, names and units below.** Engines are E1–E4, requirements are FR-xx / NFR-xx, rules are BR-xx, invariants are INV-x, decisions are D-xx. Never renumber.
4. **Status tags:**
   - **[FINAL]** — confirmed by the team.
   - **[PROPOSED]** — recommended design, not yet explicitly confirmed. Follow it unless the team says otherwise.
   - **[OPEN]** — undecided. Do **not** guess. Ask the user, or write the code so the decision is easy to change.
5. **Money and quantity are integers only.** Never use `float` / `double` anywhere in calculations.
6. If asked to produce code, diagrams or text for the project, say which section(s) of this document you are following. If a request needs something this document doesn't define, ask a question instead of assuming.
7. Old terms listed in §16 ("Do not use") must not be reintroduced.

---

## 1. Project snapshot

| Field | Value |
|---|---|
| Project | **AgriLedger — Atomic Shared-Inventory & Proportional Payout System** |
| One-line pitch | Many small farmers pool the same commodity into one shared stock; buyers purchase from the pool; every sale atomically deducts stock and splits revenue among contributors in proportion to ownership — with three live linked ledgers and a tamper-evident audit trail. |
| Team / Theme | Team 05 / Theme 04 |
| Language & database | **C++** and **PostgreSQL** [FINAL] |
| Concurrency approach | **DB-isolated locking** [FINAL] |
| Users | Farmers (sellers), Buyers, Pool Managers — all three **log in** [FINAL] |
| Display units | Standard market units (kg, quintal) [FINAL] |
| Payments | Simulated only — recorded in ledgers, no real money moves [FINAL] |

### Project status

| Stage | Status |
|---|---|
| S0 Problem definition (Gate 0) | Complete |
| S1 Requirements baseline (Gate 1) | Complete |
| Sprint 1 — S2/S3 solution design + selective partial implementation | **In progress** |
| Gate 2 / Review 3 | **Not yet held** — next formal review |
| S4–S7 broader implementation, testing, integration, closure | Later |

### Team and engine ownership [FINAL]

| Member | SRN | Engine owned |
|---|---|---|
| Ayush Kumar Rai | 01FE24BCS223 | **E1** — DSA (Audit Ledger Engine) |
| Shubhang R | 01FE24BCS090 | **E2** — DBMS (Atomic Sale Engine) |
| Sakshi K | 01FE24BCS218 | **E3** — Operating Systems (Concurrency Engine) |
| Jairaj Kavatagimath | 01FE21BCS232 | **E4** — Web Technologies (Request Orchestration Engine) |

Notes: Jairaj joined later (he is the 4th member; his SRN batch differs, which is correct). Earlier "team roles" (project lead, backend lead, testing lead) are **dropped**. Ownership is **engine-wise only**: each owner must be able to defend their engine end to end at Gate 2.

---

## 2. Problem, stakeholders and goals

### 2.1 Problem statement [FINAL]

> Design a system that allows multiple sellers to pool fungible stock in a shared inventory and ensures that every sale atomically deducts stock and distributes proportional revenue to all contributing sellers, without oversold stock or partial payouts, even under concurrent transactions.

### 2.2 Why the problem is real (S0 evidence, summarised)

- Small farmers cannot meet bulk buyers' minimum order sizes alone, so they pool stock informally (verbal agreements, notebooks, spreadsheets).
- Manual pooling has: no audit trail (payout disputes cannot be settled), no concurrency control (overselling near the last units), no atomicity (stock deduction and payouts are separate manual steps; a seller can be shorted and nothing reverses it), and a single point of failure (one person).
- Existing formal systems (e-NAM, WDRA e-NWR) track individual receipts per depositor; they do not manage fungible pooled stock with proportional payout.

### 2.3 Stakeholders

| Stakeholder | System role name | What they need |
|---|---|---|
| Small seller / farmer | **Farmer** | Fair, verifiable payout proportional to contribution; to see their share, balance and history. Trust is the top priority. |
| Buyer | **Buyer** | See real stock and one price; buy without needing to know whose stock it is; never be oversold. |
| FPO / co-operative staff | **Pool Manager** | Create pools, record deposits, manage accounts, see all ledgers and integrity status without manual maths. |
| Secondary (not users in v1) | — | FPO administrators, auditors/regulators (future), bulk-buyer procurement agencies. |

### 2.4 Needs (S0 needs, now formally numbered) [PROPOSED]

The Gate 1 documents cite "N1–N5" without defining them. Use these definitions from now on:

| ID | Need | Priority rank (S0) |
|---|---|---|
| N1 | Atomic sale: stock deduction and all payouts succeed or fail together | 1 |
| N2 | Concurrency safety: no overselling under simultaneous buyers | 2 |
| N3 | Audit trail: immutable, verifiable history of contributions, sales, payouts | 3 |
| N4 | Automated, provably correct proportional split | 4 |
| N5 | Live visibility of stock, ownership and statements to all users | — |

### 2.5 Project objectives (S0, renumbered O1–O6)

| ID | Objective |
|---|---|
| O1 | Implement an atomic mechanism so stock deduction and all seller payouts succeed or fail together. |
| O2 | Prevent overselling under concurrent buyer requests using DB-isolated locking. |
| O3 | Automatically calculate the proportional revenue split from each seller's ownership. |
| O4 | Maintain an immutable, append-only record of all contributions, sales and payouts (three linked ledgers + hash-chained audit trail). |
| O5 | Provide real-time (live) visibility of stock, ownership and ledger changes to all users. |
| O6 | Demonstrate the effect of concurrency control by comparing unprotected vs DB-isolated handling under simulated load (see D-14; application-locked variant is a Challenge item). |

---

## 3. Scope

### 3.1 In scope — Mandatory

| # | Item |
|---|---|
| 1 | Pool creation (commodity, grade label, display unit, one selling price) and per-farmer contribution tracking |
| 2 | Single price per pool; fungible stock only |
| 3 | Atomic stock deduction + proportional payout, with full rollback on any failure |
| 4 | DB-isolated concurrency handling with a per-pool FIFO queue |
| 5 | **Three linked ledgers** (physical inventory, ownership, financial) + **automatic reconciliation check** on every transaction |
| 6 | Append-only, SHA-256 hash-chained audit trail with integrity verification |
| 7 | **Live updates**: ledgers and stock update on screen without refresh |
| 8 | Login with three user types (Farmer, Buyer, Pool Manager) and server-side access control |
| 9 | REST API + browser dashboard (plain HTML/JS, no mobile app) |
| 10 | Farmer statements generated from the ledgers |

### 3.2 Challenge / stretch (build only if Mandatory items are done and tested)

| ID | Item |
|---|---|
| CH-01 | Deductions as named line items (e.g., market fee, commission, transport) taken from gross before the split, stored as basis-point rates per pool/sale |
| CH-02 | Farmer approve/dispute action on a statement |
| CH-03 | Application-locked (mutex) concurrency variant for the comparison demo |
| CH-04 | Manager approval step before a buyer's sale executes |

### 3.3 Out of scope (binding)

| # | Item | Reason |
|---|---|---|
| 1 | Real payment gateway / bank integration | External dependency and compliance risk; payouts are simulated. |
| 2 | Per-seller or negotiated pricing | Breaks the fungible-pool model. |
| 3 | Mobile application | Time; web dashboard is enough. |
| 4 | Market price feed, e-NAM / ReMS integration, auctions, bidding | Not the problem being solved; AgriLedger augments, not replaces, the mandi system. |
| 5 | Warehouse / stack / bin tracking, physical quality testing | Physical operations; not digitisable here. |
| 6 | Grade-aware compound pool keys and automatic grade blending | A pool is one commodity of one grade (a free-text grade label is stored for display only). Different grades = different pools. |
| 7 | Perishables, dairy | Different turnover and storage; pooling model doesn't apply. |
| 8 | Commission agents, credit/loans, eNWR pledge finance | Needs bank/NBFC partners. |
| 9 | Distributed or multi-node database | Single PostgreSQL instance only. |
| 10 | SMS/email notifications | Not needed for demonstration. |

### 3.4 Assumptions

- Pool Manager enters contributions at the point of deposit; farmers trust the data entered.
- All units in a pool are genuinely fungible (same commodity, same grade).
- Concurrent buyers are simulated by a scripted test client (real load is not available).
- One PostgreSQL instance; one application server process for the demo (design must remain correct with several).

### 3.5 Constraints

- 15-week semester; Sprint 1 design (Weeks 6–9) → Gate 2; later sprints S4–S7.
- Team of 4, one engine each.
- No real banking APIs. C++ and PostgreSQL are fixed.
- **Gate 2 is design-focused:** a fully working integrated application is *not* required for Gate 2; early code is optional.

---

## 4. Users and access control

**What "role" means here:** every login account has exactly one **user type** (its role). The role decides which screens it sees and which API actions the server will allow. The check is done **on the server in E4** for every request — hiding a button in the dashboard is not security. We keep it to exactly three roles; no custom permissions, no admin hierarchy.

| Action | Pool Manager | Farmer | Buyer |
|---|:--:|:--:|:--:|
| Create user accounts (farmers, buyers) | ✔ | | |
| Create a pool | ✔ | | |
| Record a farmer's contribution | ✔ | | |
| Place a sale | ✔ (on behalf of a buyer) | | ✔ (own) |
| View pool list, price, live stock | ✔ | ✔ | ✔ |
| View **all** ledgers of a pool | ✔ | | |
| View own ownership %, balance, ownership/financial entries | ✔ (any farmer) | ✔ (own only) | |
| View own purchases | ✔ (any buyer) | | ✔ (own only) |
| Generate seller statement | ✔ (any farmer) | ✔ (own only) | |
| View reconciliation status & run integrity check | ✔ | view status only | view status only |

Security basics [PROPOSED]: passwords stored hashed (argon2id via libsodium); login returns a random session token; the API rejects any request without a valid token or with the wrong role; all inputs validated in E4 *and* constrained again in the database (layered validation).

---

## 5. Requirements (final IDs)

### 5.1 Numbering decisions

- **NFR numbering follows the RTM / Priority table** (NFR-01 = concurrency correctness, NFR-02 = exact arithmetic, NFR-03 = tamper-evident ledger). The older SRS table used a different order; it is retired.
- The performance requirement from the old SRS returns as **NFR-04**.
- **FR-08 to FR-10 are additions** after the Gate 1 baseline. [PROPOSED] — a short change note should be given to the guide before Gate 2.

### 5.2 Functional requirements

| ID | Requirement | Priority | Source need | Status |
|---|---|---|---|---|
| FR-01 | The system shall allow the Pool Manager to create a pool for a fungible commodity by giving commodity name, grade label, display unit and one selling price per unit. A new pool starts with zero stock. | Mandatory | N5 | Baseline |
| FR-02 | The system shall record each farmer's contribution (pool, farmer, quantity > 0) and add it to the pool stock and that farmer's quantity. | Mandatory | N5 / N3 | Baseline |
| FR-03 | The system shall compute each farmer's ownership percentage as remaining farmer quantity ÷ total remaining pool quantity, and recompute it after every contribution and every sale. | Mandatory | N4 | Baseline |
| FR-04 | The system shall execute the stock deduction and all farmer payouts of a sale as one atomic transaction; if any step fails, the entire sale is rolled back. | Mandatory | N1 | Baseline |
| FR-05 | The system shall split each sale's quantity and revenue among the contributing farmers in proportion to their ownership at the moment of sale. | Mandatory | N4 | Baseline |
| FR-06 | The system shall prevent overselling: under concurrent buyer requests the total quantity sold shall never exceed available stock and stock shall never become negative. | Mandatory | N2 | Baseline |
| FR-07 | The system shall generate a farmer statement, from the ledgers, showing contributions, share of quantity sold, payouts and remaining quantity. | Mandatory | N3 / N5 | Baseline |
| FR-08 | The system shall authenticate users and restrict every operation by user type (Pool Manager, Farmer, Buyer) as defined in §4. | Mandatory | N5 / N3 | **Addition** |
| FR-09 | The system shall maintain three linked ledgers (physical inventory, ownership, financial) and run an automatic reconciliation check inside every transaction; a failed check rolls the transaction back. | Mandatory | N1 / N3 | **Addition** |
| FR-10 | The system shall push every committed ledger change to connected dashboards live, without a page refresh, showing each user only what their type allows. | Mandatory | N5 | **Addition** |

### 5.3 Non-functional requirements

| ID | Requirement | Category | Measurable target |
|---|---|---|---|
| NFR-01 | Stock shall remain correct under concurrent access using DB-isolated locking. | Correctness / Concurrency | 0 oversold or negative-stock outcomes in all protected concurrent tests. |
| NFR-02 | All money and quantity calculations use exact integer arithmetic (money in integer paise, quantity in integer grams). No floating point. | Correctness | Every verification case reconciles exactly with zero rounding loss. |
| NFR-03 | The ledger shall be tamper-evident: the application cannot modify or delete entries and any tampering is detectable. | Security / Integrity | 100% of simulated alterations/deletions detected by integrity verification. |
| NFR-04 | Sale transactions shall complete within the response-time target under the specified concurrent test load. | Performance | **[PROPOSED target]** ≥ 95% of sales complete within 500 ms with 50 concurrent buyers on one pool, single local PostgreSQL. **[OPEN: team to confirm numbers.]** |

### 5.4 Acceptance criteria

| Req | Acceptance criterion | Verification |
|---|---|---|
| FR-01 | Created pool stores commodity, grade label, display unit, one price and starts at 0 stock. | Create pool; inspect record and stock. |
| FR-02 | Each valid contribution raises pool stock and that farmer's balance by exactly the quantity; zero/negative/invalid input is rejected with no state change. | Known contributions; reconcile stock and balances; invalid-input tests. |
| FR-03 | Ownership % equals remaining ÷ total remaining for every farmer after every event. | Multiple contributions and sales; recompute independently. |
| FR-04 | A sale either completes with stock deduction and all payouts together, or leaves no partial state. | Successful sale + injected-failure sale; compare DB state. |
| FR-05 | Per-farmer quantity and payout are proportional to ownership; allocations sum exactly to the sale quantity and gross. | Uneven contributions; rounding-case tests (§12). |
| FR-06 | With concurrent buyers, accepted quantity ≤ starting stock; stock never < 0; accepted + rejected = total requests. | Scripted concurrent test on a deliberately small pool. |
| FR-07 | Statement shows contributions, quantity sold, payouts, remaining quantity, all matching ledger rows. | Statements for partial and completed sales; reconcile. |
| FR-08 | Unauthenticated requests get 401; a user attempting an action outside their type gets 403; no data changes. | Role-matrix test over every endpoint. |
| FR-09 | After every committed transaction, INV-1…INV-5 hold; a deliberately corrupted row makes the transaction fail and roll back. | Failure-injection tests; manual corruption test. |
| FR-10 | A committed event appears on all authorised open dashboards within 2 s (local) without refresh; a rolled-back transaction produces no push. | Two browsers + scripted sale; stopwatch check. |
| NFR-01 | See FR-06, run with ≥ 50 concurrent requests, repeated runs. | Concurrency harness. |
| NFR-02 | Static check: no float/double in the money/quantity code paths; reconciliation passes on all test data. | Code review + tests. |
| NFR-03 | UPDATE/DELETE by the application role is refused; an alteration made as DB superuser is detected by verification at the exact entry. | Tamper test. |
| NFR-04 | Measured percentile meets the target in §5.3. | Load script with timing report. |

### 5.5 Requirements Traceability Matrix

| Req | Source need | Owning engine | Supporting engines | Design ref | Test ref | Status |
|---|---|---|---|---|---|---|
| FR-01 | N5 | E4 | E2 | §7, §10 | T-FR-01 | Baseline |
| FR-02 | N5/N3 | E2 | E1, E4 | §8, §9 | T-FR-02 | Baseline |
| FR-03 | N4 | E1 | E2 | §6 BR-03, §9 | T-FR-03 | Baseline |
| FR-04 | N1 | E2 | E1, E3 | §9, §11 | T-FR-04 | Baseline |
| FR-05 | N4 | E1 | E2 | §6 BR-04/05 | T-FR-05 | Baseline |
| FR-06 | N2 | E3 | E2 | §11 | T-FR-06 | Baseline |
| FR-07 | N3/N5 | E1 | E2, E4 | §8, §10 | T-FR-07 | Baseline |
| FR-08 | N5/N3 | E4 | — | §4, §10 | T-FR-08 | Addition |
| FR-09 | N1/N3 | E2 | E1 | §8 | T-FR-09 | Addition |
| FR-10 | N5 | E4 | E2 | §8.6 | T-FR-10 | Addition |
| NFR-01 | N2 | E3 | E2 | §11 | T-NFR-01 | Baseline |
| NFR-02 | N4 | E1 | E2 | §6 | T-NFR-02 | Baseline |
| NFR-03 | N3 | E1 | E2 | §8.4 | T-NFR-03 | Baseline |
| NFR-04 | N2 | E3 | E2, E4 | §11 | T-NFR-04 | Baseline (restored) |

### 5.6 Use cases

| ID | Actor | Use case | Main flow | Exception |
|---|---|---|---|---|
| UC-01 | Pool Manager | Create pool | Enter commodity, grade label, unit, price → pool created with zero stock | Reject incomplete/invalid data |
| UC-02 | Pool Manager | Record contribution | Select pool and farmer, enter positive quantity → recorded | Reject zero/negative or invalid pool/farmer |
| UC-03 | Pool Manager / Farmer | View stock and ownership | Open pool → live stock, ownership % | Error if record doesn't exist |
| UC-04 | Buyer / Pool Manager (on behalf) | Complete a sale | Enter quantity → validated → queued → atomic sale and payouts | Reject if quantity exceeds stock |
| UC-05 | Buyer / System | Handle concurrent sales | Simultaneous requests → FIFO queue + DB lock → only requests supported by stock accepted | Conflicting requests rejected cleanly |
| UC-06 | Farmer / Pool Manager | Generate farmer statement | Select farmer (and pool) → statement from ledgers | Report if ledger records missing/inconsistent |
| UC-07 | Pool Manager | Inspect ledgers and integrity | Open three ledgers, reconciliation status, run hash-chain verification | Flag the first broken entry |
| UC-08 | All users | Log in / log out | Enter credentials → session token → role-specific dashboard | Reject wrong credentials |
| UC-09 | Pool Manager | Create user account | Enter name, type (Farmer/Buyer), credentials | Reject duplicate username |
| UC-10 | Buyer | Browse pools and own purchases | See live price/stock; view own past purchases | — |

---

## 6. Domain model and business rules

### 6.1 Units and money [FINAL for display; PROPOSED for storage]

| Item | Stored as | Shown on dashboard |
|---|---|---|
| Quantity | integer **grams** (`int64`) | **kg** (up to 3 decimals) and **quintal** (1 quintal = 100 kg). Input is accepted in kg and converted to grams on entry. |
| Price | integer **paise per kg** (`int64`) | ₹ per kg |
| Money amounts | integer **paise** (`int64`) | ₹ (2 decimals) |
| Ownership % | computed exactly from integer grams; shown as a percentage rounded **for display only** | e.g. 24.32 % |

### 6.2 Business rules

| ID | Rule |
|---|---|
| BR-01 | A **pool** = one commodity, one grade label, one price per kg. It starts with 0 stock. The price is fixed for the pool in v1. |
| BR-02 | Only a Pool Manager records contributions. Quantity must be > 0 grams. |
| BR-03 | **Ownership** of farmer *i* in a pool = `remaining_g[i] ÷ total_remaining_g`. Ownership changes with new contributions; sales reduce everyone proportionally (so ratios are preserved, apart from rounding). |
| BR-04 | **Quantity allocation of a sale:** sale quantity `Q` (grams) is split as `Q × remaining_g[i] ÷ total_remaining_g`, using exact integer arithmetic with the **largest-remainder method**. Ties are broken by lowest `seller_id`. Allocations always sum exactly to `Q`. |
| BR-05 | **Revenue:** `gross_paise = round_half_up(Q × price_paise_per_kg ÷ 1000)` — rounded **once**, at the sale level. Each farmer's payout is `gross × alloc_g[i] ÷ Q` using the same largest-remainder method, so payouts always sum exactly to `gross_paise`. |
| BR-06 | A sale is rejected if `Q > pool stock` (checked after taking the pool lock) or `Q ≤ 0`. |
| BR-07 | Payouts are **simulated**: they are recorded as payout rows and financial-ledger entries; no money moves. |
| BR-08 | A sale executes immediately when submitted (no approval step in v1; see CH-04). |
| BR-09 | Ledgers are append-only. A correction is a **new adjustment event**, never an edit. (No adjustment UI in v1; the rule exists so nothing is ever edited in place.) |

### 6.3 Invariants (must hold after every committed transaction)

| ID | Invariant |
|---|---|
| INV-1 | `pool.total_stock_g` = Σ `pool_seller_balance.remaining_g` for that pool |
| INV-2 | `pool.total_stock_g` = latest `inventory_ledger.resulting_stock_g` for that pool |
| INV-3 | For every farmer: `pool_seller_balance.remaining_g` = latest `ownership_ledger.resulting_balance_g` |
| INV-4 | For every sale: Σ `alloc_g` = sale quantity and Σ payouts = `gross_paise` |
| INV-5 | `pool.total_stock_g ≥ 0` and Σ quantity sold ≤ Σ quantity contributed |

---

## 7. Architecture

### 7.1 Layered architecture (client–server) [FINAL]

| Layer | Contents |
|---|---|
| Presentation | Browser dashboard (HTML + vanilla JS) with a WebSocket connection for live updates |
| Application | REST API + WebSocket server, **E4 Request Orchestration Engine** (auth, validation, routing, live fan-out) |
| Core processing | **E1** Audit Ledger Engine, **E2** Atomic Sale Engine, **E3** Concurrency Engine |
| Data / persistence | PostgreSQL database `agriledger` |

### 7.2 Engines [FINAL names and owners]

| ID | Name | Owner | Course focus | Primary responsibility |
|---|---|---|---|---|
| E1 | DSA (Audit Ledger Engine) | Ayush Kumar Rai | Data Structures & Algorithms, Discrete Mathematics | Proportional allocation with exact integers; builds hash-linked audit entries; ledger verification; statement data; lookup structures |
| E2 | DBMS (Atomic Sale Engine) | Shubhang R | DBMS | Schema, ACID transactions, row locks, constraints/triggers, the three ledgers, reconciliation, NOTIFY |
| E3 | Operating Systems (Concurrency Engine) | Sakshi K | Operating Systems | Per-pool FIFO queue, worker threads, retry policy, concurrency test harness |
| E4 | Web Technologies (Request Orchestration Engine) | Jairaj Kavatagimath | Web Technologies & Networks (+ validation/testing from Software Engineering) | REST/WebSocket API, authentication and role checks, input validation, orchestration, dashboard, integration tests |

### 7.3 What each engine does and does NOT own

| Engine | Owns | Does NOT own |
|---|---|---|
| E1 | Allocation maths, hash computation, chain verification, statement assembly, in-memory lookups | Writing to the database, locking, HTTP, queueing |
| E2 | All SQL, transactions, locking, ledger rows, reconciliation, NOTIFY | Allocation algorithm, hash algorithm, request queueing, HTTP |
| E3 | Queueing and dispatch order of sale requests, retries, concurrency experiments | Stock arithmetic, SQL for sales, validation rules, allocation |
| E4 | Login/sessions, role checks, input validation, API, WebSocket broadcast, dashboard, end-to-end tests | Transaction implementation, locking, hashing, allocation |

### 7.4 Interfaces between engines [PROPOSED; supersedes earlier inconsistent diagrams]

| From → To | What is exchanged |
|---|---|
| Dashboard ↔ E4 | HTTP/JSON requests and responses; WebSocket live events |
| E4 → E3 | Validated **sale** requests (every sale goes through E3) |
| E4 → E2 | Validated **pool-creation, contribution, user-account** requests and **read/query** requests |
| E3 → E2 | One sale request at a time per pool, for atomic execution |
| E2 → E1 | Balances + sale data → allocations/payouts; ledger rows + previous hash → prepared audit entry (in-process function calls) |
| E1 → E2 | Allocations, payouts, prepared audit entry (hash-linked) |
| E2 → E4 | Results and PostgreSQL `NOTIFY` events (via E4's listener connection) |

### 7.5 Sale path (the one flow everyone must agree on)

`Buyer → Dashboard → E4 (auth, validate) → E3 (per-pool FIFO queue) → E2 (transaction, row lock) ⇄ E1 (allocation, hash) → PostgreSQL → COMMIT → NOTIFY → E4 listener → WebSocket → dashboards`

E3 queues and dispatches. **E2 opens the transaction and takes the lock.** (Earlier wording in Sprint 1 UC-OS-01 steps 3–7 that had E3 opening the transaction is superseded.)

---

## 8. Ledger design — the core of AgriLedger

### 8.1 Concept

One business **event** (e.g., "sale of 500 kg from POOL-01") produces linked rows in **three ledgers**, all inside one transaction, tied together by `event_id`, and then sealed into a **hash-chained audit trail**.

| Ledger | Question it answers | Written on |
|---|---|---|
| **Ledger 1 — Physical inventory** | What stock exists in the pool? | contribution (+), sale (−) |
| **Ledger 2 — Ownership** | Which farmer owns how much of it? | contribution (+), sale (− per farmer) |
| **Ledger 3 — Financial** | What money was generated and who is owed what? | sale (gross + per-farmer payout rows) |
| **Audit trail (`LEDGER_ENTRY`)** | Has anything been altered? | every event (one hash-linked entry per event) |

A **reconciliation check** (INV-1…INV-5) runs inside every transaction before COMMIT. If it fails, the whole transaction rolls back.

### 8.2 Tables [PROPOSED — extends the Sprint 1 ER design]

Business tables (current state and facts):

| Table | Key fields |
|---|---|
| `app_user` | user_id PK, username UNIQUE, password_hash, user_type (`MANAGER`/`FARMER`/`BUYER`), seller_id FK null, buyer_id FK null |
| `session_token` | token PK, user_id FK, expires_at |
| `seller` | seller_id PK, seller_name, contact_reference |
| `buyer` | buyer_id PK, buyer_name |
| `pool` | pool_id PK, commodity, grade_label, display_unit, price_paise_per_kg, total_stock_g, created_at |
| `contribution` | contribution_id PK, pool_id FK, seller_id FK, quantity_g, contributed_at, event_id FK |
| `pool_seller_balance` | balance_id PK, pool_id FK, seller_id FK, remaining_g — UNIQUE(pool_id, seller_id) |
| `sale` | sale_id PK, pool_id FK, buyer_id FK, quantity_g, unit_price_paise_per_kg, gross_paise, created_at, event_id FK |
| `payout` | payout_id PK, sale_id FK, seller_id FK, quantity_allocated_g, payout_amount_paise |
| `event` | event_id PK, pool_id FK, event_type (`POOL_CREATED`/`CONTRIBUTION`/`SALE`), actor_user_id FK, created_at |

Append-only ledgers (never updated or deleted):

| Table | Key fields |
|---|---|
| `inventory_ledger` | inv_id PK, event_id FK, pool_id FK, movement_type, delta_g (signed), resulting_stock_g, created_at |
| `ownership_ledger` | own_id PK, event_id FK, pool_id FK, seller_id FK, delta_g (signed), resulting_balance_g, created_at |
| `financial_ledger` | fin_id PK, event_id FK, pool_id FK, seller_id FK null, entry_type (`SALE_GROSS`/`SELLER_PAYOUT`; later `FEE_*` for CH-01), amount_paise, created_at |
| `LEDGER_ENTRY` (audit trail) | ledger_entry_id PK, pool_id FK, sequence_no, event_id FK, event_type, previous_hash, entry_hash, created_at — UNIQUE(pool_id, sequence_no) |

`pool_seller_balance` and `pool.total_stock_g` are the fast "current state"; the ledgers are the permanent history. INV-1…INV-3 force them to agree.

### 8.3 What is written per event

| Event | inventory_ledger | ownership_ledger | financial_ledger | audit entry |
|---|---|---|---|---|
| POOL_CREATED | 1 row, stock 0 | — | — | 1 |
| CONTRIBUTION | 1 row (+qty) | 1 row (farmer, +qty) | — | 1 |
| SALE | 1 row (−Q) | N rows (one per contributing farmer, − alloc) | 1 `SALE_GROSS` + N `SELLER_PAYOUT` | 1 |

### 8.4 Audit trail: append-only and tamper-evident

- **Per-pool hash chain** [PROPOSED]: each pool has its own chain (`sequence_no` 1, 2, 3 …). The pool row is already locked for every contribution/sale, so chain order needs **no extra lock** and different pools never block each other. First entry's `previous_hash` = 64 zeros.
- **Entry hash:** `SHA-256( pool_id | sequence_no | event_id | event_type | created_at_utc | canonical_ledger_rows | previous_hash )`, where `canonical_ledger_rows` is the event's inventory, ownership and financial rows serialised in a fixed order. Computed by **E1**, stored by **E2**.
- **Append-only enforcement (E2):** the application's DB role has INSERT/SELECT only on ledger and audit tables (no UPDATE/DELETE/TRUNCATE), plus `BEFORE UPDATE OR DELETE` triggers that raise an exception.
- **Verification (E1, via UC-07):** walk each pool's chain, recompute every hash, check each `previous_hash` link, check sequence has no gaps, and re-check INV-1…INV-5 from the ledgers. Report the **first broken entry**.

### 8.5 Reconciliation (E2, inside the transaction)

Run after all inserts, before COMMIT: INV-1, INV-2, INV-3 (for touched farmers), INV-4 (for sales), INV-5. Any failure → ROLLBACK with a reason code. A scheduled/on-demand **full** reconciliation across all history is also available to the Pool Manager.

### 8.6 Live updates (FR-10) [PROPOSED]

1. Inside the transaction, E2 issues `NOTIFY ledger_events, '<event_id>:<pool_id>'`. PostgreSQL delivers it **only on COMMIT** — a rolled-back sale never notifies.
2. E4 keeps one dedicated connection that has run `LISTEN ledger_events` (a listener thread).
3. On notification, E4 fetches that event's ledger rows and broadcasts a JSON message over **WebSocket** to subscribed dashboards, filtered by user type (a farmer only receives their own ownership/financial rows plus pool stock; a buyer only pool stock and their own purchases; a manager everything).
4. On reconnect, the dashboard re-fetches the current state, then resumes live events.

### 8.7 What each dashboard shows

| User | Screens |
|---|---|
| **Pool Manager** | Pool list; pool detail with **live stock gauge**; **three live ledger feeds** (new rows appear at the top); reconciliation badge (green RECONCILED + last event checked / red with failing INV); "Verify integrity" button showing chain result; forms: create pool, record contribution, place sale for a buyer, create account; farmer list with ownership %, balances. |
| **Farmer** | My pools; my ownership % and remaining quantity (kg/quintal); my ownership and payout entries streaming live; my statement per pool. |
| **Buyer** | Available pools with commodity, grade, ₹/kg, **live available stock**; buy form (kg); my purchases; result of last purchase (accepted / rejected with reason). |

---

## 9. Algorithms and data structures (E1, with E2/E3 touchpoints)

| Item | Structure / method | Used for |
|---|---|---|
| Largest-remainder allocation | integer arithmetic over `int64` (use 128-bit intermediates where `Q × remaining` might overflow) | BR-04, BR-05 |
| Append-only ledger | `std::vector` of entries in memory for verification; append-only tables in DB | NFR-03 |
| FIFO queue | `std::queue` per pool with mutex + condition variable (E3) | Request ordering |
| Hash map | `std::unordered_map` keyed by seller/pool/contribution id | Fast lookup for statements and allocation |
| Hash chaining | SHA-256 (OpenSSL EVP) over canonical rows + previous hash | NFR-03 |
| Ownership ratio | exact rational `remaining_g / total_remaining_g`; percentage derived for display only | FR-03 |

Allocation algorithm (BR-04 and BR-05 use the same routine):
1. For each farmer *i*: `base_i = floor(Total × weight_i ÷ Σweight)`; `rem_i = (Total × weight_i) mod Σweight`.
2. `leftover = Total − Σ base_i`.
3. Give +1 to the `leftover` farmers with the largest `rem_i` (ties → lowest `seller_id`).
4. Result sums exactly to `Total`.

---

## 10. Technology stack and API

### 10.1 Stack [PROPOSED except where marked FINAL]

| Concern | Choice |
|---|---|
| Language | **C++17 or later** [FINAL: C++] |
| Database | **PostgreSQL** (15+) [FINAL] |
| DB access | **libpqxx** (connection pool of N connections; one separate listener connection) |
| HTTP + WebSocket | **Crow** (header-only; has REST + WebSocket). Alternative if the team prefers: Drogon. |
| JSON | nlohmann/json |
| Hashing | OpenSSL (libcrypto) SHA-256 |
| Password hashing | libsodium (argon2id) |
| Build | CMake (+ vcpkg or system packages) |
| Unit tests | GoogleTest |
| Concurrency/load tests | C++ test client using `std::thread` + real HTTP calls (or direct libpqxx) |
| Frontend | Static HTML + CSS + vanilla JavaScript served by the C++ server; WebSocket for live data; no build step |
| Local dev | PostgreSQL via Docker Compose (optional) |

### 10.2 API surface [PROPOSED]

| Method & path | Who | Purpose |
|---|---|---|
| `POST /auth/login`, `POST /auth/logout` | all | Session token |
| `POST /users` | Manager | Create farmer/buyer account (UC-09) |
| `POST /pools` | Manager | Create pool (UC-01) |
| `GET /pools`, `GET /pools/{id}` | all | Pool list/detail (live stock) |
| `POST /pools/{id}/contributions` | Manager | Record contribution (UC-02) |
| `POST /pools/{id}/sales` | Buyer, Manager | Place sale (UC-04) |
| `GET /pools/{id}/ledgers/inventory` \| `/ownership` \| `/financial` | Manager (farmer: own rows) | Ledger rows (paged) |
| `GET /pools/{id}/reconciliation` | all (status only for non-managers) | Current INV status |
| `POST /pools/{id}/integrity-check` | Manager | Run chain verification (UC-07) |
| `GET /farmers/{id}/statement?pool={id}` | Manager, that Farmer | Statement (UC-06) |
| `WS /live` | all (authenticated) | Live events |

Sample sale request/response:

```json
POST /pools/12/sales
{ "quantity_kg": "500.000", "buyer_id": 7 }

200 OK
{ "status": "ACCEPTED", "sale_id": 91, "quantity_g": 500000,
  "gross_paise": 1250000, "stock_after_g": 1500000,
  "payouts": [ {"seller_id":1,"alloc_g":150000,"paise":375000}, ... ] }

409 Conflict
{ "status": "REJECTED", "reason": "INSUFFICIENT_STOCK", "stock_g": 2000 }
```

---

## 11. Concurrency design (E3 + E2) — DB-isolated locking [FINAL]

**Mechanism:** every sale first enters a **per-pool FIFO queue** (E3, arrival-order fairness and back-pressure). A worker dispatches it to E2, which runs one transaction:

1. `BEGIN` (READ COMMITTED).
2. `SELECT … FROM pool WHERE pool_id = ? FOR UPDATE` — **row lock**; other sales/contributions on this pool wait here.
3. Re-read stock; if `Q > stock` → `ROLLBACK`, reject with `INSUFFICIENT_STOCK`.
4. `SELECT … FROM pool_seller_balance WHERE pool_id = ? ORDER BY seller_id FOR UPDATE` (fixed lock order avoids deadlocks).
5. E1 computes allocations/payouts and the prepared audit entry (previous hash read from this pool's chain tail).
6. Insert sale, payouts, three ledger rows sets, audit entry; update balances and stock.
7. Run reconciliation; `NOTIFY`; `COMMIT`.

**Why both queue and DB lock:** the DB lock alone guarantees correctness even with several server processes; the queue adds fair ordering and avoids many threads blocking on one lock.

**Failure handling:** lock timeout, deadlock or serialization failure → rollback and **retry up to 3 times** with short backoff, then reject. Invalid requests are rejected by E4 and never reach E3.

**Guaranteed postconditions:** every request is accepted or rejected; stock is never negative; total sold ≤ starting stock; accepted + rejected = total requests.

**Worked scenarios (use as test cases):**

| Scenario | Result |
|---|---|
| Stock 10 kg; Buyer 1 and Buyer 2 each request 8 kg | First in queue accepted (stock → 2 kg); second rejected (insufficient stock); no overselling |
| Stock 20 kg; both request 8 kg | Both accepted; stock → 4 kg |
| Failure injected during Buyer 1's transaction | Full rollback; Buyer 2 then processed against unchanged stock |

**Comparison demo (O6)** [see D-14]: the harness can run a deliberately **unprotected** mode (no lock) to show overselling and a **DB-isolated** mode to show correctness. The application-locked (mutex) variant is CH-03.

---

## 12. Test plan (E4 owns integration; each engine owner owns their unit tests)

| ID | What | Expected |
|---|---|---|
| T-FR-01…07 | Per acceptance criteria in §5.4 | Pass |
| T-FR-08 | Role matrix over every endpoint | 401/403 correctly; no state change |
| T-FR-09 | Inject failure after each step of a sale; corrupt a balance row | Rollback; reconciliation fails loudly |
| T-FR-10 | Two open dashboards + a sale | Both update ≤ 2 s; rolled-back sale pushes nothing |
| T-NFR-01 | 50+ concurrent buyers, small pool, repeated 20× | 0 oversold; accepted + rejected = total |
| T-NFR-02 | Rounding cases below; scan for floating types | Exact |
| T-NFR-03 | Edit/delete a row as superuser, then verify | Detected at the exact entry |
| T-NFR-04 | Load run with timing | ≥ 95% under target |

### Worked numeric example (use for demos and unit tests)

Pool: Wheat, Grade A, **₹25/kg = 2,500 paise/kg**. Contributions: Farmer A 600 kg, Farmer B 800 kg, Farmer C 600 kg → stored as 600,000 g / 800,000 g / 600,000 g; total 2,000,000 g.

Sale: **500 kg = 500,000 g**.

| Farmer | Ownership | Allocated g | Payout (paise) | Remaining g |
|---|---|---|---|---|
| A | 30% | 150,000 | 375,000 | 450,000 |
| B | 40% | 200,000 | 500,000 | 600,000 |
| C | 30% | 150,000 | 375,000 | 450,000 |
| **Total** | | **500,000** | **1,250,000 (= ₹12,500 gross)** | **1,500,000** |

Rounding example: three farmers with equal 1,000 g each; sale of 100 g at 2,500 paise/kg. Quantity: 33.33… each → floors 33/33/33, leftover 1 → equal remainders, tie to lowest id → **A 34, B 33, C 33**. Gross = 100 × 2500 ÷ 1000 = **250 paise**. Payout shares: A 85.0, B 82.5, C 82.5 → floors 85/82/82 (sum 249), leftover 1 → largest remainder tie between B and C → lowest id → **A 85, B 83, C 82 = 250**.

---

## 13. Implementation order and milestones [PROPOSED]

Design continues in parallel for all engines. Build order follows dependencies (E2 first, since the others use its tables and transaction path).

| Step | Work | Primary owner(s) |
|---|---|---|
| M1 | PostgreSQL schema, constraints, triggers, append-only roles; basic atomic contribution/sale transaction without hashing | E2 |
| M2 | Allocation (largest-remainder) library with unit tests; hash/audit-entry builder; chain verification | E1 |
| M3 | Three ledgers wired into E2's transaction + reconciliation + NOTIFY | E2 + E1 |
| M4 | Per-pool FIFO queue, worker, retry policy; concurrency harness (unprotected vs DB-isolated) | E3 |
| M5 | REST API, authentication, role checks, validation | E4 |
| M6 | WebSocket live feed + dashboards for all three user types | E4 (+E2 for events) |
| M7 | Integration, full test matrix, tamper and load tests, demo script | All; E4 leads |

**Gate 2 (design) evidence** — all must be consistent with this document: overall architecture; engine interaction diagram; engine-wise architecture (4); at least one end-to-end sequence diagram (use the sale path in §7.5); ER diagram (§8.2); state/activity diagrams where meaningful; algorithm/data-structure notes; API/data exchange; security/error/testing notes; optional early code with one test and a Git commit. Assessment: 4 areas × 5 marks = 20, scaled to 10 ISA marks; every student must explain their own engine independently.

---

## 14. Risks (project level) — owners are by engine

| ID | Risk | Mitigation | Owner |
|---|---|---|---|
| R1 | Concurrency / rollback complexity underestimated | Short learning phase early; use standard locking patterns; test early | E2, E3 owners |
| R2 | Scope creep (payments, mobile, deductions, grades) | Out-of-scope list is binding; anything added must displace something | All four (shared) |
| R3 | Hard to demonstrate true concurrent load | Scripted multi-thread client; record unprotected-vs-protected evidence | E3 owner |
| R4 | Integration slippage (DB + API + live UI) | Build E2 first, UI last; keep UI minimal | E4 owner |
| R5 | Formal invariant / mutual-exclusion argument not rigorous | Dedicated review session; formal write-up of INV-1…INV-5 | E1 owner (E3 owner reviews) |
| R6 | Member unavailability / uneven workload | Every engine has a named backup among the other owners; weekly check | All four (shared) |
| R7 | Live updates (LISTEN/NOTIFY + WebSocket) harder than expected | Build polling fallback first; add push after core works | E4, E2 owners |
| R8 | Hash chain breaks or contends under concurrency | Per-pool chain under the existing pool row lock; concurrency tests include chain verification | E1, E2 owners |

---

## 15. Decision register

### Confirmed today [FINAL]

| ID | Decision |
|---|---|
| D-01 | Team has 4 members; Jairaj Kavatagimath (01FE21BCS232) joined later and owns E4 |
| D-02 | E4 is "Web Technologies (Request Orchestration Engine)" |
| D-03 | Gate 0 team roles are dropped; ownership is engine-wise |
| D-04 | **Three linked ledgers** (physical inventory, ownership, financial) with automatic reconciliation and an immutable audit trail |
| D-05 | Ledgers update live on screen |
| D-06 | Farmers, buyers and pool managers all log in (three user types) |
| D-07 | PostgreSQL + C++ |
| D-08 | DB-isolated locking is the concurrency method |
| D-09 | Dashboard shows standard market units (kg, quintal) |
| D-10 | Gate 2 has not yet been held |

### Proposed by this document — follow unless the team objects [PROPOSED]

| ID | Decision |
|---|---|
| D-11 | Store grams and paise-per-kg as integers; single rounding of gross at sale level; largest-remainder splits |
| D-12 | Per-pool hash chain, one audit entry per event (covers its ledger rows and payouts) |
| D-13 | Sales go E4 → E3 (FIFO) → E2 (transaction + lock) ⇄ E1; contributions skip the queue but take the same pool row lock |
| D-14 | Production path is DB-isolated. The unprotected mode exists as a test-harness switch for the O6 comparison. Application-locked variant is CH-03 (needs guide confirmation that O6 can be met with unprotected vs DB-isolated only) |
| D-15 | Sales execute immediately (no approval); Pool Manager can place a sale on a buyer's behalf |
| D-16 | Pool Manager creates all user accounts and records contributions |
| D-17 | NFR renumbering (§5.1), needs N1–N5 defined (§2.4), FR-08…FR-10 added, NFR-04 restored |
| D-18 | Deductions, approve/dispute, manager approval are Challenge items (CH-01, CH-02, CH-04) |
| D-19 | Crow + libpqxx + OpenSSL + libsodium + vanilla-JS frontend (§10) |

### Still open [OPEN]

| ID | Question | Who decides |
|---|---|---|
| D-20 | Dates for Gate 2 (Week 9 per handbook?), later gates and final demo; what the current week is | Team / guide |
| D-21 | NFR-04 numbers (500 ms / 50 concurrent buyers is only a proposal) | Team |
| D-22 | Guide's approval for FR-08…FR-10 additions and the new ledger design | Team → guide |
| D-23 | Owner of the formal invariant / Discrete Mathematics write-up (proposed: E1 owner, reviewed by E3 owner) | Team |
| D-24 | Diagrams (ER, use cases, engine architectures, sequence, state) must be **redrawn** to match this document before Gate 2 | Each engine owner |

---

## 16. Change log — what was resolved, and terms **not to use** any more

| Old content (in the source handbooks) | Now |
|---|---|
| Team of 3 (Gate 0) | Team of 4 |
| E4 = "Software Development (Validation Engine)", owner "ALL" | E4 = Web Technologies (Request Orchestration Engine), owner Jairaj |
| Risk owners as roles (backend lead / project lead / testing lead) | Dropped; owners by engine |
| SRS NFR order (01 arithmetic, 02 tamper, 03 performance) | NFR-01 concurrency, 02 arithmetic, 03 tamper-evident, 04 performance |
| N1–N5 used but undefined; N1 used for several things | Defined in §2.4 |
| FIFO queue inside E1 | FIFO queue belongs to **E3** only |
| E3 opens DB transaction and locks (UC-OS-01) | **E2** opens the transaction and takes the lock; E3 only queues and dispatches |
| E4 → E2 shown as the sale path | Sales go E4 → E3 → E2; E4 → E2 only for non-sale operations |
| "Seller" and "Farmer" both used | Farmer = the user type; "seller" survives only in table/column names |
| Names "Aayush", "Jairaj K" | "Ayush Kumar Rai", "Jairaj Kavatagimath" |
| Artefact titles differing between documents | "AgriLedger — Atomic Shared-Inventory & Proportional Payout System" |
| 15-week plan "weeks 4–9 engine build" (Gate 0) | Superseded by Sprint 1 plan; dates in D-20 |
| Single "ledger" with append-only entries only | Three linked ledgers + hash-chained audit trail |
| Pooling report ideas (grade-keyed pools, deductions, warehouses, auditor/accountant roles, auction integration) | Out of scope or Challenge — see §3 |

---

## 17. Domain background (context only, not requirements)

Source: *Farmer Pooling, FPOs and the Mandi System — A Ground-Level Systems Report* (18 Sep 2026) and the S0 pack.

- Small farmers deliver produce to FPO/co-op collection centres; grain of similar grade is mixed into a **homogeneous pooled lot**. Because grain becomes fungible, **ownership is tracked as a proportional share** of the pool rather than by physical bag — exactly what AgriLedger models.
- Real pools are separated by commodity, variety, grade and crop year; selling to a blended price would shortchange higher-quality contributors. AgriLedger handles this by using **one pool per grade**.
- In real settlements, buyer payment goes to the FPO's bank account and the FPO distributes shares; deductions (market fee, commission, hamali, transport) vary by state and commodity and are rarely itemised for the farmer. AgriLedger v1 simulates payouts and records only gross and farmer shares (deductions = CH-01).
- The report's recommended principles that **are** adopted: three linked ledgers, an automatic "stock = Σ ownership" reconciliation, an immutable audit log, and conservative promises (transparency, not guaranteed payment speed). The report's wider feasibility phases (e-NAM integration, multi-FPO, loans) are **not** part of this project.
- Caution: the three "field perspective" quotes in that report are labelled illustrative composites. **Do not cite them as real interviews.**

---

## 18. Glossary

| Term | Meaning |
|---|---|
| Pool | A shared stock of one commodity and grade at one price |
| Fungible | Interchangeable units; no unit belongs to a specific farmer |
| Contribution | Quantity a farmer adds to a pool |
| Ownership | Farmer's remaining quantity ÷ total remaining quantity in the pool |
| Event | One business action (pool created, contribution, sale) that writes linked ledger rows |
| Ledger | Append-only history table |
| Reconciliation | Automatic check that stock, ownership and money totals agree |
| Hash chain | Each audit entry's hash includes the previous entry's hash, so any change breaks everything after it |
| Atomic | All steps happen or none do |
| DB-isolated locking | Using the database's row locks and transaction isolation to serialise access to shared stock |
| Paise | 1/100 of a rupee; money is stored as integer paise |
| Quintal | 100 kg |
| FPO | Farmer Producer Organisation |
