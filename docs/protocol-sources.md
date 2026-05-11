# Protocol Sources (Pinned for Phase 1)

This project uses exchange-published primary documentation as the protocol source of truth.

## NASDAQ TotalView-ITCH 5.0
- Spec PDF (official): https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf
- Status in repo: parser scaffold implemented for major message types A/F/E/C/X/D/U/P/Q/I/S/R.

## NYSE Pillar Integrated Feed
- Spec PDF (official): https://www.nyse.com/publicdocs/nyse/data/NYSE_Pillar_Integrated_Feed_Client_Specification.pdf
- Versioned snapshot seen: v2.5 family (check cover page at implementation time).
- Status in repo: parser currently scaffolded and NOT yet field-accurate.

## Cboe U.S. Equities PITCH
- Technical docs index (official): https://ww2.cboe.com/us/equities/support/technical/
- PITCH specification landing page (official): https://datashop.cboe.com/cboe-us-equities-pitch
- Status in repo: parser currently scaffolded and NOT yet field-accurate.

## Implementation Policy
1. Only implement wire offsets and message-type IDs directly from exchange-published docs.
2. If docs and sample captures disagree, record the discrepancy in docs/phase-1.md.
3. Do not treat third-party summaries as normative.
