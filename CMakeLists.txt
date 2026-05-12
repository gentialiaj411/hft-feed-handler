cmake_minimum_required(VERSION 3.20)
project(MultiFeed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MF_ENABLE_FUZZ "Enable libFuzzer targets" OFF)

add_library(multifeed_core
    src/core/crc32.cpp
    src/core/time.cpp
    src/phase2/deterministic_merger.cpp
    src/phase2/jit_bridge.cpp
    src/phase2/pipeline.cpp
    src/phase2/recovery.cpp
    src/phase2/recovery_simulator.cpp
    src/phase2/sequence_tracker.cpp
    src/proto/nasdaq/itch50_parser.cpp
    src/proto/iex/deep_parser.cpp
    src/proto/cboe/pitch_parser.cpp
)

target_include_directories(multifeed_core PUBLIC include)

target_compile_options(multifeed_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(phase1_parser_validate src/apps/phase1_parser_validate_main.cpp)
target_link_libraries(phase1_parser_validate PRIVATE multifeed_core)

add_executable(phase2_pipeline_bench src/apps/phase2_pipeline_bench_main.cpp)
target_link_libraries(phase2_pipeline_bench PRIVATE multifeed_core)

add_executable(test_iex_deep_parser tests/test_iex_deep_parser.cpp)
target_link_libraries(test_iex_deep_parser PRIVATE multifeed_core)

add_executable(test_nasdaq_itch_parser tests/test_nasdaq_itch_parser.cpp)
target_link_libraries(test_nasdaq_itch_parser PRIVATE multifeed_core)

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
