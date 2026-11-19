-- =====================================================================
-- AgriLedger — Atomic Shared-Inventory & Proportional Payout System
-- PostgreSQL schema (v1.1), mapped from the corrected ER diagram + frontend decisions.
-- Units: quantities in integer grams (_g), money in integer paise (_paise).
-- Owner: E2 (DBMS, Atomic Sale Engine).
-- =====================================================================

BEGIN;

-- ---------------------------------------------------------------------
-- 1. Users and access (ISA mapped as one table per subclass, shared key)
-- ---------------------------------------------------------------------
CREATE TABLE app_user (
    user_id        BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    username       VARCHAR(50)  NOT NULL UNIQUE,
    password_hash  TEXT         NOT NULL,                 -- argon2id string
    user_type      VARCHAR(10)  NOT NULL CHECK (user_type IN ('MANAGER','FARMER','BUYER')),
    is_active      BOOLEAN      NOT NULL DEFAULT TRUE,
    must_change_password BOOLEAN NOT NULL DEFAULT TRUE,  -- accounts are created by the Pool Manager; first login forces a change
    created_at     TIMESTAMPTZ  NOT NULL DEFAULT now(),
    UNIQUE (user_id, user_type)          -- target of the subclass FKs (keeps the ISA disjoint)
);

CREATE TABLE session_token (
    token       CHAR(64)    PRIMARY KEY CHECK (token ~ '^[0-9a-f]{64}$'),  -- random 256-bit, hex
    user_id     BIGINT      NOT NULL REFERENCES app_user(user_id) ON DELETE CASCADE,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at  TIMESTAMPTZ NOT NULL,
    CHECK (expires_at > created_at)
);

CREATE TABLE pool_manager (
    user_id       BIGINT      PRIMARY KEY,
    user_type     VARCHAR(10) NOT NULL DEFAULT 'MANAGER' CHECK (user_type = 'MANAGER'),
    manager_name  VARCHAR(100) NOT NULL,
    mandi_branch  VARCHAR(100),
    FOREIGN KEY (user_id, user_type) REFERENCES app_user(user_id, user_type)
);

CREATE TABLE seller (                                   -- user type "Farmer"
    seller_id    BIGINT      PRIMARY KEY,               -- = app_user.user_id
    user_type    VARCHAR(10) NOT NULL DEFAULT 'FARMER' CHECK (user_type = 'FARMER'),
    seller_name  VARCHAR(100) NOT NULL,
    village      VARCHAR(100),
    FOREIGN KEY (seller_id, user_type) REFERENCES app_user(user_id, user_type)
);

CREATE TABLE seller_phone (                             -- multivalued attribute phone_no
    seller_id  BIGINT      NOT NULL REFERENCES seller(seller_id) ON DELETE CASCADE,
    phone_no   VARCHAR(15) NOT NULL CHECK (phone_no ~ '^\+?[0-9]{10,14}$'),
    PRIMARY KEY (seller_id, phone_no)
);

CREATE TABLE buyer (
    buyer_id    BIGINT      PRIMARY KEY,                -- = app_user.user_id
    user_type   VARCHAR(10) NOT NULL DEFAULT 'BUYER' CHECK (user_type = 'BUYER'),
    buyer_name  VARCHAR(100) NOT NULL,
    gstin       CHAR(15)    UNIQUE,
    FOREIGN KEY (buyer_id, user_type) REFERENCES app_user(user_id, user_type)
);

-- ---------------------------------------------------------------------
-- 2. Pools and current state
-- ---------------------------------------------------------------------
-- Reference data (read-only for the application; loaded by seed_reference.sql)
CREATE TABLE commodity (
    commodity_id  BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    name          VARCHAR(60) NOT NULL UNIQUE
);

