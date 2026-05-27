#include <cstdio>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "mf/service/replay_catalog.hpp"
#include "mf/service/tick_replay_service.hpp"

namespace {
std::string arg(int argc, char** argv, const std::string& key, const std::string& dflt) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == key) {
      return argv[i + 1];
    }
  }
  return dflt;
}
}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::fprintf(stderr, "tick_service_server requires Linux (mmap journal readers)\n");
  return 1;
#else
  const std::string journal = arg(argc, argv, "--journal", "");
  const std::string nbbo = arg(argc, argv, "--nbbo", "");
  const std::string listen = arg(argc, argv, "--listen", "0.0.0.0:50051");
  if (journal.empty()) {
    std::fprintf(stderr,
        "usage: tick_service_server --journal <book.journal> [--nbbo <nbbo.journal>] "
        "[--listen host:port]\n");
    return 2;
  }

  auto catalog = std::make_shared<mf::service::ReplayCatalog>();
  if (!catalog->load(journal, nbbo)) {
    std::fprintf(stderr, "failed to load journals (book=%s nbbo=%s)\n", journal.c_str(),
        nbbo.empty() ? "<none>" : nbbo.c_str());
    return 1;
  }

  std::printf(
      "tick_service_server: book_records=%llu nbbo_records=%llu listen=%s mode=recorded_replay\n",
      static_cast<unsigned long long>(catalog->book_record_count),
      static_cast<unsigned long long>(catalog->nbbo_record_count),
      listen.c_str());
  std::fflush(stdout);

  mf::service::TickReplayServiceImpl service(catalog);
  grpc::ServerBuilder builder;
  builder.AddListeningPort(listen, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (!server) {
    std::fprintf(stderr, "failed to start gRPC server on %s\n", listen.c_str());
    return 1;
  }
  std::printf("ready\n");
  std::fflush(stdout);
  server->Wait();
  return 0;
#endif
}
