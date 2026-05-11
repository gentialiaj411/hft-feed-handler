# MultiFeed Architecture (Locked v1)

## Threading (v1)
- T0 control/config
- T1 NASDAQ parser+arbiter (A/B)
- T2 IEX parser+seq/recovery (single-stream + retransmission/replay recovery)
- T3 Cboe parser+arbiter (A/B)
- T4 shared book thread (round-robin venue queues)
- T5 NBBO + features
- T6 JIT consumer bridge

## Data Ownership
- Parser threads own protocol decode state and sequence state.
- Shared book thread owns all per-venue books in v1.
- NBBO/features thread owns consolidated top-of-book and feature state.
- All handoff via bounded preallocated SPSC queues.

## Venue Redundancy Notes
- NASDAQ ITCH and Cboe PITCH paths support A/B-style duplicate stream arbitration in replay simulation.
- IEX DEEP historical replay is consumed as single-stream PCAP and uses sequence tracking + recovery logic (no native A/B arbitration dependency in v1 replay mode).

## Determinism
- Canonical event stream order drives CRC32.
- CRC input is canonical serialized BookEvent fields, not raw bytes.
- Replay invariance target: bit-identical across 100 runs.

## Phase 2 Framework Status
- Added a venue-scoped sequence tracker module (`mf::phase2::SequenceTracker`) with bounded gap window logic.
- Added multi-venue wrapper (`mf::phase2::MultiVenueSequenceTracker`) covering NASDAQ, IEX, and Cboe as independent sequence domains.
- Added recovery abstraction interface (`mf::phase2::IRecoveryHandler`) and gap-aware sequencer (`mf::phase2::GapAwareSequencer`) to issue deterministic recovery requests for missing ranges.
- Added deterministic merged publication primitive (`mf::phase2::DeterministicMerger`) with stable ordering by `(exchange_ts_ns, venue, sequence, raw_type)`.
- Current behavior:
  - in-order packets advance expected sequence
  - duplicate/old packets are classified and ignored
  - out-of-order packets within window are buffered and released deterministically once gaps are filled
  - packets beyond window are reported as oversized gaps for recovery path handling
- Remaining Phase 2 work:
  - bind recovery requests to replay/retransmission simulation transport
  - integrate merged output path into dedicated benchmark and JIT bridge

### Validator Integration (Phase 2)
- `phase1_parser_validate` now has `--phase2-merge <nasdaq_file> <iex_pcap_file>`.
- This mode routes parsed events through:
  - `GapAwareSequencer` (per-venue sequence/gap state + recovery request signaling)
  - `ReplayRecoverySimulator` (replay-time missing-range fill from previously seen events)
  - deterministic gap-buffer release path
  - `DeterministicMerger` (canonical publish order)
- It reports `merged_crc32` and recovery/gap counters for deterministic framework bring-up.

## Latency budget (laptop-bound target)
- Parse/decode: 35-80 ns
- A/B arbitration + seq tracking: 20-50 ns
- Shared book update: 100-220 ns
- NBBO update: 25-70 ns
- Feature update: 120-350 ns
- SPSC publish to JIT: 80-250 ns
- End-to-end (parse entry -> feature vector publish): 380-1,020 ns p99 envelope