CREATE TABLE commodity_parameter (                      -- quality limits per Range (e-NAM style, no penalties)
    commodity_id    BIGINT       NOT NULL REFERENCES commodity(commodity_id),
    parameter_name  VARCHAR(60)  NOT NULL,
    unit            VARCHAR(10)  NOT NULL DEFAULT '%',
    range1_max      NUMERIC(5,2) CHECK (range1_max >= 0),   -- "at most" limits; NULL = not yet filled
    range2_max      NUMERIC(5,2) CHECK (range2_max >= 0),
    range3_max      NUMERIC(5,2) CHECK (range3_max >= 0),
    source          TEXT,
    source_date     DATE,
    verified        BOOLEAN      NOT NULL DEFAULT FALSE,    -- TRUE only after checking the current official document
    PRIMARY KEY (commodity_id, parameter_name)
);

CREATE TABLE reference_price (                          -- MSP and mandi prices: display only, never blocks a sale
    ref_id                   BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    commodity_id             BIGINT      NOT NULL REFERENCES commodity(commodity_id),
    kind                     VARCHAR(12) NOT NULL CHECK (kind IN ('MSP','MANDI_MODAL')),
    price_paise_per_quintal  BIGINT      NOT NULL CHECK (price_paise_per_quintal > 0),
    label                    VARCHAR(80) NOT NULL,      -- e.g. 'MSP 2026-27' or 'Hubballi mandi'
    as_of                    DATE        NOT NULL
);

CREATE TABLE pool (
    pool_id             BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    commodity_id        BIGINT       NOT NULL REFERENCES commodity(commodity_id),
    variety             VARCHAR(60)  NOT NULL,                   -- one variety per pool (no mixing)
    range_no            SMALLINT     NOT NULL CHECK (range_no BETWEEN 1 AND 3),  -- the pool's grade
    packaging           VARCHAR(60),
    storage_location    VARCHAR(120) NOT NULL,                   -- where the stock is held / collected from
    min_order_g         BIGINT       NOT NULL CHECK (min_order_g > 0),
    status              VARCHAR(6)   NOT NULL DEFAULT 'draft' CHECK (status IN ('draft','open','closed')),
    display_unit        VARCHAR(10)  NOT NULL CHECK (display_unit IN ('kg','quintal')),
    price_paise_per_kg  BIGINT       NOT NULL CHECK (price_paise_per_kg > 0),
    total_stock_g       BIGINT       NOT NULL DEFAULT 0 CHECK (total_stock_g >= 0),   -- INV-5
    created_at          TIMESTAMPTZ  NOT NULL DEFAULT now()
);

