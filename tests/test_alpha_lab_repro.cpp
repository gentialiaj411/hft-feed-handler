#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

#if defined(__linux__)

int main() {
  const std::string tearsheet = "bench/results/alpha_lab/tearsheet.json";
  std::ifstream in(tearsheet);
  if (!in) {
    std::puts("SKIP: tearsheet.json not generated yet; run tools/alpha_lab first");
    return 0;
  }
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  assert(content.find("\"journal\"") != std::string::npos);
  assert(content.find("\"n_trials\"") != std::string::npos);
  assert(content.find("\"baseline_sharpe\"") != std::string::npos);

  const std::string manifest = "bench/results/alpha_lab/baseline_manifest.json";
  std::ifstream man(manifest);
  assert(man.good());
  std::puts("PASS alpha_lab_repro");
  return 0;
}

#else

int main() {
  std::puts("SKIP: alpha_lab repro test is Linux-only");
  return 0;
}

#endif
