-- Worked example from the master document (§12): pool 600/800/600 kg, price ₹25.00/kg, sale 500 kg.
-- Runs as the restricted application role.  h(x) = fake 64-hex hash for schema testing only.
\set ON_ERROR_STOP 1
BEGIN;
INSERT INTO app_user(username,password_hash,user_type) VALUES
 ('mgr1','x','MANAGER'),('farmerA','x','FARMER'),('farmerB','x','FARMER'),('farmerC','x','FARMER'),('buyer1','x','BUYER');
INSERT INTO pool_manager(user_id,manager_name) VALUES (1,'Pool Manager 1');
INSERT INTO seller(seller_id,seller_name,village) VALUES (2,'Farmer A','Village 1'),(3,'Farmer B','Village 2'),(4,'Farmer C','Village 3');
INSERT INTO seller_phone VALUES (2,'+919800000001');
INSERT INTO buyer(buyer_id,buyer_name) VALUES (5,'Buyer 1');
COMMIT;

-- Pool creation (FR-01): draft pool, then (after contributions + photo) opened
BEGIN;
INSERT INTO pool(commodity_id,variety,range_no,packaging,storage_location,min_order_g,display_unit,price_paise_per_kg)
 VALUES ((SELECT commodity_id FROM commodity WHERE name='Paddy'),'Sona Masuri',1,'50 kg jute bags','Test godown, Hubballi',100000,'quintal',2500);
INSERT INTO event(pool_id,event_type,actor_user_id) VALUES (1,'POOL_CREATED',1);
INSERT INTO inventory_ledger(event_id,pool_id,movement_type,delta_g,resulting_stock_g) VALUES (1,1,'OPEN',0,0);
INSERT INTO ledger_entry(event_id,pool_id,sequence_no,previous_entry_id,previous_hash,entry_hash)
 VALUES (1,1,1,NULL,repeat('0',64),md5('e1')||md5('e1x'));
COMMIT;

-- Contributions (FR-02): A 600 kg, B 800 kg, C 600 kg
DO $$
DECLARE s BIGINT; q BIGINT; ev BIGINT; seq INT := 1; prev TEXT; prev_id BIGINT; st BIGINT;
BEGIN
  FOR s,q IN SELECT * FROM (VALUES (2,600000),(3,800000),(4,600000)) v(a,b) LOOP
    PERFORM 1 FROM pool WHERE pool_id=1 FOR UPDATE;
    INSERT INTO event(pool_id,event_type,actor_user_id) VALUES (1,'CONTRIBUTION',1) RETURNING event_id INTO ev;
    INSERT INTO contribution(event_id,pool_id,seller_id,quantity_g) VALUES (ev,1,s,q);
    INSERT INTO pool_seller_balance VALUES (1,s,q)
      ON CONFLICT (pool_id,seller_id) DO UPDATE SET remaining_g = pool_seller_balance.remaining_g + EXCLUDED.remaining_g;
    UPDATE pool SET total_stock_g = total_stock_g + q WHERE pool_id=1 RETURNING total_stock_g INTO st;
    INSERT INTO inventory_ledger(event_id,pool_id,movement_type,delta_g,resulting_stock_g) VALUES (ev,1,'IN',q,st);
    INSERT INTO ownership_ledger(event_id,pool_id,seller_id,delta_g,resulting_balance_g)
      SELECT ev,1,s,q,remaining_g FROM pool_seller_balance WHERE pool_id=1 AND seller_id=s;
    SELECT entry_hash, ledger_entry_id INTO prev, prev_id FROM ledger_entry WHERE pool_id=1 ORDER BY sequence_no DESC LIMIT 1;
    seq := seq + 1;
    INSERT INTO ledger_entry(event_id,pool_id,sequence_no,previous_entry_id,previous_hash,entry_hash)
      VALUES (ev,1,seq,prev_id,prev,md5('e'||ev)||md5('x'||ev));
  END LOOP;
END $$;

-- Photo, then open the pool (STATUS_CHANGED event sealed in the chain, event 5 / sequence 5)
BEGIN;
INSERT INTO pool_photo(pool_id,file_path,file_sha256,uploaded_by) VALUES (1,'photos/p1_1.jpg',md5('p')||md5('q'),1);
INSERT INTO event(pool_id,event_type,actor_user_id) VALUES (1,'STATUS_CHANGED',1);
INSERT INTO pool_change(event_id,pool_id,field,old_value,new_value,changed_by) VALUES (5,1,'STATUS','draft','open',1);
UPDATE pool SET status='open' WHERE pool_id=1;
INSERT INTO ledger_entry(event_id,pool_id,sequence_no,previous_entry_id,previous_hash,entry_hash)
  SELECT 5,1,5,ledger_entry_id,entry_hash,md5('e5')||md5('x5') FROM ledger_entry WHERE pool_id=1 AND sequence_no=4;
