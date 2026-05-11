# Phase 1

## Scope
- Protocol-agnostic core types (`types.hpp`, `time.hpp`, `crc32.hpp`)
- Branchless big-endian decode helper (`read_be<T>`)
- NASDAQ ITCH 5.0 parser for major message types (`A/F/E/C/X/D/U/P/Q/I/S/R`)
- NYSE Pillar Integrated core message parsing (`100/101/102/103/110`)
- Cboe PITCH core message parsing (`A/d/1`, `E`, `X`, `P/r/2`)
- Compile-time wire-layout guards (`static_assert(sizeof(...))`)
- Validation harness for type counts + canonical CRC32 on normalized stream
- Fuzz harnesses wired for NASDAQ, NYSE, and Cboe parsers

## Methodology
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- Build check: `cmake --build build -j`
- Validate all 3 feeds:
  - `./build/phase1_parser_validate <nasdaq_itch_file> <nyse_pillar_file> <cboe_pitch_file>`
- Benchmark script target: `scripts/run_phase1_bench.sh` (single isolated core)

## Current Status
- Implemented: parser framework + three protocol parsers + fuzz wiring + multi-feed validator CLI.
- Data acquired in-repo:
  - NASDAQ sample: `data/raw/nasdaq/tvagg.gz`
  - NYSE sample: `data/raw/nyse/EQY_US_NYSE_IBF_1_20260102.gz`
- Cboe historical sample status:
  - Cboe PITCH historical downloads are license-gated/commercial on DataShop for full datasets.
  - Parser implementation is spec-based and fuzz-wired, but full historical replay validation is pending licensed sample access.
- Pending for Phase 1 sign-off:
  - Run sanity validation on NASDAQ + NYSE sample files and capture message counts/CRC outputs.
  - Run Cboe historical replay validation once licensed sample data is available.

## Notes
- Exchange totals are preferred where published; otherwise use checked-in manifest counts + sha256 under `data/manifests/`.
- Each `BookEvent` carries both `exchange_ts_ns` and `ingest_ts_ns`.
- Cboe PITCH uses fixed-length ASCII field parsing (including base36 IDs), not binary integer fields.
- This repository does not include redistributed proprietary exchange payload files; only locally downloaded samples are used for validation.
