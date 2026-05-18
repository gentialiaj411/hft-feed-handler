cmake_minimum_required(VERSION 3.20)
project(MultiFeed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/FS>)

option(MF_ENABLE_FUZZ "Enable libFuzzer targets" OFF)
option(MF_STRICT_WARNINGS "Treat warnings as errors for local/CI hardening" OFF)
option(MF_BUILD_PHASE4 "Build Phase 4 backtest engine + tests" ON)
option(MF_BUILD_PHASE_B "Build Phase B wire ingestion + tests (Linux only)" ON)
option(MF_BUILD_JOURNAL "Build Phase C journal + replay tooling (Linux only)" ON)
option(MF_BUILD_PHASE_D "Build Phase D NUMA/pinning/hugepage tooling (Linux only)" ON)

set(MF_HAS_LIBNUMA OFF)
if(MF_BUILD_PHASE_D AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_library(NUMA_LIB numa)
  if(NUMA_LIB)
    set(MF_HAS_LIBNUMA ON)
  else()
    message(WARNING "libnuma not found; Phase D NUMA paths will use fallback allocator.")
  endif()
endif()

add_library(multifeed_core
    src/core/crc32.cpp
    src/core/time.cpp
    src/phase2/deterministic_merger.cpp
    src/phase2/ab_arbiter.cpp
    src/phase2/jit_bridge.cpp
    src/phase2/pipeline.cpp
    src/phase2/recovery.cpp
    src/phase2/recovery_simulator.cpp
    src/phase2/sequence_tracker.cpp
    src/phase3/order_book_engine.cpp
    src/phase3/nbbo_consolidator.cpp
    src/phase3/feature_pipeline.cpp
    src/phase3/feature_bridge.cpp
    src/proto/nasdaq/itch50_parser.cpp
    src/proto/iex/deep_parser.cpp
    src/proto/cboe/pitch_parser.cpp
)

if(MF_BUILD_PHASE4)
  target_sources(multifeed_core PRIVATE
      src/phase4/sim_matching_engine.cpp
      src/phase4/strategy.cpp
      src/phase4/pnl_accountant.cpp
      src/phase4/backtest_runner.cpp
  )
endif()

if(MF_BUILD_PHASE_B AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(multifeed_core PRIVATE
      src/wire/udp_multicast_receiver.cpp
      src/wire/framing.cpp
      src/wire/feed_session.cpp
      src/wire/event_loop.cpp
  )
elseif(MF_BUILD_PHASE_B)
  message(STATUS "MF_BUILD_PHASE_B requested but disabled: Phase B targets are Linux-only.")
endif()

if(MF_BUILD_JOURNAL AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(multifeed_core PRIVATE
      src/journal/journal_writer.cpp
      src/journal/journal_reader.cpp
      src/journal/journaling_sink.cpp
  )
elseif(MF_BUILD_JOURNAL)
  message(STATUS "MF_BUILD_JOURNAL requested but disabled: Phase C journal targets are Linux-only.")
endif()

if(MF_BUILD_PHASE_D AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(multifeed_core PRIVATE
      src/os/cpu_affinity.cpp
      src/os/numa.cpp
      src/os/hugepages.cpp
  )
  if(MF_HAS_LIBNUMA)
    target_compile_definitions(multifeed_core PRIVATE MF_HAS_LIBNUMA=1)
    target_link_libraries(multifeed_core PRIVATE ${NUMA_LIB})
  endif()
elseif(MF_BUILD_PHASE_D)
  message(STATUS "MF_BUILD_PHASE_D requested but disabled: Phase D targets are Linux-only.")
endif()

target_include_directories(multifeed_core PUBLIC include)

target_compile_options(multifeed_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /FS>
    $<$<AND:$<BOOL:${MF_STRICT_WARNINGS}>,$<CXX_COMPILER_ID:GNU,Clang>>:-Werror>
    $<$<AND:$<BOOL:${MF_STRICT_WARNINGS}>,$<CXX_COMPILER_ID:MSVC>>:/WX>
)

add_executable(phase1_parser_validate src/apps/phase1_parser_validate_main.cpp)
target_link_libraries(phase1_parser_validate PRIVATE multifeed_core)

add_executable(phase2_pipeline_bench src/apps/phase2_pipeline_bench_main.cpp)
target_link_libraries(phase2_pipeline_bench PRIVATE multifeed_core)

add_executable(phase2_ab_evidence src/apps/phase2_ab_evidence_main.cpp)
target_link_libraries(phase2_ab_evidence PRIVATE multifeed_core)

add_executable(phase3_feature_latency_bench tools/feature_latency_probe.cpp)
target_link_libraries(phase3_feature_latency_bench PRIVATE multifeed_core)

add_executable(feed_hot_path_bench tools/feed_hot_path_bench.cpp)
target_link_libraries(feed_hot_path_bench PRIVATE multifeed_core)

if(MF_BUILD_PHASE4)
  add_executable(phase4_backtest tools/phase4_backtest.cpp)
  target_link_libraries(phase4_backtest PRIVATE multifeed_core)
endif()

if(MF_BUILD_PHASE_B AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(synthetic_mcast_sender tools/synthetic_mcast_sender.cpp)
  target_link_libraries(synthetic_mcast_sender PRIVATE multifeed_core)

  add_executable(wire_replay_to_pipeline tools/wire_replay_to_pipeline.cpp)
  target_link_libraries(wire_replay_to_pipeline PRIVATE multifeed_core)

  add_executable(test_phase_b_framing tests/test_phase_b_framing.cpp)
  target_link_libraries(test_phase_b_framing PRIVATE multifeed_core)

  add_executable(test_phase_b_udp_loopback tests/test_phase_b_udp_loopback.cpp)
  target_link_libraries(test_phase_b_udp_loopback PRIVATE multifeed_core)

  add_executable(test_phase_b_ab_failover tests/test_phase_b_ab_failover.cpp)
  target_link_libraries(test_phase_b_ab_failover PRIVATE multifeed_core)

  add_executable(test_phase_b_burst_loss tests/test_phase_b_burst_loss.cpp)
  target_link_libraries(test_phase_b_burst_loss PRIVATE multifeed_core)
endif()

if(MF_BUILD_JOURNAL AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(journal_replay tools/journal_replay.cpp)
  target_link_libraries(journal_replay PRIVATE multifeed_core)

  add_executable(journal_diff tools/journal_diff.cpp)
  target_link_libraries(journal_diff PRIVATE multifeed_core)

  add_executable(journal_capture_loopback tools/journal_capture_loopback.cpp)
  target_link_libraries(journal_capture_loopback PRIVATE multifeed_core)

  add_executable(test_phase_c_journal_roundtrip tests/test_phase_c_journal_roundtrip.cpp)
  target_link_libraries(test_phase_c_journal_roundtrip PRIVATE multifeed_core)

  add_executable(test_phase_c_journal_torn_record tests/test_phase_c_journal_torn_record.cpp)
  target_link_libraries(test_phase_c_journal_torn_record PRIVATE multifeed_core)

  add_executable(test_phase_c_replay_determinism tests/test_phase_c_replay_determinism.cpp)
  target_link_libraries(test_phase_c_replay_determinism PRIVATE multifeed_core)

  add_executable(test_phase_c_journal_format_version tests/test_phase_c_journal_format_version.cpp)
  target_link_libraries(test_phase_c_journal_format_version PRIVATE multifeed_core)
endif()

if(MF_BUILD_PHASE_D AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(phase_d_latency_bench tools/phase_d_latency_bench.cpp)
  target_link_libraries(phase_d_latency_bench PRIVATE multifeed_core)

  add_executable(test_phase_d_cpu_affinity tests/test_phase_d_cpu_affinity.cpp)
  target_link_libraries(test_phase_d_cpu_affinity PRIVATE multifeed_core)
  if(MF_HAS_LIBNUMA)
    target_compile_definitions(test_phase_d_cpu_affinity PRIVATE MF_HAS_LIBNUMA=1)
  endif()

  add_executable(test_phase_d_numa tests/test_phase_d_numa.cpp)
  target_link_libraries(test_phase_d_numa PRIVATE multifeed_core)
  if(MF_HAS_LIBNUMA)
    target_compile_definitions(test_phase_d_numa PRIVATE MF_HAS_LIBNUMA=1)
  endif()

  add_executable(test_phase_d_hugepages tests/test_phase_d_hugepages.cpp)
  target_link_libraries(test_phase_d_hugepages PRIVATE multifeed_core)
  if(MF_HAS_LIBNUMA)
    target_compile_definitions(test_phase_d_hugepages PRIVATE MF_HAS_LIBNUMA=1)
  endif()

  add_executable(test_phase_d_spsc_external_storage tests/test_phase_d_spsc_external_storage.cpp)
  target_link_libraries(test_phase_d_spsc_external_storage PRIVATE multifeed_core)
  if(MF_HAS_LIBNUMA)
    target_compile_definitions(test_phase_d_spsc_external_storage PRIVATE MF_HAS_LIBNUMA=1)
  endif()
endif()

add_executable(test_iex_deep_parser tests/test_iex_deep_parser.cpp)
target_link_libraries(test_iex_deep_parser PRIVATE multifeed_core)

add_executable(test_nasdaq_itch_parser tests/test_nasdaq_itch_parser.cpp)
target_link_libraries(test_nasdaq_itch_parser PRIVATE multifeed_core)

add_executable(test_cboe_pitch_parser tests/test_cboe_pitch_parser.cpp)
target_link_libraries(test_cboe_pitch_parser PRIVATE multifeed_core)

add_executable(test_phase2_sequence_tracker tests/test_phase2_sequence_tracker.cpp)
target_link_libraries(test_phase2_sequence_tracker PRIVATE multifeed_core)

add_executable(test_phase2_recovery_merge tests/test_phase2_recovery_merge.cpp)
target_link_libraries(test_phase2_recovery_merge PRIVATE multifeed_core)

add_executable(test_phase2_recovery_simulator tests/test_phase2_recovery_simulator.cpp)
target_link_libraries(test_phase2_recovery_simulator PRIVATE multifeed_core)

add_executable(test_phase2_determinism_crc tests/test_phase2_determinism_crc.cpp)
target_link_libraries(test_phase2_determinism_crc PRIVATE multifeed_core)

add_executable(test_phase2_pipeline tests/test_phase2_pipeline.cpp)
target_link_libraries(test_phase2_pipeline PRIVATE multifeed_core)

add_executable(test_phase2_jit_bridge tests/test_phase2_jit_bridge.cpp)
target_link_libraries(test_phase2_jit_bridge PRIVATE multifeed_core)

add_executable(test_phase2_ab_arbiter tests/test_phase2_ab_arbiter.cpp)
target_link_libraries(test_phase2_ab_arbiter PRIVATE multifeed_core)

add_executable(test_phase3_feature_bridge tests/test_phase3_feature_bridge.cpp)
target_link_libraries(test_phase3_feature_bridge PRIVATE multifeed_core)

add_executable(test_phase3_feature_pipeline tests/test_phase3_feature_pipeline.cpp)
target_link_libraries(test_phase3_feature_pipeline PRIVATE multifeed_core)

add_executable(test_phase3_order_book_nbbo tests/test_phase3_order_book_nbbo.cpp)
target_link_libraries(test_phase3_order_book_nbbo PRIVATE multifeed_core)

if(MF_BUILD_PHASE4)
  add_executable(test_phase4_sim_matching_engine tests/test_phase4_sim_matching_engine.cpp)
  target_link_libraries(test_phase4_sim_matching_engine PRIVATE multifeed_core)

  add_executable(test_phase4_market_making_strategy tests/test_phase4_market_making_strategy.cpp)
  target_link_libraries(test_phase4_market_making_strategy PRIVATE multifeed_core)

  add_executable(test_phase4_pnl_accountant tests/test_phase4_pnl_accountant.cpp)
  target_link_libraries(test_phase4_pnl_accountant PRIVATE multifeed_core)

  add_executable(test_phase4_backtest_runner tests/test_phase4_backtest_runner.cpp)
  target_link_libraries(test_phase4_backtest_runner PRIVATE multifeed_core)
endif()

if(MF_ENABLE_FUZZ)
  add_executable(fuzz_nasdaq_parser tests/fuzz/fuzz_nasdaq_parser.cpp)
  target_link_libraries(fuzz_nasdaq_parser PRIVATE multifeed_core)
  target_compile_options(fuzz_nasdaq_parser PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(fuzz_nasdaq_parser PRIVATE -fsanitize=fuzzer,address,undefined)

  add_executable(fuzz_iex_parser tests/fuzz/fuzz_iex_parser.cpp)
  target_link_libraries(fuzz_iex_parser PRIVATE multifeed_core)
  target_compile_options(fuzz_iex_parser PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(fuzz_iex_parser PRIVATE -fsanitize=fuzzer,address,undefined)

  add_executable(fuzz_cboe_parser tests/fuzz/fuzz_cboe_parser.cpp)
  target_link_libraries(fuzz_cboe_parser PRIVATE multifeed_core)
  target_compile_options(fuzz_cboe_parser PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(fuzz_cboe_parser PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
