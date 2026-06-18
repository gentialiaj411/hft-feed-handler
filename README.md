# MultiFeed

[![ci](https://github.com/gentialiaj411/hft-feed-handler/actions/workflows/ci.yml/badge.svg)](https://github.com/gentialiaj411/hft-feed-handler/actions/workflows/ci.yml)

**A C++20 recorded market-data pipeline and replay service:** four venue parsers, deterministic CRC-stable replay over 368M events, cross-venue NBBO journals, microstructure features, simulated OFI backtesting with block-bootstrap significance — plus a read-only gRPC query API over recorded journals. Not live exchange connectivity.

Bytes come off recorded tapes (NASDAQ ITCH 5.0, IEX DEEP+, Cboe PITCH, CME MDP 3.0 SBE). They are parsed into one canonical `BookEvent` stream, sequenced and merged deterministically, used to maintain order books and NBBO, fed through a feature pipeline, and optionally served back out over gRPC. The same recorded input produces byte-identical output across reruns. That is the quality bar.

## How the data flows

```
   raw venue bytes (ITCH / DEEP+ / PITCH / MDP3 SBE)
           │
           ▼   venue parsers
       BookEvent          ← canonical; downstream stages ignore wire format
           │
           ▼
   sequence tracking → gap recovery → A/B arbitration →
   deterministic cross-venue merge
           │
           ▼
   per-venue order books → consolidated NBBO → microstructure features
           │              │
           │              ▼
           │     NBBO journal (per-side venue + sequence provenance)
           ▼
   sim matching → PnL → OFI backtest (held-out split + block-bootstrap)
           │
           ▼
   gRPC TickReplayService (StreamTicks / QueryNbbo / StreamNbbo)  ← recorded replay only
```

## Headline results

| Claim | Number | Evidence |
|---|---|---|
| Full-day NASDAQ ITCH replay, CRC-stable across reruns | 368 M events → `merged_crc = 0xa5dd7c07` | [`AUDIT_LOG.md`](AUDIT_LOG.md) |
| Cross-venue NBBO journal, byte-identical across reruns | 5 M events → 75,808 NBBO events, `journal_crc = 0x147dcdae` | [`bench/results/nbbo_replay.md`](bench/results/nbbo_replay.md) |
| CME MDP3 (SBE) parser on cert sample, CRC stable | 213,887 packets → 2.3 M events, `crc = 0x22b18b64` | [`bench/results/mdp3_replay.md`](bench/results/mdp3_replay.md) |
| ITCH parser AVX2 vs scalar throughput | 36.2 M msg/s SIMD vs 24.9 M msg/s scalar (**1.45×**) | [`bench/results/decoder_simd_20260522T022020Z.md`](bench/results/decoder_simd_20260522T022020Z.md) |
| OFI backtest, 30% exchange-time holdout | Sharpe ≈ 0.103; block-bootstrap **p < 1e-3** (0/2000 resamples ≤ 0, n=927 equity deltas); simulated fills | [`bench/results/ofi_backtest.md`](bench/results/ofi_backtest.md) |
| Alpha research lab (13 signals, purged 5-fold CV) | Top OOS IC ≈ 0.038 (`effective_spread`); OFI failed OOS; 195 trials logged | [`bench/results/alpha_lab/tearsheet.md`](bench/results/alpha_lab/tearsheet.md) · [`docs/alpha_lab/methodology.md`](docs/alpha_lab/methodology.md) |
| Feed hot-path latency *(WSL2-bounded)* | p50 23 ns, p99 36 ns, p99.9 142 ns | [`bench/results/`](bench/results/) |
| Recorded replay/query service (gRPC) | `StreamTicks`, `QueryNbbo`, `StreamNbbo`, `Health` over mmap'd journals | [`docs/replay_service.md`](docs/replay_service.md) |

**OFI caveat (read this once):** bootstrap rejects "true mean ≤ 0" on this single-day 5M NASDAQ holdout, but Sharpe is small, fills are simulated, and there is no latency or adverse-selection model. Prior docs cited **p = 1.0** because `block_bootstrap_mean_pvalue` returned the complement; fixed 2026-05-26 ([`AUDIT_LOG.md`](AUDIT_LOG.md)). The deliverable is closed-loop backtest infrastructure with honest statistics, not a tradeable alpha claim.

### Pipeline latency (WSL2, taskset -c 2, 2M synthetic events)

![latency percentiles](docs/images/latency_percentiles.png)

p99 delta across two independent runs is under 3%. Stability is the claim; absolute numbers will be lower on tuned bare-metal Linux.

### OFI equity curve (held-out window, simulated fills)

![ofi equity curve](docs/images/ofi_equity_curve.png)

## What's in the repo

| Surface | Summary |
|---|---|
| **Parsers** | NASDAQ ITCH 5.0 (scalar + AVX2), IEX DEEP+, Cboe PITCH, CME MDP3 SBE (template 32). libFuzzer smoke in CI for the three equity parsers. |
| **Phase 2** | Sequence tracking, gap recovery, A/B arbitration (CRC stable under 50%+ drop injection), deterministic merge. |
| **Phase 3** | Order books, NBBO consolidator + journal, six microstructure features (microprice, OFI, queue-ahead, effective-spread EMA, Kyle's lambda, VPIN). |
| **Phase 4** | Sim matching, PnL accountant, OFI strategy, backtest runner. |
| **Replay service** | gRPC `TickReplayService` — read-only over recorded book/NBBO journals ([`docs/replay_service.md`](docs/replay_service.md)). |
| **Transport** | UDP multicast receiver; AF_XDP loopback code (BPF compiles; bench blocked on WSL2). |
| **Lock-free** | SPSC ring; SPMC seqlock ring with torn-read detection. |
| **CI** | GCC + Clang matrix, ASAN+UBSAN, TSAN on lock-free paths, Windows MSVC smoke ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)). |

53 CTest targets, 30+ tools/benches, ~17k lines of C++20. Sanitizer-local harnesses: [`scripts/local_run_sanitized.sh`](scripts/local_run_sanitized.sh), [`scripts/local_run_tsan.sh`](scripts/local_run_tsan.sh).

## Quick start

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

ctest --test-dir build --output-on-failure -L parser
ctest --test-dir build --output-on-failure -L phase2

./build/journal_replay --mode pipeline-crc \
  --in bench/data/itch_20190130_5m_for_ofi.journal

./build/nbbo_replay_bench \
  --in bench/data/itch_20190130_5m_for_ofi.journal \
  --out-md bench/results/nbbo_replay.md

./build/ofi_backtest \
  --journal bench/data/itch_20190130_5m_for_ofi.journal

# Alpha research lab (Linux/WSL): purged CV, signal zoo, tearsheet
bash scripts/run_alpha_lab.sh
ctest --test-dir build -L alpha_lab
```

### Replay service (Linux/WSL, optional gRPC)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMF_BUILD_REPLAY_SERVICE=ON
cmake --build build -j --target tick_service_server nbbo_replay_bench

./build/nbbo_replay_bench \
  --in bench/data/itch_20190130_5m_for_ofi.journal \
  --out-journal /tmp/mf_nbbo_bench.journal

./build/tick_service_server \
  --journal bench/data/itch_20190130_5m_for_ofi.journal \
  --nbbo /tmp/mf_nbbo_bench.journal \
  --listen 127.0.0.1:50051

python3 -m venv .venv && . .venv/bin/activate
pip install -r python/requirements.txt && bash scripts/gen_tick_proto_py.sh
python python/tick_client.py health
python python/tick_client.py stream --limit 1000
```

## Honest scope and limits

- **Offline only.** No live exchange connectivity.
- **Recorded tapes:** full-day NASDAQ ITCH 2019-01-30 (~11 GB, 368 M events); CME MDP3 cert sample. IEX/Cboe parsers are unit-tested and fuzzed but not exercised on real venue recordings.
- **OFI:** simulated maker/taker fills; single-day 5M holdout; see [`bench/results/ofi_backtest.md`](bench/results/ofi_backtest.md).
- **Latency artifacts:** WSL2-bounded; determinism claims are host-independent.
- **AF_XDP:** code complete; loopback bench needs bare-metal Linux ([`docs/runbook-afxdp.md`](docs/runbook-afxdp.md)).

Every claim is tracked in [`CLAIMS_MATRIX.md`](CLAIMS_MATRIX.md) with evidence, risk level, and verification command.

## Documentation map

| Doc | Use when |
|---|---|
| [`WALKTHROUGH.md`](WALKTHROUGH.md) | Interview prep — design choices, determinism, bug stories |
| [`docs/architecture.md`](docs/architecture.md) | System shape and layer boundaries |
| [`docs/replay_service.md`](docs/replay_service.md) | gRPC replay/query service |
| [`docs/bug_stories.md`](docs/bug_stories.md) | Sanitizer CI bugs (interview retrieval) |
| [`CLAIMS_MATRIX.md`](CLAIMS_MATRIX.md) | What is verified vs open |
| [`RESUME_CLAIMS.md`](RESUME_CLAIMS.md) | Resume bullets and caveats |
| [`AUDIT_LOG.md`](AUDIT_LOG.md) | Dated change log with root causes |
| [`IMPROVEMENT_GUIDE.md`](IMPROVEMENT_GUIDE.md) | Remaining upgrade tracks |

## Acknowledgements

NASDAQ ITCH 5.0: NASDAQ public documentation. CME MDP 3.0: CME public packet capture dataset. Sources: [`docs/protocol-sources.md`](docs/protocol-sources.md).
