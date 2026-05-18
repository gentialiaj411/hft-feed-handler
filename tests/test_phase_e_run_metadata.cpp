#include <cassert>
#include <cstdio>

#include "mf/bench/run_metadata.hpp"

int main(int argc, char** argv) {
  auto m = mf::bench::capture_run_metadata(argc, argv);
  assert(!m.host.empty());
  assert(m.cpu_count >= 0);
  if (m.git_sha == "unknown") {
    std::printf("SKIP: git sha unavailable\n");
    return 0;
  }
  assert(!m.git_sha.empty());
  return 0;
}
