#pragma once

#include <atomic>
#include <chrono>
#include <vector>

namespace mf::wire {

class FeedSession;

class WireEventLoop {
 public:
  WireEventLoop();
  ~WireEventLoop();

  bool add_session(FeedSession* session);
  void run_for(std::chrono::nanoseconds budget);
  void run();
  void stop() noexcept { stop_.store(true); }

 private:
  int epoll_fd_{-1};
  std::vector<FeedSession*> sessions_{};
  std::atomic<bool> stop_{false};
};

}  // namespace mf::wire
