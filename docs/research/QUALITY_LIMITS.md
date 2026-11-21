# Quality limits for the pool commodities: research notes (21 Nov)

**Status: NOT VERIFIED. Nothing here has been loaded into the database.** `db/seed_reference.sql` still holds only paddy Grade 1,
with `verified = false`. A teammate must check each number against the current official document before it is loaded.

## What was found, and how reliable it is
The figures below come from web search summaries of official pages (WDRA and e-NAM). I could not open the PDFs themselves
(the PDF text could not be extracted here), so treat every number as a lead to check, not a fact.

| Commodity | What the sources say (max, % by weight unless stated) | Agreement |
|---|---|---|
| **Paddy** | Moisture 17 / 18 / 20; admixture 10 / 15 / 20; immature and shrivelled grains 3 / 5 / 7; foreign matter 2 / 4 / 6; damaged, discoloured and weevilled grains 4 / 6 / 8 (Grade 1 / 2 / 3) | Matches what we already seed for Grade 1. One other e-NAM table (labelled rice) shows different bands, so confirm which applies to paddy. |
| **Maize** | WDRA/Agmark: moisture 10 / 12 / 16 and foreign matter 0.1 / 0.5 / 0.75 (special / standard / general). e-NAM: moisture up to 12.0 (Range I), 12.1 to 13.5 (II), 13.6 to 14.0 (III); foreign matter up to 0.10 in Range I | **Conflict** between WDRA and e-NAM. Needs a decision on which document the project follows. |
| **Groundnut with pods** | Moisture 8 / 10 / 12; foreign matter 1 to 3; damaged pods 2 to 5 (three grades) | Partial; the table was garbled in the summary. |
| **Tur (whole)** | Special grade: moisture 11, foreign matter 0.10, other edible grains 0.05, damaged grains 0.2, broken grains 0.10, weevilled none. A split-tur table also exists with looser limits | Only one grade seen; the split-tur table is a different product. We sell tur dal, so confirm whether whole or split applies. |

## Open questions for the team
1. Which document does the project follow: **e-NAM** (Range I to III bands) or **WDRA/Agmark** (grade names)? They differ for maize.
2. "Tur dal" in the app: whole tur (arhar whole) or split? The limits differ.
3. Grade 1 / 2 / 3 in the app maps to e-NAM Range I / II / III.

## How to finish this (no guessing)
1. Open the official PDFs: e-NAM commodity quality parameters (enam.gov.in, "Quality Parameters 193 commodities") and the WDRA sheets
   (wdra.gov.in: paddy_enam, maize-agmark, groundnutpods_enam).
2. Copy each parameter and its Range I / II / III maximum into `db/seed_reference.sql` with the source and date.
3. Set `verified = true` only after a second person has compared the numbers. The screens already show a limit only when it exists
   and show the "check against the official document" line until `verified` is true.

## Sources
- WDRA paddy (e-NAM): https://wdra.gov.in/documents/d/wdra/paddy_enam
- WDRA maize (Agmark): https://wdra.gov.in/documents/d/wdra/maize-agmark
- WDRA groundnut pods (e-NAM): https://wdra.gov.in/documents/d/wdra/groundnutpods_enam
- e-NAM commodity quality: https://enam.gov.in/commodity/commodity-quality
- e-NAM parameters document: https://enam.gov.in/web/docs/Quality_Parameters_193_commodities.pdf
