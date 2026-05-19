#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string quote(const std::filesystem::path& p) {
  std::ostringstream os;
  os << '"' << p.string() << '"';
  return os.str();
}

}  // namespace

int main(int argc, char** argv) {
  const auto exe_dir = (argc > 0 && argv[0] != nullptr)
                           ? std::filesystem::absolute(argv[0]).parent_path()
                           : std::filesystem::current_path();
  const auto bench_sweep =
#if defined(_WIN32)
      exe_dir / "bench_sweep.exe";
#else
      exe_dir / "bench_sweep";
#endif
  const std::string cmd = quote(bench_sweep) + " --events 10000 --warmup-reps 1 --measured-reps 1";
  const int rc = std::system(cmd.c_str());
  assert(rc == 0);
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
