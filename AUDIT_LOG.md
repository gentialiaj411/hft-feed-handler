# AUDIT_LOG.md

## 2026-05-19 (Recorded ITCH replay unblock patch)
- Removed the `journal_replay` hard-fail for canonical event types greater than `Delete`; real NASDAQ ITCH conversion can legitimately emit `Replace`, `Trade`, `CrossTrade`, `Imbalance`, `System`, and `StockDirectory`.
- Added `tests/test_phase_c_mixed_event_replay.cpp`, which writes a journal containing book, trade, and metadata/control events, replays it through Phase 2, and exercises Phase 4 without rejecting those event types.
- Made `itch_to_journal` safer for repeat artifact generation: output files are overwritten by default, with explicit `--append` available when appending is intended.
- Expanded CTest registration across parser, phase2, phase3, phase4, phase B/C/D, phase E, and research targets where the targets exist.
- Expanded GitHub Actions CI beyond parser/phase2/one phase-C test to include phase3, phase4, phase C, research, phase E, and fuzz smoke. Phase B/D remain registered but not part of the default CI workflow because they are more environment-sensitive.
- Fixed `test_phase_e_sweep_smoke` to locate `bench_sweep` relative to the test executable, and added a CMake target dependency on `bench_sweep`.
- Local Windows validation: CMake configure succeeded; targeted Release build succeeded for parser/phase2/phase3/phase4/research targets; CTest passed 19/19 for parser, phase2, phase3, phase4, and non-journal research tests.
- Linux validation: WSL Release build succeeded for `journal_replay`, `itch_to_journal`, `test_phase_c_mixed_event_replay`, and `test_phase_c_journal_roundtrip`; both focused Phase C tests passed under `/tmp/mdh-journal-build`.
- Recorded-tape smoke: tiny ITCH conversion from `/mnt/c/Users/bhask/Downloads/01302019.NASDAQ_ITCH50/01302019.NASDAQ_ITCH50` succeeded with `events_written=1000`, `messages_parsed=1006`, `unhandled_msg_count=6`, and `parser_malformed_messages=0`.
- Remaining blocker: immediately after the tiny conversion, WSL returned `Wsl/Service/E_UNEXPECTED` / catastrophic failure while trying to rerun `journal_replay`, so actual converted-journal replay evidence remains TODO/VERIFY.
- Local limitation: MSVC Release compilation of `bench_sweep` timed out with `cl.exe` consuming CPU and no diagnostic, so Phase E smoke still needs Linux/GitHub validation.
- Root-caused the converted-journal replay hang: skipped unknown ITCH messages created small sequence gaps, and recovery reinjection repeatedly reprocessed events already buffered as pending out-of-order records.
- Fixed `Pipeline::on_event` to skip recovered records that are already present in the pending map, preventing exponential pending-buffer growth.
- Added `test_phase2_recovery_pending_dedupe` to lock the pending-dedupe behavior.
- Changed `itch_to_journal` to write `Unknown` placeholder events for unhandled ITCH message types instead of skipping them, preserving contiguous replay sequencing while keeping `unhandled_msg_count` visible.
- Validation after fix: WSL CTest passed `phase2_recovery_pending_dedupe` and `phase_c_mixed_event_replay`; tiny real ITCH conversion wrote `events_written=1000`, `unhandled_msg_count=6`, `parser_malformed_messages=0`; `journal_replay --mode pipeline-crc` completed with `records_read=1000`, `crc_failures=0`, `merged_crc=0x074e1a0f`.
- Tiny real ITCH backtest smoke also completed with `records_read=1000`, `crc_failures=0`, zero fills/PnL on the metadata-heavy opening slice, and wrote `bench/results/journal_replay_20260519T163601Z.json`.
- 1M recorded ITCH replay passed twice on the same converted journal with `records_read=1000000`, `crc_failures=0`, and matching merged CRC `0x9a64c0b8`.
- 5M recorded ITCH conversion/replay passed: `messages_parsed=5000000`, `events_written=5000000`, `unhandled_msg_count=211118`, `parser_malformed_messages=0`, `records_read=5000000`, `crc_failures=0`, `pipeline_crc_hex=0x1f27338a`.
- 5M recorded ITCH research experiment passed twice via `experiment_runner`, writing `bench/results/experiment_itch_5m_a.json` and `bench/results/experiment_itch_5m_b.json`; both runs matched records `5000000`, input CRC `2369313900`, config hash `2743174372`, output hash `882212532`, submitted orders `121950`, fills `1387`, clock rejects `0`, Sharpe `0.107187`, fill ratio `0.0113735`, and max drawdown `9325000`.

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

