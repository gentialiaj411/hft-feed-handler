# MultiFeed Current State (May 12, 2026)

## Project Summary
- Project: `MultiFeed` (multi-venue HFT feed handler + microstructure pipeline)
- Language/runtime constraints: C++20, stdlib-only (Catch2/benchmark libs optional later)
- Perf target: Linux/WSL2
- Windows: sanity build target

## Protocol Scope
- NASDAQ ITCH 5.0
- IEX DEEP+
- Cboe PITCH (pluggable path; historical replay still data-license constrained)

## Completed (Core)
- Unified normalized event model (`BookEvent`) with canonical CRC32 path.
- Parser implementations for NASDAQ/IEX/Cboe code paths.
- IEX PCAP replay adapter and deterministic replay checks.
- Phase 2 framework:
  - Per-venue sequence tracking
  - Bounded gap detection/buffering
  - Recovery abstraction + replay recovery simulator
  - Deterministic merged publication
  - Reusable Phase 2 pipeline module
  - JIT bridge sink integration path (SPSC ring publisher adapter)
  - Dedicated Phase 2 benchmark app
- Validation/ops:
  - Phase 2 WSL/Linux test runner scripts
  - One-shot evidence collection script
  - Artifact summarizer script
  - Repo hygiene guardrails (CI + optional local hook)
  - Linux CI workflow for Phase 2 tests/bench/determinism

## Current Evidence/Validation
- Phase 2 tests compile and pass in current workspace.
- Determinism test coverage exists for merged CRC invariance under valid interleavings.
- Phase 2 bench emits measurable counters + merged CRC artifacts.
- CI workflow now uploads perf artifacts for auditability.

## Known External Constraints (Not Framework Gaps)
- NASDAQ dataset framing/container compatibility is still sample-dependent.
  - Added `--nasdaq-raw-itch` mode for raw ITCH stream triage.
- Cboe full historical replay validation remains blocked by licensed historical data access.

## Important Risk Notes
- Any benchmark claims must come from generated artifacts (no hardcoded/fixed claims).
- Raw exchange payloads should not be tracked in git history (guardrails now present).
- Determinism depends on preserving per-venue sequence semantics before merge.

## Immediate Next Technical Priorities
1. Run full evidence workflow on Linux/WSL2 with real local datasets and archive outputs.
2. Validate NASDAQ replay quality with whichever local sample format is available (`--nasdaq-only` vs `--nasdaq-raw-itch`).
3. If Cboe licensed data becomes available, run same Phase 2 evidence path and compare counters/CRC stability.
4. Align JIT shared-memory schema/versioning with downstream signal engine contract and add end-to-end compatibility tests.

## Repo Commands (Key)
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
- Phase 2 tests (Linux/WSL): `bash scripts/run_phase2_tests_wsl.sh`
- Phase 2 benchmark (Linux/WSL): `bash scripts/bench_phase2_pipeline_wsl.sh 2000000 256 1048576`
- One-shot evidence (Linux/WSL):
  - `bash scripts/run_phase2_evidence_wsl.sh <nasdaq_raw> <iex_pcap> [cboe_file] [out_dir]`
- Artifact summary:
  - `python3 scripts/summarize_phase2_artifact.py artifacts/perf/<artifact_file>.txt`

## Suggested Expert-LLM Review Prompt
Use the prompt below verbatim (or minimally edited):

---
You are a principal low-latency C++ systems reviewer with HFT feed-handler experience.

Context:
- Repo: MultiFeed (`market-data-handler`)
- Constraints: C++20, stdlib-only, Linux/WSL2 perf target, Windows sanity only
- Current phase: Phase 2 framework completed in-code
- Protocols: NASDAQ ITCH 5.0, IEX DEEP+, Cboe PITCH (pluggable, licensed replay gap)

Your tasks:
1. Perform an architecture/code-quality review focused on:
   - Determinism guarantees (event ordering, CRC stability, replay invariance)
   - Sequence/gap/recovery correctness under bursty/out-of-order conditions
   - Merge semantics and tie-break behavior
   - JIT bridge handoff robustness and backpressure/drop accounting
2. Identify concrete failure modes and rank by severity.
3. Propose a prioritized remediation plan with minimal-risk refactor steps.
4. Provide a perf-risk analysis:
   - expected branch/cache/TLB sensitivities in hot paths
   - likely allocator/contention pitfalls
   - where microbench methodology can mislead
5. Audit testing completeness:
   - what is covered
   - what critical scenarios are missing
   - exact additional tests to add (with input patterns and expected outcomes)
6. Output:
   - `Findings` (severity-ordered)
   - `Open Questions`
   - `Recommended Commits` (small, logically isolated)
   - `Go/No-Go for Phase 3` with explicit criteria

Important:
- Do not invent benchmark numbers.
- Treat known external constraints separately from code defects.
- Be specific; cite file paths and exact modules/functions.
---
