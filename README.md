# MultiFeed — Multi-Venue HFT Market Data Handler

MultiFeed is a C++20 portfolio project that models a production-style market data pipeline across three venues without using live feeds. It focuses on deterministic replay, recovery behavior, and microstructure feature extraction under high-throughput constraints.

## Architecture

```text
[ITCH 5.0 / IEX DEEP+ / Cboe PITCH parsers]
                    ->
[Phase 2: det. merge + gap recovery + A/B arbiter]
                    ->
[Phase 3: order book + NBBO + feature engine]
                    ->
[SPSCRingBuffer<FeatureVector>]
```

## Technical Highlights

- 3-venue binary protocol parsing: NASDAQ ITCH 5.0, IEX DEEP+, and Cboe PITCH.
- Deterministic merge sort keyed on `(exchange_ts_ns, venue, sequence, raw_type)` with CRC32 invariance checks.
- Per-venue sequence tracking with bounded gap buffering and `force_advance` on `GapTooLarge`.
- A/B dual-feed arbitration with configurable drop injection and CRC comparison.
- Per-symbol order book with L1 aggregation feeding NBBO consolidation.
- Six microstructure features:
  - microprice
  - OFI rolling window
  - queue-ahead estimation
  - effective spread EMA
  - Kyle's lambda online OLS
  - VPIN
- Lock-free `SPSCRingBuffer` with cache-line-separated atomics.
- `libFuzzer` targets for all three parsers.

## Build (Linux/WSL2)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Test & CI

GitHub Actions workflow: `phase2-linux.yml`

Pipeline stages:
- build
- unit tests
- bench snapshot
- determinism regression
- A/B evidence (independent + complementary)

Local WSL2 test run:

```bash
bash scripts/run_phase2_tests_wsl.sh
```

## Validation Scripts

- `scripts/validate_feature_math.py`: independent Python math harness for all 6 feature formulas with 14 checks.
- `scripts/summarize_phase2_artifact.py`: summarize phase2 artifact output.
- `scripts/generate_phase2_report.py`: generate consolidated phase2 evidence reports.

## Key Design Decisions

- CRC32 is used as a compact determinism witness so large replay outputs can be compared quickly and reproducibly across runs.
- Gap recovery uses a bounded deque store to cap memory growth while preserving near-term out-of-order recovery opportunity.
- `force_advance` on `GapTooLarge` prevents venue progress deadlock and keeps the merged timeline moving under sustained loss.

## Performance Evidence

Numbers below are from a single pinned core (`taskset -c 0`), Release build, WSL2 on Intel Core Ultra 9 275HX (24-core, ~16 GB RAM). `ticks_per_ns=1.0` means the bench fell back to `steady_clock` on WSL2; treat these as wall-ns upper bounds, not TSC-calibrated.

### Feed hot-path (sequence tracking + SPSC ring, 2 M events)

| Percentile | Latency |
|---|---|
| p50 | 79 ns |
| p99 | 111 ns |
| p99.9 | 246 ns |

### Phase 3 feature engine (order book → NBBO → 6 features → ring publish, 1 M events)

| Percentile | Latency |
|---|---|
| p50 | 431 ns |
| p99 | 814 ns |
| p99.9 | 11 111 ns |

To reproduce:

```bash
taskset -c 0 ./build/feed_hot_path_bench --events 2000000
taskset -c 0 ./build/phase3_feature_latency_bench --events 1000000
```
