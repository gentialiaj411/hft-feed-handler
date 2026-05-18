#pragma once

#include <cstddef>

namespace mf::os {

// If libnuma is unavailable, allocation falls back to aligned_alloc(64, ...),
// current_numa_node()/numa_node_of_cpu() return -1, and numa_available()==false.
bool numa_available() noexcept;
int current_numa_node() noexcept;
int numa_node_of_cpu(int cpu) noexcept;
void* numa_alloc_on_node(std::size_t bytes, int node) noexcept;
void numa_free(void* ptr, std::size_t bytes) noexcept;

}  // namespace mf::os
