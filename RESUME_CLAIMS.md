# RESUME_CLAIMS.md

## Purpose
Token-efficient source of truth for resume-facing project claims in this repo.

## Resume Bullets (User-Provided Draft)
Project: HFT Market Data Handler

- Built a C++ pipeline that ingests, normalizes, and merges NASDAQ ITCH 5.0, IEX DEEP+, and Cboe PITCH feeds into a single consistent event stream, validated across 2 million replayed events
- Implemented A/B dual-feed failover with gap recovery and per-venue sequence tracking, maintaining zero output divergence under 50% simulated packet loss in CI
- Engineered a per-tick feature engine for 6 microstructure signals and published outputs through a lock-free SPSC ring buffer at 386ns p99 latency

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

Evidence state: PARTIAL. `EventStore`, `SimulationClock`, `StrategyEngine`, and `ExperimentRunner` have passing synthetic-journal tests. 2M synthetic canonical-event artifacts match input/config/output hashes across reruns. Large recorded market-tape experiment JSON artifact is still TODO/VERIFY.

Resume-safe scoped wording:
- Built an event-sourced C++20 HFT research simulator with CRC-validated canonical event storage and deterministic replay, reproducing identical input CRC, config hash, and strategy output hash across a 2M-event synthetic canonical journal.
