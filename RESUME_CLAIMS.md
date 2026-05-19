# RESUME_CLAIMS.md

## Purpose
Token-efficient source of truth for resume-facing project claims in this repo.

## Resume Bullets (User-Provided Draft)
Project: HFT Market Data Handler

- Built a C++ pipeline that ingests, normalizes, and merges NASDAQ ITCH 5.0, IEX DEEP+, and Cboe PITCH feeds into a single consistent event stream, validated across 2 million replayed events
- Implemented A/B dual-feed failover with gap recovery and per-venue sequence tracking, maintaining zero output divergence under 50% simulated packet loss in CI
- Engineered a per-tick feature engine for 6 microstructure signals and published outputs through a lock-free SPSC ring buffer at 399ns p99 latency on WSL2 (`bench/results/feature_latency_wsl2_4b5ab45af37c4ea1fee77aea27e89ef8fa35ff5a.json`)
- Measured feed hot-path sequence tracking and SPSC publication at 36-37ns p99 on WSL2 (`bench/results/feed_hot_path_wsl2_4b5ab45af37c4ea1fee77aea27e89ef8fa35ff5a.json`)

## Evidence Status
- Source: user-provided resume draft.
- Verification state: TODO/VERIFY for all quantitative claims unless backed by tests/bench artifacts.
- Rule: do not restate these as facts in external-facing docs until CLAIMS_MATRIX.md links concrete evidence.

## Scrutiny Checklist
- Confirm each metric/percentile/throughput claim has reproducible command + artifact.
- Confirm fault-tolerance and correctness claims map to explicit tests.
- Confirm implementation nouns in bullets (e.g., Raft, KV-cache paging, ORC JIT) exist in code paths.
- Replace or soften any claim that lacks direct evidence.

## Optimization Guidance
- Prefer one strong, reproducible metric per bullet over multiple weak metrics.
- Keep bullets outcome-first, mechanism-second, evidence-third.
- If evidence is partial, rewrite with scoped language (e.g., "in current benchmark setup").

## Candidate Upgrade After Research Layer
- Built an event-sourced C++20 HFT research simulator with CRC-validated canonical event storage, deterministic event-time replay, feature-driven order intents, execution simulation, and PnL/risk reporting.

Evidence state: STRONGER. `EventStore`, `SimulationClock`, `StrategyEngine`, and `ExperimentRunner` have passing synthetic-journal tests. 2M synthetic canonical-event artifacts match input/config/output hashes across reruns. Recorded 5M NASDAQ ITCH artifacts `bench/results/experiment_itch_5m_{a,b}.json` also match input CRC `2369313900`, config hash `2743174372`, and output hash `882212532`.

Resume-safe scoped wording:
- Built an event-sourced C++20 HFT research simulator with CRC-validated canonical event storage and deterministic replay, reproducing identical input CRC, config hash, and strategy output hash across a 2M-event synthetic canonical journal.
- Extended the simulator to a 5M-event recorded NASDAQ ITCH replay, reproducing identical input CRC, config hash, and strategy output hash across reruns, with 121,950 submitted orders and 1,387 simulated fills.

## Additional Verified Surfaces
- Phase 4 simulation/backtest tooling exists: matching engine, strategy, PnL accountant, and backtest runner tests are present in the repo.
- Phase B wire-ingestion, Phase C journal/replay, and Phase D runtime/pinning code paths also exist, but their resume value should stay scoped to the artifacts recorded in `CLAIMS_MATRIX.md`.
- The WSL2 feature-latency artifact is stable across two 2M-event runs within 1.24% p99.
- The WSL2 feed-hot-path artifact is stable across two 2M-event runs within 2.78% p99.

## Candidate Upgrade After Recorded Backtest (TODO/VERIFY)
- Wrote an offline NASDAQ ITCH 5.0 -> canonical journal converter and backtested a market-making strategy on a recorded ITCH sample (2019-01-30, N events) with deterministic PnL across reruns.

Evidence state: PARTIAL/BOUNDED. Converter tool exists (`tools/itch_to_journal.cpp`) and 5M-event recorded ITCH replay artifacts now exist. Avoid implying full-day or cross-venue recorded validation unless those artifacts are added.
