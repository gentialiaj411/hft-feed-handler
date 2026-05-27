cmake_minimum_required(VERSION 3.20)
project(MultiFeed LANGUAGES CXX)
enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/FS>)

option(MF_ENABLE_FUZZ "Enable libFuzzer targets" OFF)
option(MF_STRICT_WARNINGS "Treat warnings as errors for local/CI hardening" OFF)
option(MF_BUILD_PHASE4 "Build Phase 4 backtest engine + tests" ON)
option(MF_BUILD_PHASE_B "Build Phase B wire ingestion + tests (Linux only)" ON)
option(MF_BUILD_AF_XDP "Build AF_XDP loopback transport + tests (Linux + libbpf)" ON)
option(MF_BUILD_JOURNAL "Build Phase C journal + replay tooling (Linux only)" ON)
option(MF_BUILD_PHASE_D "Build Phase D NUMA/pinning/hugepage tooling (Linux only)" ON)
option(MF_BUILD_BENCH_SWEEP "Build Phase E bench sweep + evidence tooling" ON)
option(MF_BUILD_REPLAY_SERVICE "Build gRPC recorded-data replay/query service (Linux + protoc/grpc)" ON)
option(MF_ENABLE_AVX2_HOTPATH "Compile AVX2 parser hot path where supported" ON)

