# Protocol Sources (Pinned for Phase 1)

This project uses exchange-published primary documentation as the protocol source of truth.

## NASDAQ TotalView-ITCH 5.0
- Spec PDF (official): https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf
- Status in repo: parser scaffold implemented for major message types A/F/E/C/X/D/U/P/Q/I/S/R.

## IEX DEEP
- Market data resources (official): https://www.iex.io/resources/trading/market-data
- DEEP specification landing page (official): https://www.iex.io/documents/deep-v1-08
- IEX transport protocol landing page (official): https://www.iex.io/documents/iex-tp-v1
- Historical data overview (official): https://iextrading.com/trading/market-data/index.html
- Notes:
  - IEX historical data is distributed as `pcap.gz` captures over IEX-TP.
  - File naming includes protocol and spec version (e.g. `..._IEXTP1_DEEP1.0.pcap.gz`).

## IEX DEEP+ (Pinned for v1 parser path)
- DEEP+ specification page (official): https://www.iex.io/documents/iex-deep-plus-specification
- DEEP+ specification file used locally:
  - URL: https://cdn.prod.website-files.com/696f8ac812dcabe749e3aa49/696f8ac912dcabe749e3d507_67882e431903bd5d34d451ff_IEX%2520DEEP%252B%2520Specification%2520v1.02.pdf
  - SHA256: `321981AE9542F02136021FBD9E23A3EE094B99D3AD26F005EF29BD408949001B`
- Public DEEP+ historical sample file used locally:
  - URL: https://storage.googleapis.com/iex/data/feeds/20241001/20241001_IEXTP1_DPLS1.0.pcap.gz
  - SHA256: `91BAD3A0DEA84017C0516B8E22CAAF48BBDA6C87421FED4912BBA81F02B2F5DF`

## Cboe U.S. Equities PITCH
- Technical docs index (official): https://ww2.cboe.com/us/equities/support/technical/
- PITCH specification landing page (official): https://datashop.cboe.com/cboe-us-equities-pitch
- Status in repo: core message parsing implemented for Add (`A/d/1`), Execute (`E`), Cancel (`X`), Trade (`P/r/2`).

## Implementation Policy
1. Only implement wire offsets and message-type IDs directly from exchange-published docs.
2. If docs and sample captures disagree, record the discrepancy in `AUDIT_LOG.md`.
3. Do not treat third-party summaries as normative.
