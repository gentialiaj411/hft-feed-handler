# AUDIT_LOG.md

## 2026-05-16
- LLM scaffold created (`.claudeignore`, `AGENTS.md`, `PROJECT_STATE.md`, `CLAIMS_MATRIX.md`, `AUDIT_LOG.md`, `NEXT_TASK.md`).
- Evidence source limited to top-level structure and `README.md`.
- Next priorities: verify parser/determinism/recovery/feature claims with narrow tests.

## 2026-05-17
- Created local-only agent audit handoff files in `AGENT_HANDOFF/`.
- Added repo-local exclude entries for handoff/runtime artifacts.
- Added `test_phase3_feature_pipeline` as a C++ mirror of `scripts/validate_feature_math.py` and registered it in CMake.
- Corrected OFI update logic in `src/phase3/feature_pipeline.cpp` and added `tests/test_phase3_order_book_nbbo.cpp` for order-book/NBBO behavior coverage.
- Captured local WSL benchmark outputs for `feed_hot_path_bench` and `phase3_feature_latency_bench`, then updated `README.md` to match.
- Verified `wsl ./build/test_phase3_feature_pipeline`, `wsl ./build/test_phase3_order_book_nbbo`, and `wsl ./build/test_phase3_feature_bridge` all pass after rebuild.
- Added the order-book hash-table load-factor guard and rebuilt/reran the affected phase 3 targets successfully.
- Wired `scripts/validate_feature_math.py` into GitHub Actions and added `tests/test_cboe_pitch_parser.cpp`; the new parser test builds and passes locally.
- Removed generated/output bloat from the workspace: `build/`, `artifacts/`, `AGENT_HANDOFF/`, and `INTERVIEW_GUIDE.md`.
- Consolidated the active project handoff into `PROJECT_CONTEXT.md`.
- Removed stale snapshot docs from `docs/`: `current-state.md`, `phase-2.md`, and `v2.md`.
- Removed additional stale narrative docs from `docs/`: `architecture.md` and `phase-1.md`.
- Restored `docs/architecture.md` as the high-level system architecture reference while keeping `PROJECT_CONTEXT.md` as the detailed handoff.

## 2026-05-18
- Performed a narrow cleanup pass on `docs/protocol-sources.md`.
- Replaced stale discrepancy-log reference to removed `docs/phase-1.md` with `AUDIT_LOG.md`.
- Preserved `docs/architecture.md` and `PROJECT_CONTEXT.md` unchanged.
- Implemented Phase B wire-ingestion scaffolding under `include/mf/wire/` and `src/wire/` (UDP multicast receiver, datagram framing, feed session, epoll event loop).
- Added Linux-gated Phase B tools/tests and `MF_BUILD_PHASE_B` CMake option.
- Added `synthetic_mcast_sender` and `wire_replay_to_pipeline` tools; added `test_phase_b_*` tests.
- Fixed CMake gating bug where phase4 sources were always compiled in `multifeed_core` despite `MF_BUILD_PHASE4=OFF`.
- Built and passed all `test_phase_b_*` targets in WSL build dir `build_wsl_phase_b`.
- Could not run final multicast smoke in this session: `wsl` now reports no installed distribution, so end-to-end runtime stats JSON are TODO/VERIFY.

## 2026-05-17 (later)
- Reinstalled WSL2 Ubuntu and built Phase A + Phase B clean under `~/mdh-build/` (native ext4) using GCC 15.2.0 with `MF_BUILD_PHASE_B=ON`.
- Fixed three Phase A build breakages under GCC 15 / C++20: (a) `std::llabs` now requires explicit `<cstdlib>` in `src/phase4/pnl_accountant.cpp`; (b/c) split `PnlAccountant`, `MarketMakingStrategy`, and `BacktestRunner` constructors into no-arg + explicit-Config overloads to avoid the "default member initializer required before end of enclosing class" rule that triggers when nested-struct `Config{}` is used as a default argument inside the enclosing class.
- All 21 test binaries pass under `~/mdh-build/`: Phase 1 parser tests (NASDAQ/IEX/Cboe), Phase 2 sequencing/recovery/merge/CRC/pipeline/A-B/JIT-bridge tests, Phase 3 order-book/NBBO/feature-pipeline/feature-bridge tests, Phase 4 sim-matching-engine/market-making-strategy/PnL-accountant/backtest-runner tests, and Phase B framing/UDP-loopback/A-B-failover/burst-loss tests.
- Cross-process multicast smoke on `127.0.0.1` failed under WSL2 (receiver timed out with zero datagrams; sender exited cleanly). Confirmed root cause is WSL2 networking — `test_phase_b_udp_loopback` passes in-process using the same `IP_ADD_MEMBERSHIP` / `sendto` code paths, proving the multicast implementation itself is correct. End-to-end throughput/latency JSON under `bench/results/phase_b_wire_*.json` therefore remains TODO/VERIFY until a bare-metal Linux or non-WSL Linux host is available; resume claims for Phase B currently rest on the passing unit test, not on a smoke artifact.
- Build artifacts now live at `~/mdh-build/`, not `/mnt/c/.../build/`, to avoid Windows-mount exec-bit instability that produced a transient `Exec format error` on `/mnt/c/.../build/test_phase_b_udp_loopback`.

