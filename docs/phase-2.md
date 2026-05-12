# Phase 2

## Scope (Completed in Repo)
- Per-venue sequence tracking (`mf::phase2::SequenceTracker`)
- Gap detection + bounded buffering window logic
- Recovery abstraction (`IRecoveryHandler`) + replay recovery simulator
- Deterministic merged publication (`DeterministicMerger`)
- Reusable Phase 2 pipeline (`mf::phase2::Pipeline`)
- JIT bridge sink path (`JitBridge`) with SPSC ring publisher adapter
- Dedicated benchmark harness (`phase2_pipeline_bench`)

## Validation Commands
- Build:
  - `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
  - `cmake --build build -j`
- Unit-style tests:
  - `./build/test_phase2_sequence_tracker`
  - `./build/test_phase2_recovery_merge`
  - `./build/test_phase2_recovery_simulator`
  - `./build/test_phase2_determinism_crc`
  - `./build/test_phase2_pipeline`
  - `./build/test_phase2_jit_bridge`
- Validator integration:
  - `./build/phase1_parser_validate --phase2-merge <nasdaq_file> <iex_pcap_file> [cboe_file]`
  - `./build/phase1_parser_validate --phase2-merge-jit <nasdaq_file> <iex_pcap_file> [cboe_file]`

## NASDAQ Framing Adapter Path
- Added raw ITCH mode for current sample mismatch triage:
  - `./build/phase1_parser_validate --nasdaq-raw-itch <nasdaq_raw_itch_file>`

## Linux/WSL2 Perf Evidence Workflow
- Run reproducible benchmark on target platform:
  - `bash scripts/bench_phase2_pipeline_wsl.sh 2000000 256 1048576`
- Output artifact is saved under:
  - `artifacts/perf/phase2_pipeline_bench_YYYYMMDD_HHMMSS.txt`

## One-Shot Evidence Collection (WSL)
- Run all key Phase 2 evidence steps in one command:
  - `bash scripts/run_phase2_evidence_wsl.sh <nasdaq_raw> <iex_pcap> [cboe_file] [out_dir]`
- Default output:
  - `artifacts/perf/phase2_evidence_YYYYMMDD_HHMMSS.txt`
- Includes:
  - Phase 2 test suite execution and PASS stamp
  - NASDAQ raw ITCH adapter validation (`--nasdaq-raw-itch`)
  - Phase 2 merge + JIT bridge counters (`--phase2-merge-jit`)
  - Phase 2 synthetic pipeline benchmark (`phase2_pipeline_bench`)

## WSL Test-Only Runner
- Run just test evidence collection:
  - `bash scripts/run_phase2_tests_wsl.sh [out_dir]`
- Output artifact:
  - `artifacts/perf/phase2_tests_YYYYMMDD_HHMMSS.txt`

## Repo Hygiene Guardrails
- CI enforcement:
  - `.github/workflows/repo-hygiene.yml`
  - `.github/workflows/phase2-linux.yml`
  - `scripts/check_repo_hygiene.ps1`
- Optional local pre-commit hook install:
  - `powershell -ExecutionPolicy Bypass -File scripts/install_git_hooks.ps1`

## Known External Constraints (Not Framework Gaps)
- NASDAQ historical sample framing/container compatibility is dataset-dependent.
- Cboe full historical replay validation remains dependent on licensed sample availability.

## Phase 2 Completion Checklist
- [x] Sequence tracking + gap window logic
- [x] Recovery abstraction + replay simulator
- [x] Deterministic merged publication path
- [x] JIT bridge sink integration path
- [x] Dedicated benchmark harness
- [x] WSL/Linux test + perf evidence runners
- [x] Repo hygiene guardrails (CI + optional local hook)
