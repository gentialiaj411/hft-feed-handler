# Book Throughput

- cycles: 50000
- events: 250000
- warmup_cycles: 5000
- timing_source: RDTSCP when available, steady_clock fallback otherwise
- ticks_per_ns: 3.07199

| engine | ops_per_sec |
|---|---:|
| naive_std_map_order_book | 3.38581e+06 |
| current_aggregate_order_book | 8.22049e+06 |
| intrusive_slab_order_book | 4.37808e+06 |
| current_vs_naive | 2.42792x |
| intrusive_vs_naive | 1.29306x |
| intrusive_vs_current | 0.532581x |

Methodology: same synthetic add/execute/cancel/replace lifecycle tape, warmup discarded, single-thread hot loop.
Note: the intrusive engine maintains per-order queues and slab-backed nodes; the current aggregate engine remains faster on this aggregate-only workload.
Metadata: {"git_sha":"6fe76e8cd3e162f1a92925113363a360e5f24880","build_type":"Release","cxx_flags":"unknown","host":"unknown","cpu_model":"unknown","cpu_count":24,"kernel":"unknown","utc_timestamp":"20260522T020747Z","command_line":"C:\\Users\\bhask\\Documents\\PROJECTS\\market-data-handler\\build_phase_e_win\\Release\\book_engine_bench.exe --cycles 50000 --warmup-cycles 5000 --out-dir bench/results"}