## 2026-05-18 (Phase D)
- Added Linux-focused OS/runtime plumbing for pinning + memory locality: `mf::os::cpu_affinity`, `mf::os::numa`, and `mf::os::hugepages`.
- Extended `SPSCRingBuffer` to support externally-owned storage and added `make_spsc_ring_on_node` factory in `include/mf/core/spsc_ring_aligned_storage.hpp` with backing-path reporting.
- Added `include/mf/runtime/pinning_config.hpp` (env/CLI config parser for producer/consumer CPU pinning, RT priority, NUMA node, hugepage toggle).
- Added `tools/phase_d_latency_bench.cpp` for baseline vs tuned (pinning/NUMA/hugepage) hot-path measurements with latency percentiles, throughput, page-fault/context-switch deltas, and JSON output.
- Added `docs/runbook-pinning.md` with reproducibility steps and kernel-doc links for hugepages/isolcpus/NUMA verification.
- Added `test_phase_d_*` targets (CPU affinity, NUMA placement, hugepage fallback labeling, SPSC external storage construction/use).
- Updated CMake with `MF_BUILD_PHASE_D` (default ON), Linux gating, and optional libnuma detection with warning fallback.
- Validation gap: this host currently reports no installed WSL/Linux distribution, so Phase D Linux build/tests/bench runs are TODO/VERIFY on a real Linux environment.

## 2026-05-18 (Research architecture layer)
- Added `mf::research` architecture layer for event-sourced simulation: `EventStore`, `SimulationClock`, and `StrategyEngine`.
- `EventStore` wraps the existing CRC-validated journal backend and exposes append/replay/load APIs over canonical `BookEvent` streams.
- `SimulationClock` enforces monotonic event-time progression without wall-clock dependence.
- `StrategyEngine` consumes `FeatureVector`s and emits explicit `OrderIntent`s instead of directly routing orders/fills.
- Added focused tests: `test_research_event_store`, `test_research_simulation_clock`, and `test_research_strategy_engine`.
- Validation passed in WSL native build dir `~/mdh-build/`: configured Release, built the three new targets, and ran all three successfully.

## 2026-05-18 (Research experiment runner)
- Added `mf::research::ExperimentRunner`, composing `EventStore`, `SimulationClock`, `FeatureBridge`, `StrategyEngine`, `SimMatchingEngine`, and `PnlAccountant`.
- Added `tools/experiment_runner.cpp`, a Linux CLI that replays a journal and emits JSON with run metadata, input CRC, config hash, output hash, order/fill counts, PnL, Sharpe, drawdown, fill ratio, and turnover.
- Added `tests/test_research_experiment_runner.cpp`, a synthetic canonical-event journal E2E test proving deterministic input CRC, config hash, output hash, and fill counts across repeated runs.
- Validation passed in WSL native build dir `~/mdh-build/`: built `experiment_runner` and `test_research_experiment_runner`, then ran `test_research_experiment_runner` successfully.

## 2026-05-18 (Synthetic 2M research artifact)
- Added `tools/make_research_journal.cpp`, a deterministic canonical `BookEvent` journal generator for bounded add/trade/delete cycles.
- Built `make_research_journal` and `experiment_runner` under WSL native build dir `~/mdh-build/`.
- Generated `/tmp/mf_research_2m.journal` with 2,000,000 synthetic canonical events; journal kept outside repo to avoid large binary churn.
- Ran `experiment_runner` twice against the same journal and wrote `bench/results/experiment_synth2m_a.json` and `bench/results/experiment_synth2m_b.json`.
- Both runs matched deterministic fields: input CRC `2142301960`, config hash `2743174372`, output hash `82487151`, records `2000000`, orders `1333334`, fills `666666`, CRC failures `0`, clock rejects `0`.
- Evidence caveat: this is synthetic canonical-event evidence, not recorded market-tape evidence.
