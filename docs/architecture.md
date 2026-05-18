# MultiFeed Architecture

## Purpose
This document gives the high-level architecture of `market-data-handler` / `MultiFeed`.
It is intentionally shorter than `PROJECT_CONTEXT.md` and exists to answer one question quickly:
how does the system move from raw multi-venue market data to deterministic features?

## System Overview
MultiFeed is an offline C++20 market-data pipeline that:
- decodes three venue protocols
- normalizes them into one canonical event model
- enforces per-venue sequence discipline
- merges events deterministically
- maintains per-venue order books and consolidated NBBO
- computes microstructure features
- publishes feature vectors through a lock-free SPSC ring

```text
Raw venue payloads
  -> protocol parsers
  -> canonical BookEvent
  -> sequence tracking and gap handling
  -> deterministic merge / recovery / A-B arbitration
  -> order book maintenance
  -> NBBO consolidation
  -> feature pipeline
  -> FeatureVector publication
```

## Architectural Layers

### 1. Protocol Decode
The parser layer converts venue-specific wire formats into `mf::core::BookEvent`.
Supported venue inputs:
- NASDAQ ITCH 5.0
- IEX DEEP+
- Cboe PITCH

Each parser is responsible for:
- wire-format decoding
- timestamp normalization
- side/order/price/quantity extraction
- setting venue identity and raw message type
- rejecting malformed payloads cleanly

### 2. Canonical Event Model
`mf::core::BookEvent` is the shared object that all later stages consume.
It carries:
- venue
- event type
- sequence
- exchange and ingest timestamps
- symbol
- order and trade identifiers
- price, quantity, side
- raw message type

This is the key abstraction boundary in the system.
Everything after parsing works in terms of this model, not protocol-specific structs.

### 3. Phase 2 Replay and Recovery
Phase 2 makes the stream replay-safe and deterministic.
Its responsibilities are:
- sequence tracking per venue
- bounded gap buffering
- recovery request emission
- deterministic merge ordering
- optional A/B arbitration for dual-feed replay
- recovery simulator integration

Important rules:
- in-order events advance immediately
- small gaps are buffered
- duplicate/old events are ignored
- large gaps force advancement so the stream does not deadlock
- deterministic merge ordering uses:
  1. `exchange_ts_ns`
  2. `venue`
  3. `sequence`
  4. `raw_type`

### 4. Phase 3 Market State and Features
Phase 3 consumes merged canonical events and builds market state:
- per-venue order books
- per-symbol NBBO
- microstructure features

The phase is split into:
- order book engine
- NBBO consolidator
- feature pipeline
- feature bridge / publisher

### 5. Publication Layer
Feature vectors are published through a lock-free SPSC ring.
This keeps the hot path simple:
- one producer
- one consumer
- bounded capacity
- cache-line-separated atomic indices

## Data Ownership Model
- Parsers own wire-format interpretation only.
- Phase 2 owns sequencing, recovery, replay order, and determinism witnesses.
- Phase 3 owns order state, NBBO, and feature computation.
- The ring buffer owns only publication transport.

This separation matters because it keeps protocol parsing, replay correctness, and feature math from bleeding into each other.

## Concurrency and Throughput Shape
The design is intentionally pipeline-like rather than fully shared-state concurrent:
- venue parsing can be independent
- sequencing is venue-scoped
- deterministic merge happens after events are normalized
- book and feature state are updated in a controlled downstream path

The primary lock-free primitive is the SPSC ring at publication time.
Elsewhere, the code prefers deterministic state transitions over aggressive shared concurrency.

## Determinism Strategy
The project uses two determinism anchors:
- stable merge ordering
- CRC32 over selected canonical event fields

The goal is not cryptographic integrity.
The goal is repeatable replay and fast regression checking.

## Validation Surfaces
Architecture-level confidence comes from:
- parser unit tests
- sequence/recovery tests
- determinism CRC tests
- order book and NBBO tests
- feature math tests
- feature bridge publication tests
- benchmark and evidence scripts

## Relationship To `PROJECT_CONTEXT.md`
- `docs/architecture.md` answers: "what is the system shape?"
- `PROJECT_CONTEXT.md` answers: "what exactly does each module/API/test do right now?"

Keep both files.
The architecture file should stay high-level.
The project context file should stay detailed.

