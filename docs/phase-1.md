# Phase 1

## Scope
- Protocol-agnostic core types (`types.hpp`, `time.hpp`, `crc32.hpp`)
- Branchless big-endian decode helper (`read_be<T>`)
- NASDAQ ITCH 5.0 parser scaffold for major message types
- Compile-time wire-layout guards (`static_assert(sizeof(...))`)
- Validation harness for type counts + canonical CRC32 on normalized stream
- Fuzz harness directory scaffolding under `tests/fuzz/`

## Methodology (to run)
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- Validate: `./build/phase1_parser_validate <itch_file>`
- Benchmark script target: `scripts/run_phase1_bench.sh` (single isolated core)

## Notes
- Exchange totals are preferred where published; otherwise use checked-in manifest counts + sha256 under `data/manifests/`.
- Each `BookEvent` carries both `exchange_ts_ns` and `ingest_ts_ns`.