set(MF_HAS_LIBNUMA OFF)
set(MF_HAS_LIBBPF OFF)
if(MF_BUILD_AF_XDP AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_library(BPF_LIB bpf)
  find_path(BPF_INCLUDE_DIR bpf/libbpf.h)
  if(BPF_LIB AND BPF_INCLUDE_DIR)
    set(MF_HAS_LIBBPF ON)
  else()
    message(WARNING "libbpf not found; AF_XDP targets will be disabled.")
  endif()
endif()

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
    src/phase3/intrusive_order_book.cpp
    src/phase3/book_snapshot.cpp
    src/phase3/nbbo_consolidator.cpp
    src/phase3/feature_pipeline.cpp
    src/phase3/feature_bridge.cpp
    src/research/strategy_engine.cpp
    src/research/signals/ofi.cpp
    src/research/strategies/ofi_strategy.cpp
    src/research/metrics/risk_metrics.cpp
    src/proto/nasdaq/itch50_parser.cpp
    src/proto/iex/deep_parser.cpp
    src/proto/cboe/pitch_parser.cpp
    src/parsers/mdp3/sbe_header.cpp
    src/parsers/mdp3/cert_bin_reader.cpp
    src/parsers/mdp3/mdp3_parser.cpp
    src/bench/histogram.cpp
    src/bench/run_metadata.cpp
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

if(MF_BUILD_AF_XDP AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND MF_HAS_LIBBPF)
  target_sources(multifeed_core PRIVATE
      src/transport/afxdp/udp_payload.cpp
      src/transport/afxdp/receiver.cpp
      src/transport/afxdp/feed_session.cpp
  )
  target_compile_definitions(multifeed_core PRIVATE MF_HAS_LIBBPF=1)
  target_include_directories(multifeed_core PRIVATE ${BPF_INCLUDE_DIR})
  target_link_libraries(multifeed_core PRIVATE ${BPF_LIB})

  find_program(CLANG_BIN clang)
  if(CLANG_BIN)
    set(MF_AF_XDP_BPF_OBJ "${CMAKE_BINARY_DIR}/afxdp_redirect.bpf.o")
    add_custom_command(
        OUTPUT ${MF_AF_XDP_BPF_OBJ}
        COMMAND ${CLANG_BIN} -g -O2 -target bpf -c
            ${CMAKE_SOURCE_DIR}/bpf/afxdp_redirect.bpf.c -o ${MF_AF_XDP_BPF_OBJ}
        DEPENDS ${CMAKE_SOURCE_DIR}/bpf/afxdp_redirect.bpf.c
        COMMENT "Compiling AF_XDP BPF object"
    )
    add_custom_target(afxdp_bpf_obj ALL DEPENDS ${MF_AF_XDP_BPF_OBJ})
  endif()
elseif(MF_BUILD_AF_XDP)
  message(STATUS "MF_BUILD_AF_XDP requested but disabled: requires Linux + libbpf.")
endif()

if(MF_BUILD_JOURNAL AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(multifeed_core PRIVATE
      src/journal/journal_writer.cpp
      src/journal/journal_reader.cpp
      src/journal/journaling_sink.cpp
      src/journal/nbbo_journal_writer.cpp
      src/journal/nbbo_journal_reader.cpp
      src/phase3/nbbo_journal_pipeline.cpp
      src/research/event_store.cpp
  )
  if(MF_BUILD_PHASE4)
    target_sources(multifeed_core PRIVATE
        src/research/experiment_runner.cpp
        src/research/metrics/bootstrap.cpp
    )
  endif()
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

if(MF_BUILD_BENCH_SWEEP)
  add_executable(bench_sweep tools/bench_sweep.cpp)
  target_link_libraries(bench_sweep PRIVATE multifeed_core)

  add_executable(test_phase_e_histogram tests/test_phase_e_histogram.cpp)
  target_link_libraries(test_phase_e_histogram PRIVATE multifeed_core)

  add_executable(test_phase_e_run_metadata tests/test_phase_e_run_metadata.cpp)
  target_link_libraries(test_phase_e_run_metadata PRIVATE multifeed_core)

  add_executable(test_phase_e_sweep_smoke tests/test_phase_e_sweep_smoke.cpp)
  target_link_libraries(test_phase_e_sweep_smoke PRIVATE multifeed_core)
  add_dependencies(test_phase_e_sweep_smoke bench_sweep)
endif()

target_include_directories(multifeed_core PUBLIC include)

target_compile_options(multifeed_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /FS>
    $<$<AND:$<BOOL:${MF_STRICT_WARNINGS}>,$<CXX_COMPILER_ID:GNU,Clang>>:-Werror>
    $<$<AND:$<BOOL:${MF_STRICT_WARNINGS}>,$<CXX_COMPILER_ID:MSVC>>:/WX>
)

if(MF_ENABLE_AVX2_HOTPATH)
  target_compile_options(multifeed_core PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang>:-mavx2>
      $<$<CXX_COMPILER_ID:MSVC>:/arch:AVX2>
  )
endif()

add_executable(phase1_parser_validate src/apps/phase1_parser_validate_main.cpp)
target_link_libraries(phase1_parser_validate PRIVATE multifeed_core)

add_executable(phase2_pipeline_bench src/apps/phase2_pipeline_bench_main.cpp)
target_link_libraries(phase2_pipeline_bench PRIVATE multifeed_core)

add_executable(phase2_ab_evidence src/apps/phase2_ab_evidence_main.cpp)
target_link_libraries(phase2_ab_evidence PRIVATE multifeed_core)

add_executable(phase3_feature_latency_bench tools/feature_latency_probe.cpp)
target_link_libraries(phase3_feature_latency_bench PRIVATE multifeed_core)

add_executable(latency_stage_bench tools/latency_stage_bench.cpp)
target_link_libraries(latency_stage_bench PRIVATE multifeed_core)

add_executable(book_engine_bench tools/book_engine_bench.cpp)
target_link_libraries(book_engine_bench PRIVATE multifeed_core)

add_executable(decoder_simd_bench tools/decoder_simd_bench.cpp)
target_link_libraries(decoder_simd_bench PRIVATE multifeed_core)

add_executable(spmc_contention_bench tools/spmc_contention_bench.cpp)
target_link_libraries(spmc_contention_bench PRIVATE multifeed_core)

add_executable(feed_hot_path_bench tools/feed_hot_path_bench.cpp)
target_link_libraries(feed_hot_path_bench PRIVATE multifeed_core)

add_executable(spmc_fanout_bench tools/spmc_fanout_bench.cpp)
target_link_libraries(spmc_fanout_bench PRIVATE multifeed_core)

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

if(MF_BUILD_AF_XDP AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND MF_HAS_LIBBPF)
  add_executable(afxdp_itch_producer tools/afxdp_itch_producer.cpp)
  target_link_libraries(afxdp_itch_producer PRIVATE multifeed_core)

  add_executable(afxdp_itch_consumer tools/afxdp_itch_consumer.cpp)
  target_link_libraries(afxdp_itch_consumer PRIVATE multifeed_core)

  add_executable(afxdp_loopback_bench tools/afxdp_loopback_bench.cpp)
  target_link_libraries(afxdp_loopback_bench PRIVATE multifeed_core)

  add_executable(test_afxdp_loopback tests/test_afxdp_loopback.cpp)
  target_link_libraries(test_afxdp_loopback PRIVATE multifeed_core)
  if(TARGET afxdp_bpf_obj)
    add_dependencies(test_afxdp_loopback afxdp_bpf_obj)
    add_dependencies(afxdp_itch_consumer afxdp_bpf_obj)
    add_dependencies(afxdp_loopback_bench afxdp_bpf_obj)
  endif()
endif()

if(MF_BUILD_JOURNAL AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(ab_failover_validation tools/ab_failover_validation.cpp)
  target_link_libraries(ab_failover_validation PRIVATE multifeed_core)

  add_executable(book_reconcile tools/book_reconcile.cpp)
  target_link_libraries(book_reconcile PRIVATE multifeed_core)

  add_executable(sharded_replay_bench tools/sharded_replay_bench.cpp)
  target_link_libraries(sharded_replay_bench PRIVATE multifeed_core)

  add_executable(itch_to_journal tools/itch_to_journal.cpp)
  target_link_libraries(itch_to_journal PRIVATE multifeed_core)

  add_executable(mdp3_replay_bench tools/mdp3_replay_bench.cpp)
  target_link_libraries(mdp3_replay_bench PRIVATE multifeed_core)

  add_executable(journal_replay tools/journal_replay.cpp)
  target_link_libraries(journal_replay PRIVATE multifeed_core)

  add_executable(journal_diff tools/journal_diff.cpp)
  target_link_libraries(journal_diff PRIVATE multifeed_core)

  add_executable(nbbo_journal_from_book tools/nbbo_journal_from_book.cpp)
  target_link_libraries(nbbo_journal_from_book PRIVATE multifeed_core)

  add_executable(nbbo_journal_diff tools/nbbo_journal_diff.cpp)
  target_link_libraries(nbbo_journal_diff PRIVATE multifeed_core)

  add_executable(nbbo_replay_bench tools/nbbo_replay_bench.cpp)
  target_link_libraries(nbbo_replay_bench PRIVATE multifeed_core)

  add_executable(test_nbbo_journal_determinism tests/test_nbbo_journal_determinism.cpp)
  target_link_libraries(test_nbbo_journal_determinism PRIVATE multifeed_core)

  if(MF_BUILD_PHASE_B)
    add_executable(journal_capture_loopback tools/journal_capture_loopback.cpp)
    target_link_libraries(journal_capture_loopback PRIVATE multifeed_core)
  endif()

  add_executable(test_phase_c_journal_roundtrip tests/test_phase_c_journal_roundtrip.cpp)
  target_link_libraries(test_phase_c_journal_roundtrip PRIVATE multifeed_core)

  add_executable(test_phase_c_journal_torn_record tests/test_phase_c_journal_torn_record.cpp)
  target_link_libraries(test_phase_c_journal_torn_record PRIVATE multifeed_core)

  if(MF_BUILD_PHASE_B)
    add_executable(test_phase_c_replay_determinism tests/test_phase_c_replay_determinism.cpp)
    target_link_libraries(test_phase_c_replay_determinism PRIVATE multifeed_core)
  endif()

  add_executable(test_phase_c_journal_format_version tests/test_phase_c_journal_format_version.cpp)
  target_link_libraries(test_phase_c_journal_format_version PRIVATE multifeed_core)

  add_executable(test_phase_c_mixed_event_replay tests/test_phase_c_mixed_event_replay.cpp)
  target_link_libraries(test_phase_c_mixed_event_replay PRIVATE multifeed_core)

  add_executable(test_ab_failover_validation tests/test_ab_failover_validation.cpp)
  target_link_libraries(test_ab_failover_validation PRIVATE multifeed_core)
  add_dependencies(test_ab_failover_validation ab_failover_validation)

  add_executable(test_research_event_store tests/test_research_event_store.cpp)
  target_link_libraries(test_research_event_store PRIVATE multifeed_core)

  if(MF_BUILD_PHASE4)
    add_executable(experiment_runner tools/experiment_runner.cpp)
    target_link_libraries(experiment_runner PRIVATE multifeed_core)

    add_executable(ofi_backtest tools/ofi_backtest.cpp)
    target_link_libraries(ofi_backtest PRIVATE multifeed_core)

    add_executable(test_bootstrap tests/test_bootstrap.cpp)
    target_link_libraries(test_bootstrap PRIVATE multifeed_core)

    add_executable(make_research_journal tools/make_research_journal.cpp)
    target_link_libraries(make_research_journal PRIVATE multifeed_core)

    add_executable(test_research_experiment_runner tests/test_research_experiment_runner.cpp)
    target_link_libraries(test_research_experiment_runner PRIVATE multifeed_core)
  endif()

  add_executable(test_replay_service_symbol tests/test_replay_service_symbol.cpp)
  target_link_libraries(test_replay_service_symbol PRIVATE multifeed_core)

  if(MF_BUILD_REPLAY_SERVICE)
    find_package(Protobuf REQUIRED)
    find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
    if(NOT GRPC_CPP_PLUGIN_EXECUTABLE)
      message(STATUS "grpc_cpp_plugin not found; MF_BUILD_REPLAY_SERVICE disabled.")
      set(MF_BUILD_REPLAY_SERVICE OFF)
    endif()
  endif()

  if(MF_BUILD_REPLAY_SERVICE)
    set(MF_TICK_PROTO ${CMAKE_SOURCE_DIR}/proto/tick_service.proto)
    set(MF_TICK_GEN_DIR ${CMAKE_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${MF_TICK_GEN_DIR})
    set(MF_TICK_PB_CC ${MF_TICK_GEN_DIR}/tick_service.pb.cc)
    set(MF_TICK_PB_H ${MF_TICK_GEN_DIR}/tick_service.pb.h)
    set(MF_TICK_GRPC_CC ${MF_TICK_GEN_DIR}/tick_service.grpc.pb.cc)
    set(MF_TICK_GRPC_H ${MF_TICK_GEN_DIR}/tick_service.grpc.pb.h)
    add_custom_command(
        OUTPUT ${MF_TICK_PB_CC} ${MF_TICK_PB_H} ${MF_TICK_GRPC_CC} ${MF_TICK_GRPC_H}
        COMMAND protobuf::protoc
        ARGS --proto_path=${CMAKE_SOURCE_DIR}/proto
             --cpp_out=${MF_TICK_GEN_DIR}
             --grpc_out=${MF_TICK_GEN_DIR}
             --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
             ${MF_TICK_PROTO}
        DEPENDS ${MF_TICK_PROTO}
        COMMENT "Generating tick_service protobuf/gRPC sources"
    )
    add_library(tick_service_proto STATIC ${MF_TICK_PB_CC} ${MF_TICK_GRPC_CC})
    target_include_directories(tick_service_proto PUBLIC ${MF_TICK_GEN_DIR})
    target_link_libraries(tick_service_proto PUBLIC protobuf::libprotobuf)
    find_package(gRPC CONFIG QUIET)
    if(gRPC_FOUND)
      target_link_libraries(tick_service_proto PUBLIC gRPC::grpc++)
    else()
      find_package(PkgConfig QUIET)
      if(PkgConfig_FOUND)
        pkg_check_modules(GRPCPP grpc++)
        if(GRPCPP_FOUND)
          target_link_libraries(tick_service_proto PUBLIC ${GRPCPP_LIBRARIES})
          target_include_directories(tick_service_proto PUBLIC ${GRPCPP_INCLUDE_DIRS})
          target_compile_options(tick_service_proto PUBLIC ${GRPCPP_CFLAGS_OTHER})
        else()
          message(STATUS "libgrpc++ not found via pkg-config; MF_BUILD_REPLAY_SERVICE disabled.")
          set(MF_BUILD_REPLAY_SERVICE OFF)
        endif()
      else()
        message(STATUS "PkgConfig not found; MF_BUILD_REPLAY_SERVICE disabled.")
        set(MF_BUILD_REPLAY_SERVICE OFF)
      endif()
    endif()
  endif()

  if(MF_BUILD_REPLAY_SERVICE)
    add_library(multifeed_replay_service
        src/service/replay_catalog.cpp
        src/service/tick_replay_service.cpp
    )
    target_include_directories(multifeed_replay_service PUBLIC include ${MF_TICK_GEN_DIR})
    target_link_libraries(multifeed_replay_service PUBLIC multifeed_core tick_service_proto)

    add_executable(tick_service_server tools/tick_service_server.cpp)
    target_link_libraries(tick_service_server PRIVATE multifeed_replay_service)
  endif()
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

add_executable(test_nasdaq_itch_simd_parser tests/test_nasdaq_itch_simd_parser.cpp)
target_link_libraries(test_nasdaq_itch_simd_parser PRIVATE multifeed_core)

add_executable(test_cboe_pitch_parser tests/test_cboe_pitch_parser.cpp)
target_link_libraries(test_cboe_pitch_parser PRIVATE multifeed_core)

add_executable(test_mdp3_parser tests/test_mdp3_parser.cpp)
target_link_libraries(test_mdp3_parser PRIVATE multifeed_core)

add_executable(test_phase2_sequence_tracker tests/test_phase2_sequence_tracker.cpp)
target_link_libraries(test_phase2_sequence_tracker PRIVATE multifeed_core)

add_executable(test_core_spmc_seqlock_ring tests/test_core_spmc_seqlock_ring.cpp)
target_link_libraries(test_core_spmc_seqlock_ring PRIVATE multifeed_core)

add_executable(test_phase2_recovery_merge tests/test_phase2_recovery_merge.cpp)
target_link_libraries(test_phase2_recovery_merge PRIVATE multifeed_core)

add_executable(test_phase2_recovery_simulator tests/test_phase2_recovery_simulator.cpp)
target_link_libraries(test_phase2_recovery_simulator PRIVATE multifeed_core)

add_executable(test_phase2_recovery_pending_dedupe tests/test_phase2_recovery_pending_dedupe.cpp)
target_link_libraries(test_phase2_recovery_pending_dedupe PRIVATE multifeed_core)

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

add_executable(test_phase3_book_snapshot tests/test_phase3_book_snapshot.cpp)
target_link_libraries(test_phase3_book_snapshot PRIVATE multifeed_core)

add_executable(test_phase3_book_reconciliation tests/test_phase3_book_reconciliation.cpp)
target_link_libraries(test_phase3_book_reconciliation PRIVATE multifeed_core)

add_executable(test_phase3_intrusive_order_book tests/test_phase3_intrusive_order_book.cpp)
target_link_libraries(test_phase3_intrusive_order_book PRIVATE multifeed_core)

add_executable(test_research_simulation_clock tests/test_research_simulation_clock.cpp)
target_link_libraries(test_research_simulation_clock PRIVATE multifeed_core)

add_executable(test_research_strategy_engine tests/test_research_strategy_engine.cpp)
target_link_libraries(test_research_strategy_engine PRIVATE multifeed_core)

add_executable(test_ofi_signal tests/test_ofi_signal.cpp)
target_link_libraries(test_ofi_signal PRIVATE multifeed_core)

add_executable(test_ofi_strategy tests/test_ofi_strategy.cpp)
target_link_libraries(test_ofi_strategy PRIVATE multifeed_core)

add_executable(test_risk_metrics tests/test_risk_metrics.cpp)
target_link_libraries(test_risk_metrics PRIVATE multifeed_core)

add_executable(test_runtime_shard_router tests/test_runtime_shard_router.cpp)
target_link_libraries(test_runtime_shard_router PRIVATE multifeed_core)

add_executable(test_runtime_sharded_pipeline_determinism tests/test_runtime_sharded_pipeline_determinism.cpp)
target_link_libraries(test_runtime_sharded_pipeline_determinism PRIVATE multifeed_core)

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

if(MF_ENABLE_FUZZ AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
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
elseif(MF_ENABLE_FUZZ)
  message(WARNING "MF_ENABLE_FUZZ requested but disabled: libFuzzer targets require Clang.")
endif()

function(mf_add_test test_name target_name label)
  if(TARGET ${target_name})
    add_test(NAME ${test_name} COMMAND ${target_name})
    set_tests_properties(${test_name} PROPERTIES LABELS "${label}")
  endif()
endfunction()

mf_add_test(parser_nasdaq_itch test_nasdaq_itch_parser parser)
mf_add_test(parser_nasdaq_itch_simd test_nasdaq_itch_simd_parser parser)
mf_add_test(parser_iex_deep test_iex_deep_parser parser)
mf_add_test(parser_cboe_pitch test_cboe_pitch_parser parser)
mf_add_test(parser_mdp3 test_mdp3_parser parser)

mf_add_test(core_spmc_seqlock_ring test_core_spmc_seqlock_ring core)

mf_add_test(phase2_sequence_tracker test_phase2_sequence_tracker phase2)
mf_add_test(phase2_recovery_merge test_phase2_recovery_merge phase2)
mf_add_test(phase2_recovery_simulator test_phase2_recovery_simulator phase2)
mf_add_test(phase2_recovery_pending_dedupe test_phase2_recovery_pending_dedupe phase2)
mf_add_test(phase2_determinism_crc test_phase2_determinism_crc phase2)
mf_add_test(phase2_pipeline test_phase2_pipeline phase2)
mf_add_test(phase2_jit_bridge test_phase2_jit_bridge phase2)
mf_add_test(phase2_ab_arbiter test_phase2_ab_arbiter phase2)

mf_add_test(phase3_feature_bridge test_phase3_feature_bridge phase3)
mf_add_test(phase3_feature_pipeline test_phase3_feature_pipeline phase3)
mf_add_test(phase3_order_book_nbbo test_phase3_order_book_nbbo phase3)
mf_add_test(phase3_book_snapshot test_phase3_book_snapshot phase3)
mf_add_test(phase3_book_reconciliation test_phase3_book_reconciliation phase3)
mf_add_test(phase3_intrusive_order_book test_phase3_intrusive_order_book phase3)

mf_add_test(phase4_sim_matching_engine test_phase4_sim_matching_engine phase4)
mf_add_test(phase4_market_making_strategy test_phase4_market_making_strategy phase4)
mf_add_test(phase4_pnl_accountant test_phase4_pnl_accountant phase4)
mf_add_test(phase4_backtest_runner test_phase4_backtest_runner phase4)

mf_add_test(phase_b_framing test_phase_b_framing phase_b)
mf_add_test(phase_b_udp_loopback test_phase_b_udp_loopback phase_b)
mf_add_test(phase_b_ab_failover test_phase_b_ab_failover phase_b)
mf_add_test(phase_b_burst_loss test_phase_b_burst_loss phase_b)
mf_add_test(afxdp_loopback test_afxdp_loopback phase_b)

mf_add_test(phase_c_journal_roundtrip test_phase_c_journal_roundtrip phase_c)
mf_add_test(phase_c_journal_torn_record test_phase_c_journal_torn_record phase_c)
mf_add_test(phase_c_replay_determinism test_phase_c_replay_determinism phase_c)
mf_add_test(phase_c_journal_format_version test_phase_c_journal_format_version phase_c)
mf_add_test(phase_c_mixed_event_replay test_phase_c_mixed_event_replay phase_c)
mf_add_test(nbbo_journal_determinism test_nbbo_journal_determinism phase3)
mf_add_test(ab_failover_validation test_ab_failover_validation phase2)

mf_add_test(phase_d_cpu_affinity test_phase_d_cpu_affinity phase_d)
mf_add_test(phase_d_numa test_phase_d_numa phase_d)
mf_add_test(phase_d_hugepages test_phase_d_hugepages phase_d)
mf_add_test(phase_d_spsc_external_storage test_phase_d_spsc_external_storage phase_d)

mf_add_test(phase_e_histogram test_phase_e_histogram phase_e)
mf_add_test(phase_e_run_metadata test_phase_e_run_metadata phase_e)
mf_add_test(phase_e_sweep_smoke test_phase_e_sweep_smoke phase_e)

mf_add_test(research_event_store test_research_event_store research)
mf_add_test(research_simulation_clock test_research_simulation_clock research)
mf_add_test(research_strategy_engine test_research_strategy_engine research)
mf_add_test(ofi_signal test_ofi_signal research)
mf_add_test(ofi_strategy test_ofi_strategy research)
mf_add_test(risk_metrics test_risk_metrics research)
mf_add_test(research_experiment_runner test_research_experiment_runner research)
mf_add_test(bootstrap test_bootstrap research)
mf_add_test(runtime_shard_router test_runtime_shard_router runtime)
mf_add_test(runtime_sharded_pipeline_determinism test_runtime_sharded_pipeline_determinism runtime)

mf_add_test(replay_service_symbol test_replay_service_symbol replay)