## 2026-05-18 (Context refresh)
- Refreshed `context/BATON.md`, `context/PROJECT_CONTEXT.md`, and `context/EVIDENCE_MAP.md` to reflect the current repo surface more accurately.
- Documented the additional phase surfaces now present in the repo: wire ingestion, journal/replay, phase 4 simulation/backtest, research, runtime pinning, and bench metadata utilities.
- Updated `NEXT_TASK.md`, `CLAIMS_MATRIX.md`, and `RESUME_CLAIMS.md` to keep evidence boundaries aligned with the broader codebase.
- Kept the offline three-venue boundary explicit and left live-feed / recorded-market-tape claims scoped to `TODO/VERIFY`.

## 2026-05-18 (Task 1 artifacts)
- Built the repo in WSL2 Release mode under `/home/genti411/mdh-task1-build` with GNU 15.2.0, using the existing `cmake -S . -B ... -DCMAKE_BUILD_TYPE=Release` / `cmake --build ... -j` flow from `README.md`.
- Added `lat_ns_max` output to `tools/feature_latency_probe.cpp` and `tools/feed_hot_path_bench.cpp` so the artifact schema could include max latency without inventing numbers.
- Captured two WSL2 runs each for `phase3_feature_latency_bench` and `feed_hot_path_bench` at `taskset -c 2` with 2,000,000 events and 10,000 warmup iterations.
- Wrote `bench/results/feature_latency_wsl2_4b5ab45af37c4ea1fee77aea27e89ef8fa35ff5a.json` and `bench/results/feed_hot_path_wsl2_4b5ab45af37c4ea1fee77aea27e89ef8fa35ff5a.json`, plus the corresponding stdout text files.
- Moved the feature/publish latency claim from the old 386ns p99 draft to artifact-backed WSL2 numbers: 399ns p99 for feature latency and 36-37ns p99 for feed hot path.
- Claims remain TODO/VERIFY for bare-metal Linux comparison and for anything beyond the measured WSL2 artifact scope.

## 2026-05-18 (Task 2 gate check: recorded ITCH Phase 4 backtest)
- Read required context/docs first: `context/BATON.md`, `context/PROJECT_CONTEXT.md`, `context/EVIDENCE_MAP.md`, `CLAIMS_MATRIX.md`, `RESUME_CLAIMS.md`, `NEXT_TASK.md`, `AUDIT_LOG.md`.
- Verified current `~/mdh-task1-build/phase4_backtest` behavior and source (`tools/phase4_backtest.cpp`): no CLI ingestion flags; runs only a hard-coded synthetic tape and writes a synthetic JSON.
- Verified `tools/journal_replay.cpp` supports Phase 4 metrics only from canonical journals via `--journal <path> --mode backtest|both`.
- Searched tool/source surface for an existing raw-ITCH to canonical-journal converter; none found in current repo.
- Computed requested tape hash from the actual file path:
  - Tape file: `/mnt/c/Users/bhask/Downloads/01302019.NASDAQ_ITCH50/01302019.NASDAQ_ITCH50`
  - SHA-256: `1d0972ffc25b35902ccc3f9069aae517da56903d5795f872902b8697315f30c3`
- Per Task 2 stop rule, did not run bounded/full Phase 4 recorded-tape backtests and did not fabricate artifacts without a valid ingestion path.
- Documented the exact adapter gap and nearest reusable module in `NEXT_TASK.md`.

## 2026-05-18 (Task 2 unblock attempt: minimal ITCH->journal converter)
- Added `tools/itch_to_journal.cpp` (Linux CLI): `--in`, `--out`, optional `--max-events`, `--max-bytes`.
- Converter uses existing modules only: `mf::proto::nasdaq::Itch50Parser` and `mf::journal::JournalWriter`; no journal-format change, no canonical-event model change.
- Added CMake wiring for `itch_to_journal` under the existing Linux journal-tools block.
- Extended `tools/journal_replay.cpp` output fields to include realized/unrealized/total PnL and fill ratio; also accepts `--seed` argument (currently ignored) so Task 2 command shape remains stable.
- Build succeeded in existing WSL Release dir: `/home/genti411/mdh-task1-build`.
- Converter smoke conversion succeeded (1M events):
  - `bytes_read=35097203`
  - `messages_parsed=1211089`
  - `events_written=1000000`
  - `unhandled_msg_count=211089`
  - `parser_malformed_messages=0`
  - `wall_time_sec=1.03299`
  - Output journal path: `/tmp/itch_20190130_1m.journal` (non-empty, ~107MB)