CREATE TABLE pool_photo (                               -- append-only; buyers see the 3 most recent
    photo_id     BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    pool_id      BIGINT      NOT NULL REFERENCES pool(pool_id),
    file_path    TEXT        NOT NULL,
    file_sha256  CHAR(64)    NOT NULL CHECK (file_sha256 ~ '^[0-9a-f]{64}$'),
    uploaded_by  BIGINT      NOT NULL REFERENCES app_user(user_id),
    uploaded_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE pool_seller_balance (                      -- relationship HOLDS_SHARE (M:N)
    pool_id      BIGINT NOT NULL REFERENCES pool(pool_id),
    seller_id    BIGINT NOT NULL REFERENCES seller(seller_id),
    remaining_g  BIGINT NOT NULL CHECK (remaining_g >= 0),
    PRIMARY KEY (pool_id, seller_id)
);

-- ---------------------------------------------------------------------
-- 3. Events (one per business action) and business facts
-- ---------------------------------------------------------------------
CREATE TABLE event (
    event_id       BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    pool_id        BIGINT      NOT NULL REFERENCES pool(pool_id),
    event_type     VARCHAR(20) NOT NULL CHECK (event_type IN ('POOL_CREATED','CONTRIBUTION','SALE','PRICE_CHANGED','STATUS_CHANGED')),
    actor_user_id  BIGINT      NOT NULL REFERENCES app_user(user_id),
    created_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (event_id, pool_id)           -- lets child rows prove they belong to the same pool
);

CREATE TABLE contribution (
    contribution_id  BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    event_id         BIGINT      NOT NULL UNIQUE,       -- RECORDS (1:1)
    pool_id          BIGINT      NOT NULL,
    seller_id        BIGINT      NOT NULL REFERENCES seller(seller_id),
    quantity_g       BIGINT      NOT NULL CHECK (quantity_g > 0),                    -- BR-02
    contributed_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    FOREIGN KEY (event_id, pool_id) REFERENCES event(event_id, pool_id)
);

CREATE TABLE sale (
    sale_id                  BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    event_id                 BIGINT      NOT NULL UNIQUE,   -- BOOKS (1:1)
    pool_id                  BIGINT      NOT NULL,
    buyer_id                 BIGINT      NOT NULL REFERENCES buyer(buyer_id),
    request_id               UUID        NOT NULL UNIQUE,   -- client-made id: a repeated request can never sell twice
    quantity_g               BIGINT      NOT NULL CHECK (quantity_g > 0),            -- BR-06
    unit_price_paise_per_kg  BIGINT      NOT NULL CHECK (unit_price_paise_per_kg > 0),
    gross_paise              BIGINT      NOT NULL CHECK (gross_paise >= 0),
    created_at               TIMESTAMPTZ NOT NULL DEFAULT now(),
    FOREIGN KEY (event_id, pool_id) REFERENCES event(event_id, pool_id)
);

CREATE TABLE payout (                                   -- weak entity, owners SALE + SELLER
    sale_id               BIGINT NOT NULL REFERENCES sale(sale_id),
    seller_id             BIGINT NOT NULL REFERENCES seller(seller_id),
    quantity_allocated_g  BIGINT NOT NULL CHECK (quantity_allocated_g >= 0),
    payout_amount_paise   BIGINT NOT NULL CHECK (payout_amount_paise >= 0),
    PRIMARY KEY (sale_id, seller_id)
);

CREATE TABLE pool_change (                              -- price / status changes, sealed in the hash chain
    change_id   BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    event_id    BIGINT      NOT NULL UNIQUE,
    pool_id     BIGINT      NOT NULL,
    field       VARCHAR(6)  NOT NULL CHECK (field IN ('PRICE','STATUS')),
    old_value   TEXT        NOT NULL,
    new_value   TEXT        NOT NULL,
    changed_by  BIGINT      NOT NULL REFERENCES app_user(user_id),
    FOREIGN KEY (event_id, pool_id) REFERENCES event(event_id, pool_id)
);

-- ---------------------------------------------------------------------
-- 4. The three linked ledgers and the audit chain (append-only)
-- ---------------------------------------------------------------------
CREATE TABLE inventory_ledger (
    inv_id             BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    event_id           BIGINT      NOT NULL,
    pool_id            BIGINT      NOT NULL,
    movement_type      VARCHAR(4)  NOT NULL CHECK (movement_type IN ('OPEN','IN','OUT')),
    delta_g            BIGINT      NOT NULL,
    resulting_stock_g  BIGINT      NOT NULL CHECK (resulting_stock_g >= 0),
    created_at         TIMESTAMPTZ NOT NULL DEFAULT now(),
    CHECK ( (movement_type = 'OPEN' AND delta_g = 0)
         OR (movement_type = 'IN'   AND delta_g > 0)
         OR (movement_type = 'OUT'  AND delta_g < 0) ),
    FOREIGN KEY (event_id, pool_id) REFERENCES event(event_id, pool_id)
);

CREATE TABLE ownership_ledger (
    own_id               BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    event_id             BIGINT      NOT NULL,
    pool_id              BIGINT      NOT NULL,
    seller_id            BIGINT      NOT NULL REFERENCES seller(seller_id),
    delta_g              BIGINT      NOT NULL CHECK (delta_g <> 0),
    resulting_balance_g  BIGINT      NOT NULL CHECK (resulting_balance_g >= 0),
    created_at           TIMESTAMPTZ NOT NULL DEFAULT now(),
    FOREIGN KEY (event_id, pool_id) REFERENCES event(event_id, pool_id)
);

CREATE TABLE financial_ledger (
    fin_id        BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    event_id      BIGINT      NOT NULL,
    pool_id       BIGINT      NOT NULL,
    seller_id     BIGINT      REFERENCES seller(seller_id),         -- NULL for SALE_GROSS
    entry_type    VARCHAR(15) NOT NULL CHECK (entry_type IN ('SALE_GROSS','SELLER_PAYOUT')),
    amount_paise  BIGINT      NOT NULL CHECK (amount_paise >= 0),
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
    CHECK ( (entry_type = 'SALE_GROSS'    AND seller_id IS NULL)
         OR (entry_type = 'SELLER_PAYOUT' AND seller_id IS NOT NULL) ),
    FOREIGN KEY (event_id, pool_id) REFERENCES event(event_id, pool_id)
);

CREATE TABLE ledger_entry (                             -- hash-chained audit trail
    ledger_entry_id  BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    event_id         BIGINT      NOT NULL UNIQUE,       -- SEALED_BY (1:1)
    pool_id          BIGINT      NOT NULL,
    sequence_no      BIGINT      NOT NULL CHECK (sequence_no >= 1),
    previous_entry_id BIGINT     UNIQUE,                -- PRECEDES (recursive 1:1); NULL only for entry #1
    previous_hash    CHAR(64)    NOT NULL CHECK (previous_hash ~ '^[0-9a-f]{64}$'),
    entry_hash       CHAR(64)    NOT NULL UNIQUE CHECK (entry_hash ~ '^[0-9a-f]{64}$'),
    created_at       TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (pool_id, sequence_no),                      -- one chain per pool, no duplicates
    UNIQUE (ledger_entry_id, pool_id),                  -- target of the self-reference below
    CHECK ((sequence_no = 1) = (previous_entry_id IS NULL)),
    FOREIGN KEY (event_id, pool_id) REFERENCES event(event_id, pool_id),
    FOREIGN KEY (previous_entry_id, pool_id) REFERENCES ledger_entry(ledger_entry_id, pool_id)  -- same pool's chain
);

-- ---------------------------------------------------------------------
-- 5. Indexes for the common lookups (statements, ledger feeds)
-- ---------------------------------------------------------------------
CREATE INDEX idx_event_pool          ON event (pool_id, event_id);
CREATE INDEX idx_contribution_seller ON contribution (seller_id, pool_id);
CREATE INDEX idx_sale_pool           ON sale (pool_id);
CREATE INDEX idx_sale_buyer          ON sale (buyer_id);
CREATE INDEX idx_payout_seller       ON payout (seller_id);
CREATE INDEX idx_inv_pool            ON inventory_ledger (pool_id, inv_id);
CREATE INDEX idx_own_pool_seller     ON ownership_ledger (pool_id, seller_id, own_id);
CREATE INDEX idx_fin_pool_seller     ON financial_ledger (pool_id, seller_id, fin_id);
CREATE INDEX idx_session_user        ON session_token (user_id);
CREATE INDEX idx_photo_pool          ON pool_photo (pool_id, photo_id DESC);
CREATE INDEX idx_change_pool         ON pool_change (pool_id, change_id);
CREATE INDEX idx_ref_price           ON reference_price (commodity_id, kind, as_of DESC);

-- ---------------------------------------------------------------------
-- 6. Append-only enforcement (NFR-03, BR-09)
-- ---------------------------------------------------------------------
CREATE FUNCTION forbid_change() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    RAISE EXCEPTION 'AgriLedger: % on % is not allowed (append-only table)', TG_OP, TG_TABLE_NAME
        USING ERRCODE = 'insufficient_privilege';
END $$;

DO $$
DECLARE t TEXT;
BEGIN
    FOREACH t IN ARRAY ARRAY['event','contribution','sale','payout','pool_photo','pool_change',
                             'inventory_ledger','ownership_ledger','financial_ledger','ledger_entry']
    LOOP
        EXECUTE format('CREATE TRIGGER %I BEFORE UPDATE OR DELETE ON %I
                        FOR EACH ROW EXECUTE FUNCTION forbid_change()', t || '_no_change', t);
        EXECUTE format('CREATE TRIGGER %I BEFORE TRUNCATE ON %I
                        FOR EACH STATEMENT EXECUTE FUNCTION forbid_change()', t || '_no_truncate', t);
    END LOOP;
END $$;

-- ---------------------------------------------------------------------
-- 7. Business rules enforced by the database itself
-- ---------------------------------------------------------------------
-- (a) A pool cannot be 'open' until it has at least one photo.
CREATE FUNCTION pool_open_needs_photo() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    IF NEW.status = 'open' AND NOT EXISTS (SELECT 1 FROM pool_photo WHERE pool_id = NEW.pool_id) THEN
        RAISE EXCEPTION 'AgriLedger: pool % cannot be opened without a photo', NEW.pool_id
            USING ERRCODE = 'check_violation';
    END IF;
    RETURN NEW;
END $$;
CREATE TRIGGER pool_open_needs_photo BEFORE INSERT OR UPDATE OF status ON pool
    FOR EACH ROW EXECUTE FUNCTION pool_open_needs_photo();

-- (b) A sale needs an open pool, and must meet the minimum order unless it takes
--     the whole remainder. Remaining stock is computed from the facts (contributions
--     minus earlier sales), so the check does not depend on statement order.
CREATE FUNCTION sale_rules() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE p pool%ROWTYPE; remaining BIGINT;
BEGIN
    SELECT * INTO p FROM pool WHERE pool_id = NEW.pool_id;
    IF p.status <> 'open' THEN
        RAISE EXCEPTION 'AgriLedger: pool % is not open for sale', NEW.pool_id
            USING ERRCODE = 'check_violation';
    END IF;
    SELECT coalesce((SELECT sum(quantity_g) FROM contribution WHERE pool_id = NEW.pool_id), 0)
         - coalesce((SELECT sum(quantity_g) FROM sale WHERE pool_id = NEW.pool_id), 0)
      INTO remaining;
    IF NEW.quantity_g < p.min_order_g AND NEW.quantity_g <> remaining THEN
        RAISE EXCEPTION 'AgriLedger: sale of % g is below the minimum order of % g', NEW.quantity_g, p.min_order_g
            USING ERRCODE = 'check_violation';
    END IF;
    RETURN NEW;
END $$;
CREATE TRIGGER sale_rules BEFORE INSERT ON sale
    FOR EACH ROW EXECUTE FUNCTION sale_rules();

-- (c) A pool_change row must belong to a PRICE_CHANGED / STATUS_CHANGED event of the same kind.
CREATE FUNCTION pool_change_matches_event() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE et TEXT;
BEGIN
    SELECT event_type INTO et FROM event WHERE event_id = NEW.event_id;
    IF (NEW.field = 'PRICE'  AND et <> 'PRICE_CHANGED')
    OR (NEW.field = 'STATUS' AND et <> 'STATUS_CHANGED') THEN
        RAISE EXCEPTION 'AgriLedger: pool_change field % does not match event type %', NEW.field, et
            USING ERRCODE = 'check_violation';
    END IF;
    RETURN NEW;
END $$;
CREATE TRIGGER pool_change_matches_event BEFORE INSERT ON pool_change
    FOR EACH ROW EXECUTE FUNCTION pool_change_matches_event();

-- ---------------------------------------------------------------------
-- 8. Application role: least privilege (second line of defence)
-- ---------------------------------------------------------------------
DO $$ BEGIN                            -- roles are cluster-wide, so create it only once
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'agriledger_app') THEN
        CREATE ROLE agriledger_app NOLOGIN; -- create a LOGIN user and GRANT agriledger_app to it
    END IF;
END $$;

GRANT SELECT, INSERT ON event, contribution, sale, payout, pool_photo, pool_change,
                        inventory_ledger, ownership_ledger, financial_ledger, ledger_entry
      TO agriledger_app;
GRANT SELECT, INSERT, UPDATE ON pool, pool_seller_balance,
                                app_user, pool_manager, seller, buyer
      TO agriledger_app;
GRANT SELECT, INSERT, DELETE ON session_token, seller_phone TO agriledger_app;
GRANT SELECT ON commodity, commodity_parameter, reference_price TO agriledger_app;   -- reference data is loaded by an admin

COMMIT;
