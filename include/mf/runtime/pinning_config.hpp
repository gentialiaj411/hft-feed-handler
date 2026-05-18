#pragma once

#include <cstdlib>
#include <string>

namespace mf::runtime {

// Schema:
// MF_PIN_PRODUCER_CPU=2
// MF_PIN_CONSUMER_CPU=3
// MF_PIN_REALTIME=0|1
// MF_PIN_RT_PRIORITY=50
// MF_PIN_RING_NUMA_NODE=0
// MF_PIN_RING_HUGEPAGES=0|1
struct PinningConfig {
  int producer_cpu{-1};
  int consumer_cpu{-1};
  bool realtime{false};
  int rt_priority{50};
  int ring_numa_node{0};
  bool ring_hugepages{false};
};

inline bool parse_boolish(const std::string& v, bool dflt) {
  if (v == "1" || v == "true" || v == "on") return true;
  if (v == "0" || v == "false" || v == "off") return false;
  return dflt;
}

inline PinningConfig pinning_config_from_env() {
  PinningConfig cfg{};
  auto get = [](const char* key) -> const char* { return std::getenv(key); };
  if (const char* v = get("MF_PIN_PRODUCER_CPU")) cfg.producer_cpu = std::atoi(v);
  if (const char* v = get("MF_PIN_CONSUMER_CPU")) cfg.consumer_cpu = std::atoi(v);
  if (const char* v = get("MF_PIN_REALTIME")) cfg.realtime = parse_boolish(v, cfg.realtime);
  if (const char* v = get("MF_PIN_RT_PRIORITY")) cfg.rt_priority = std::atoi(v);
  if (const char* v = get("MF_PIN_RING_NUMA_NODE")) cfg.ring_numa_node = std::atoi(v);
  if (const char* v = get("MF_PIN_RING_HUGEPAGES")) cfg.ring_hugepages = parse_boolish(v, cfg.ring_hugepages);
  return cfg;
}

inline PinningConfig pinning_config_from_cli(int argc, char** argv, PinningConfig cfg = pinning_config_from_env()) {
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&](int idx) -> const char* { return (idx + 1 < argc) ? argv[idx + 1] : nullptr; };
    if (a == "--producer-cpu" && next(i) != nullptr) cfg.producer_cpu = std::atoi(argv[++i]);
    else if (a == "--consumer-cpu" && next(i) != nullptr) cfg.consumer_cpu = std::atoi(argv[++i]);
    else if (a == "--realtime") cfg.realtime = true;
    else if (a == "--rt-priority" && next(i) != nullptr) cfg.rt_priority = std::atoi(argv[++i]);
    else if (a == "--ring-numa-node" && next(i) != nullptr) cfg.ring_numa_node = std::atoi(argv[++i]);
    else if (a == "--hugepages") cfg.ring_hugepages = true;
  }
  return cfg;
}

}  // namespace mf::runtime
