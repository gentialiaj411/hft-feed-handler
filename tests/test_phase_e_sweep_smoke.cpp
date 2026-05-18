#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
  const int rc =
#if defined(_WIN32)
      std::system("bench_sweep.exe --events 10000 --warmup-reps 1 --measured-reps 1");
#else
      std::system("./bench_sweep --events 10000 --warmup-reps 1 --measured-reps 1");
#endif
  (void)rc;
  bool found_summary = false;
  for (const auto& ent : std::filesystem::directory_iterator("bench/results")) {
    const auto p = ent.path().filename().string();
    if (p.rfind("sweep_summary_", 0) == 0) {
      found_summary = true;
      std::ifstream is(ent.path());
      std::string s((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
      assert(s.find("\"artifacts\"") != std::string::npos);
      break;
    }
  }
  assert(found_summary);
  return 0;
}