COMMIT;

-- Sale of 500 kg to Buyer 1 (FR-04/05/09): gross 1,250,000 paise; shares 150/200/150 kg; payouts 375,000/500,000/375,000
BEGIN;
SELECT total_stock_g FROM pool WHERE pool_id=1 FOR UPDATE;
SELECT seller_id, remaining_g FROM pool_seller_balance WHERE pool_id=1 ORDER BY seller_id FOR UPDATE;
INSERT INTO event(pool_id,event_type,actor_user_id) VALUES (1,'SALE',5);   -- event 6
INSERT INTO sale(event_id,pool_id,buyer_id,request_id,quantity_g,unit_price_paise_per_kg,gross_paise) VALUES (6,1,5,gen_random_uuid(),500000,2500,1250000);
INSERT INTO payout VALUES (1,2,150000,375000),(1,3,200000,500000),(1,4,150000,375000);
UPDATE pool_seller_balance SET remaining_g = remaining_g - p.quantity_allocated_g
  FROM payout p WHERE p.sale_id=1 AND pool_seller_balance.pool_id=1 AND pool_seller_balance.seller_id=p.seller_id;
UPDATE pool SET total_stock_g = total_stock_g - 500000 WHERE pool_id=1;
INSERT INTO inventory_ledger(event_id,pool_id,movement_type,delta_g,resulting_stock_g) VALUES (6,1,'OUT',-500000,1500000);
INSERT INTO ownership_ledger(event_id,pool_id,seller_id,delta_g,resulting_balance_g)
  SELECT 6,1,b.seller_id,-p.quantity_allocated_g,b.remaining_g FROM payout p JOIN pool_seller_balance b
    ON b.pool_id=1 AND b.seller_id=p.seller_id WHERE p.sale_id=1;
INSERT INTO financial_ledger(event_id,pool_id,seller_id,entry_type,amount_paise) VALUES (6,1,NULL,'SALE_GROSS',1250000);
INSERT INTO financial_ledger(event_id,pool_id,seller_id,entry_type,amount_paise)
  SELECT 6,1,seller_id,'SELLER_PAYOUT',payout_amount_paise FROM payout WHERE sale_id=1;
INSERT INTO ledger_entry(event_id,pool_id,sequence_no,previous_entry_id,previous_hash,entry_hash)
  SELECT 6,1,6,ledger_entry_id,entry_hash,md5('e6')||md5('x6') FROM ledger_entry WHERE pool_id=1 AND sequence_no=5;
COMMIT;

-- Reconciliation invariants (all must return 'OK')
SELECT 'INV-1' inv, CASE WHEN p.total_stock_g = (SELECT sum(remaining_g) FROM pool_seller_balance WHERE pool_id=p.pool_id) THEN 'OK' ELSE 'FAIL' END FROM pool p
UNION ALL
SELECT 'INV-2', CASE WHEN p.total_stock_g = (SELECT resulting_stock_g FROM inventory_ledger WHERE pool_id=p.pool_id ORDER BY inv_id DESC LIMIT 1) THEN 'OK' ELSE 'FAIL' END FROM pool p
UNION ALL
SELECT 'INV-3', CASE WHEN bool_and(b.remaining_g = (SELECT resulting_balance_g FROM ownership_ledger o WHERE o.pool_id=b.pool_id AND o.seller_id=b.seller_id ORDER BY own_id DESC LIMIT 1)) THEN 'OK' ELSE 'FAIL' END FROM pool_seller_balance b
UNION ALL
SELECT 'INV-4', CASE WHEN bool_and(s.quantity_g=(SELECT sum(quantity_allocated_g) FROM payout WHERE sale_id=s.sale_id) AND s.gross_paise=(SELECT sum(payout_amount_paise) FROM payout WHERE sale_id=s.sale_id)) THEN 'OK' ELSE 'FAIL' END FROM sale s
UNION ALL
SELECT 'INV-5', CASE WHEN p.total_stock_g >= 0 AND (SELECT coalesce(sum(quantity_g),0) FROM sale WHERE pool_id=p.pool_id) <= (SELECT sum(quantity_g) FROM contribution WHERE pool_id=p.pool_id) THEN 'OK' ELSE 'FAIL' END FROM pool p;

SELECT s.seller_name, b.remaining_g/1000 AS remaining_kg, round(100.0*b.remaining_g/p.total_stock_g,2) AS ownership_pct
FROM pool_seller_balance b JOIN seller s USING (seller_id) JOIN pool p USING (pool_id) ORDER BY seller_id;
