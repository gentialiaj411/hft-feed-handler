#include "mf/bench/run_metadata.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>

#if defined(__linux__)
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace mf::bench {

namespace {
std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
  return s;
}

std::string utc_stamp() {
  const auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  char out[64];
  std::strftime(out, sizeof(out), "%Y%m%dT%H%M%SZ", &tm);
  return out;
}

std::string run_cmd(const char* cmd) {
  std::string out;
#if defined(_WIN32)
  FILE* f = _popen(cmd, "r");
#else
  FILE* f = popen(cmd, "r");
#endif
  if (f == nullptr) return out;
  char buf[256];
  while (std::fgets(buf, static_cast<int>(sizeof(buf)), f) != nullptr) out += buf;
#if defined(_WIN32)
  _pclose(f);
#else
  pclose(f);
#endif
  return trim(out);
}
}  // namespace

RunMetadata capture_run_metadata(int argc, char** argv) {
  RunMetadata m{};
  m.utc_timestamp = utc_stamp();
  m.git_sha = run_cmd("git rev-parse HEAD 2> /dev/null");
  if (m.git_sha.empty()) m.git_sha = "unknown";
  m.build_type =
#if defined(NDEBUG)
      "Release";
#else
      "Debug";
#endif
  m.cxx_flags =
#if defined(__VERSION__)
      __VERSION__;
#else
      "unknown";
#endif
  m.cpu_count = static_cast<int>(std::thread::hardware_concurrency());
  if (argc > 0 && argv != nullptr) {
    std::ostringstream os;
    for (int i = 0; i < argc; ++i) {
      if (i > 0) os << " ";
      os << argv[i];
    }
    m.command_line = os.str();
  }

#if defined(__linux__)
  char hn[256]{};
  if (::gethostname(hn, sizeof(hn)) == 0) m.host = hn;
  utsname u{};
  if (::uname(&u) == 0) m.kernel = u.release;
  std::ifstream cpuinfo("/proc/cpuinfo");
  std::string line;
  while (std::getline(cpuinfo, line)) {
    if (line.rfind("model name", 0) == 0) {
      const auto pos = line.find(':');
      if (pos != std::string::npos) {
        m.cpu_model = trim(line.substr(pos + 1));
        break;
      }
    }
  }
#else
  m.host = "unknown";
  m.kernel = "unknown";
  m.cpu_model = "unknown";
#endif
  return m;
}

std::string run_metadata_to_json(const RunMetadata& m) {
  std::ostringstream os;
  os << "{"
     << "\"git_sha\":\"" << m.git_sha << "\","
     << "\"build_type\":\"" << m.build_type << "\","
     << "\"cxx_flags\":\"" << m.cxx_flags << "\","
     << "\"host\":\"" << m.host << "\","
     << "\"cpu_model\":\"" << m.cpu_model << "\","
     << "\"cpu_count\":" << m.cpu_count << ","
     << "\"kernel\":\"" << m.kernel << "\","
     << "\"utc_timestamp\":\"" << m.utc_timestamp << "\","
     << "\"command_line\":\"" << m.command_line << "\""
     << "}";
  return os.str();
}

}  // namespace mf::bench
