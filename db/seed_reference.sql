-- Reference data seed (run as an admin, not as the application role).
-- ONLY paddy Range 1 values are filled in so far (as shown on the wireframes).
-- Every other limit is NULL and verified = FALSE until a teammate copies the numbers
-- from the CURRENT official e-NAM / DMI trade-parameter document.
\set ON_ERROR_STOP 1
BEGIN;
INSERT INTO commodity(name) VALUES ('Paddy'),('Maize'),('Tur dal'),('Groundnut with pods');

INSERT INTO commodity_parameter(commodity_id,parameter_name,range1_max,source,source_date,verified)
SELECT commodity_id, v.p, v.r1, 'collected from web sources; check against current e-NAM/DMI document', DATE '2026-11-19', FALSE
FROM commodity, (VALUES ('Moisture',17),('Admixture',10),('Immature and shrivelled grains',3),
                        ('Foreign matter',2),('Damaged or weevilled grains',4)) AS v(p,r1)
WHERE name = 'Paddy';

-- parameter NAMES for the other crops (limits still to be filled and verified)
INSERT INTO commodity_parameter(commodity_id,parameter_name)
SELECT commodity_id, p FROM commodity, LATERAL unnest(
  CASE name
    WHEN 'Maize'               THEN ARRAY['Moisture','Foreign matter','Admixture / other edible grains']
    WHEN 'Tur dal'             THEN ARRAY['Moisture','Foreign matter','Other edible grains','Damaged grains','Weevilled grains']
    WHEN 'Groundnut with pods' THEN ARRAY['Moisture','Foreign matter','Damaged pods','Pods of other varieties']
    ELSE ARRAY[]::text[] END) AS p;

-- Government support price (MSP) for common paddy, 2026-27: Rs 2,441 per quintal = 244100 paise
INSERT INTO reference_price(commodity_id,kind,price_paise_per_quintal,label,as_of)
SELECT commodity_id,'MSP',244100,'MSP 2026-27 (common paddy)',DATE '2026-11-19' FROM commodity WHERE name='Paddy';
COMMIT;
