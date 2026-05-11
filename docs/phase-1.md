# Phase 1

## Scope
- Protocol-agnostic core types (`types.hpp`, `time.hpp`, `crc32.hpp`)
- Branchless big-endian decode helper (`read_be<T>`)
- NASDAQ ITCH 5.0 parser for major message types (`A/F/E/C/X/D/U/P/Q/I/S/R`)
- IEX DEEP+ parser path (core subset implemented against v1.02 spec)
- Cboe PITCH core message parsing (`A/d/1`, `E`, `X`, `P/r/2`)
- Compile-time wire-layout guards (`static_assert(sizeof(...))`)
- Validation harness for type counts + canonical CRC32 on normalized stream
- Fuzz harnesses wired for NASDAQ, IEX, and Cboe parsers

## Methodology
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- Build check: `cmake --build build -j`
- Validate all 3 feeds:
  - `./build/phase1_parser_validate <nasdaq_itch_file> <iex_deep_file> [cboe_pitch_file]`
- Precheck candidate files for binary-vs-text mismatch:
  - `powershell -ExecutionPolicy Bypass -File scripts/precheck_feed_files.ps1 -Files <file1>,<file2>,<file3>`
- Validate NASDAQ+IEX immediately from downloaded `.gz` samples when format-compatible:
  - `powershell -ExecutionPolicy Bypass -File scripts/run_phase1_validate.ps1`
- Benchmark script target: `scripts/run_phase1_bench.sh` (single isolated core)

## Current Status
- Implemented: parser framework + three protocol parsers + fuzz wiring + multi-feed validator CLI.
- Data acquired in-repo:
  - NASDAQ sample: `data/raw/nasdaq/tvagg.gz`
  - IEX/NYSE/Cboe: pending wire-format-compatible captures for full 3-venue replay
- Venue swap note:
  - NYSE Pillar was replaced with IEX DEEP for this project because free publicly downloadable NYSE historical files are TAQ reconstructions (text/CSV) rather than Pillar wire-format binary captures.
  - IEX DEEP+ provides public historical `pcap.gz` binary captures and an official spec, making replay-based validation feasible without exchange licensing cost.
- Cboe historical sample status:
  - Cboe PITCH historical downloads are license-gated/commercial on DataShop for full datasets.
  - Parser implementation is spec-based and fuzz-wired, but full historical replay validation is pending licensed sample access.
- Pending for Phase 1 sign-off:
  - Run sanity validation on NASDAQ binary replay sample and capture message counts/CRC outputs.
  - Complete IEX DEEP spec-accurate message decoding for the v1 subset and validate on historical PCAP-derived messages.
  - Run Cboe historical replay validation once licensed sample data is available.

## Replay Findings (Current)
- Input precheck (`scripts/precheck_feed_files.ps1`) results:
  - `data/raw/expanded/nasdaq.bin`: binary-like
  - `data/raw/expanded/nyse.bin`: text/csv-like (not wire-format binary)
  - `data/raw/expanded/nyse_openbook.bin`: binary-like (OPENBOOK, not Pillar)
  - `data/raw/expanded/iex.bin`: binary-like (IEXTP1/DEEP+ pcap payload carrier)
- NASDAQ replay run:
  - command: `phase1_parser_validate --nasdaq-only data/raw/expanded/nasdaq.bin`
  - frames=`27,798,910`, parsed=`2,003`, malformed=`27,796,907`, crc32=`0x08158a87`
  - interpretation: parser and binary-file ingestion execute, but this sample file's framing/container does not match the validator's current framed-message assumption. A format adapter is required for true end-to-end decode rates.
- IEX replay run:
  - command: `phase1_parser_validate data/raw/expanded/nasdaq.bin data/raw/expanded/iex.bin`
  - iex frames=`59,367`, parsed=`59,367`, malformed=`0`, crc32=`0xc58c75ae`
  - interpretation: IEX-TP/PCAP replay adapter and DEEP+ subset parser are functioning on real historical sample data.
- IEX determinism run (100 replays):
  - command: `powershell -ExecutionPolicy Bypass -File scripts/run_determinism_iex.ps1 -Runs 100`
  - result: `unique_crc_count=1`, `deterministic=true`, stable crc=`0xc58c75ae`
  - interpretation: for the current IEX DEEP+ subset and sample, replay output is bit-stable across 100 runs.
- IEX replay throughput snapshot (Windows build, single-process replay):
  - command: `powershell -ExecutionPolicy Bypass -File scripts/bench_iex_replay.ps1 -Runs 10`
  - frames/run=`59,367`
  - avg=`99.606 ms`, p50=`99.151 ms`, p99=`101.687 ms`
  - avg throughput=`596,019.15 msgs/sec`

## Notes
- Exchange totals are preferred where published; otherwise use checked-in manifest counts + sha256 under `data/manifests/`.
- Each `BookEvent` carries both `exchange_ts_ns` and `ingest_ts_ns`.
- Cboe PITCH uses fixed-length ASCII field parsing (including base36 IDs), not binary integer fields.
- This repository does not include redistributed proprietary exchange payload files; only locally downloaded samples are used for validation.

## Phase 1 Checkpoint
- Validated now:
  - IEX DEEP+ core parser subset (`a/M/R/L/T/S/E`) on real historical `pcap.gz` sample.
  - IEX replay determinism across 100 runs (single CRC value).
  - IEX replay throughput snapshot script and baseline numbers.
  - Parser fixture tests for DEEP+ core message decode semantics.
- Implemented but blocked on data/source compatibility:
  - NASDAQ ITCH parser exists, but currently downloaded `tvagg.gz` sample does not match the expected replay framing/message taxonomy for full decode validation.
  - Cboe parser exists, but historical replay is blocked by paid data licensing.
- Safe to start in Phase 2 immediately:
  - Build arbitration/recovery framework against NASDAQ + IEX interfaces.
  - Keep Cboe as pluggable path pending licensed sample data.
  - Phase 2 kickoff now includes a compiled sequence/gap tracker framework module with dedicated tests.
- Current state update:
  - Phase 2 framework is now implemented in-repo: sequence tracking, gap handling, recovery simulation, deterministic merge, JIT bridge sink, and benchmark harness.
  - This does not remove the known external validation constraints: NASDAQ sample framing mismatch and Cboe licensed historical replay access.
- Must stay explicit in README/interviews:
  - NYSE Pillar was swapped to IEX DEEP+ due to binary-data availability.
  - Cboe historical replay remains a licensing-gated validation item.
