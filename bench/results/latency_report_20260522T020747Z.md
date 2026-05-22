# Latency Report

- events_measured: 50000
- warmup_discarded: 5000
- decoded_total: 55000
- published_total: 55000
- timing_source: RDTSCP when available, steady_clock fallback otherwise
- ticks_per_ns: 3.071992
- requested_core: -1
- pinned: false
- methodology: warmup discarded, single producer thread, synthetic ITCH Add payloads, cache-warm steady-state loop

| stage | count | p50_ns | p99_ns | p99.9_ns | p99.99_ns | max_ns |
|---|---:|---:|---:|---:|---:|---:|
| decode | 50000 | 62 | 78 | 99 | 247 | 17921 |
| sequence | 50000 | 49 | 64 | 96 | 425 | 35691 |
| book_apply | 50000 | 342 | 685 | 1596 | 61412 | 90371 |
| publish | 50000 | 40 | 60 | 126 | 747 | 9724 |
| end_to_end | 50000 | 495 | 842 | 7930 | 61412 | 90997 |
