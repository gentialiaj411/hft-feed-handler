#include "mf/wire/event_loop.hpp"

#ifdef __linux__
#include <sys/epoll.h>
#include <unistd.h>

#include "mf/core/time.hpp"
#include "mf/wire/feed_session.hpp"

namespace mf::wire {

WireEventLoop::WireEventLoop() { epoll_fd_ = ::epoll_create1(0); }

WireEventLoop::~WireEventLoop() {
  if (epoll_fd_ >= 0) ::close(epoll_fd_);
}

bool WireEventLoop::add_session(FeedSession* session) {
  if (session == nullptr || session->fd() < 0 || epoll_fd_ < 0) return false;
  epoll_event ev{};
  ev.events = EPOLLIN | EPOLLET;
  ev.data.ptr = session;
  if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, session->fd(), &ev) != 0) return false;
  sessions_.push_back(session);
  return true;
}

void WireEventLoop::run_for(std::chrono::nanoseconds budget) {
  if (epoll_fd_ < 0) return;
  const std::uint64_t start = mf::core::monotonic_raw_now_ns();
  constexpr int kMaxEvents = 32;
  epoll_event events[kMaxEvents]{};
  while (!stop_.load()) {
    const std::uint64_t now = mf::core::monotonic_raw_now_ns();
    if (now - start >= static_cast<std::uint64_t>(budget.count())) break;
    const int n = ::epoll_wait(epoll_fd_, events, kMaxEvents, 10);
    if (n <= 0) continue;
    for (int i = 0; i < n; ++i) {
      auto* session = static_cast<FeedSession*>(events[i].data.ptr);
      if (session == nullptr) continue;
      // Edge-triggered requires draining until EAGAIN; FeedSession::poll performs that drain.
      session->poll();
    }
  }
}

void WireEventLoop::run() {
  while (!stop_.load()) {
    run_for(std::chrono::milliseconds(100));
  }
}

}  // namespace mf::wire
#endif