- Blocker encountered: `journal_replay` succeeds on synthetic journals, but fails on converted ITCH journals in this environment (silent nonzero and intermittent `Wsl/Service/E_UNEXPECTED` service failures), so replay compatibility is not yet proven.
- Per stop conditions, no 5M conversion, no deterministic backtest pair, and no `bench/results/phase4_backtest_itch_20190130_<gitsha>.json` artifact were produced in this pass.

## 2026-05-18 (Task 2 diagnosis run: replay failure isolation)
- Read required context files first (`context/BATON.md`, `context/PROJECT_CONTEXT.md`, `context/EVIDENCE_MAP.md`) plus `AUDIT_LOG.md` Task 2 blocker entry.
- Step 1: WSL health initially looked good (`wsl --status`, `free -h`, `df -h /tmp /home`, and two successful `ls /tmp` calls), but multiple later invocations intermittently failed with:
  - `Windows Subsystem for Linux has no installed distributions`
  - `Wsl/Service/E_UNEXPECTED`
- Ran `wsl --shutdown`, then rechecked; instability still recurred during diagnosis commands.
- Step 2 known-good journal source: existing CLI `make_research_journal` (no throwaway code). Generated `/tmp/known_good.journal` with 100 events.
- Step 3 replay result on known-good journal: direct invocation of `/home/genti411/mdh-task1-build/journal_replay --journal /tmp/known_good.journal` succeeded with exit 0 and normal output.
- ITCH conversion on tiny slice succeeded: `/home/genti411/mdh-task1-build/itch_to_journal --in /mnt/c/Users/bhask/Downloads/01302019.NASDAQ_ITCH50/01302019.NASDAQ_ITCH50 --out /tmp/itch_diag.journal --max-events 1000`.
- Replay result on converted ITCH journal: direct invocation returned exit 1 with no stdout/stderr (silent failure path observed when WSL did not transient-fail first).
- Step 4C: built Debug replay target at `/tmp/build-debug` and reran:
  - known-good journal: exit 0 with expected output
  - ITCH journal: exit 1 with no output
- Attempted `strace` per step 4C, but command path was blocked by recurrent WSL service failures, so no trace artifact could be captured in this session.

## 2026-05-18 (Observability patch attempt for silent replay exit)
- Ran WSL recovery/status sequence: `wsl --shutdown`, `wsl --list --verbose`, `wsl --update`, `wsl --status`, `wsl --version`; distro is present and WSL is on 2.7.3.0, but transient failures still recur during later invocations.
- Confirmed two consecutive `ls /tmp` calls succeeded before starting debug steps.
- Built Debug targets under `/tmp/build-debug`: `journal_replay`, `itch_to_journal`, `make_research_journal`.
- Generated tiny journals:
  - known-good: `/tmp/known_good.journal` via `make_research_journal --events 100`
  - ITCH-converted: `/tmp/itch_diag.journal` via `itch_to_journal --max-events 1000`
- Captured first header lines:
  - known-good starts with `4d46 4a4e 0100 0000 0100 0000 ...`
  - ITCH journal starts with same magic/version, but first record payload bytes differ materially at `0x10+`.
- `strace` is not installed in this WSL image (`bash: strace: command not found`), and installation was blocked by non-interactive sudo plus recurring WSL service faults.
- Applied minimal observability patch:
  - `include/mf/journal/journal_reader.hpp`: expose `error_reason`, `error_offset`, `had_error`.
  - `src/journal/journal_reader.cpp`: set explicit reasons on error branches (`header_magic`, `header_version`, `record_size`, `record_crc`, `unexpected_eof`, fallback `record_decode`).
  - `tools/journal_replay.cpp`: print stderr reason+offset and `exiting nonzero` on invalid args/open/read error; added explicit `event_type_unhandled` guard before pipeline ingestion.
- Post-patch validation could not be completed deterministically due renewed `no installed distributions` / `Wsl/Service/E_UNEXPECTED` transients during replay runs.

## 2026-05-18 (Task 4 + Task 5: CI and README scope/architecture)
- Added minimal GitHub Actions workflow at `.github/workflows/ci.yml` for `push`/`pull_request` on `main` with one `ubuntu-latest` job: configure, build, parser tests, phase 2 tests, phase C replay determinism test, and 30s fuzz smoke for each parser target with file-existence guards.
- Removed legacy `.github/workflows/phase2-linux.yml` (bench/evidence workflow) to keep CI scoped to build + tests + fuzz smoke only.
- Updated `CMakeLists.txt` to register parser/phase2/phase-C tests with `ctest` (`enable_testing`, named tests, labels) so CI can run the required subsets deterministically.
- Updated `README.md` with the explicit `## Scope` section, a new architecture ASCII diagram that includes canonical `mf::core::BookEvent` and downstream phase/research paths, and a focused `## Benchmarks` section linking the Task 1 artifact paths and WSL2 caveat text.
