# Identity documents for sign-up: research notes (21 Nov)

**Status: PROPOSED.** The document lists in the app are a reasonable starting point, not an official checklist.

## What was found
- Farmer and trader registration is done at the APMC office; forms are available there and must be submitted with a list of documents.
  The exact list was not found online for Karnataka.
- On **e-NAM**, a farmer registers with KYC and the registration goes to the chosen APMC for approval. A trader needs a **trade licence
  from that APMC** plus KYC details and a photograph.
- Market licensing in Karnataka falls under the Karnataka Agricultural Produce Marketing (Regulation) Act, 1966.
- The searches did **not** confirm that land records (RTC), GST or PAN are required by APMCs. Ask the local APMC office.

## What the app does today
- Farmers choose one of: APMC farmer registration, land record (RTC), Aadhaar. Buyers choose one of: APMC trade licence, GST registration, PAN.
- Only the **type** and the **last four characters** are stored, plus who checked it and when. No card images and no full numbers.
- The Pool Manager checks the original in person, then presses Approve.

## Why not store card photos
Storing images or full numbers of identity documents (especially Aadhaar) brings legal duties (Aadhaar Act rules for private bodies, DPDP Act
2023 consent and minimisation) and a serious harm if leaked. For a college project, the check-and-record approach shows the same control.

## To confirm with the guide or a local APMC office
1. The real document list for farmers and for traders (adjust the two lists in `signup.js`, `manager-people.js`, E4 and E2 together).
2. Whether a bank account detail is needed for payouts (out of scope: no real payments).
3. Whether the Manager should be able to see and record a document number for traders (for example the licence number) in full.
