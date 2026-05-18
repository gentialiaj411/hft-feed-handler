#pragma once

#include <string>

namespace mf::os {

// set_realtime_fifo() requires CAP_SYS_NICE and appropriate rtprio limits.
bool pin_current_thread(int cpu) noexcept;
bool set_thread_name(const std::string& name) noexcept;
bool set_realtime_fifo(int priority) noexcept;
int last_errno() noexcept;

}  // namespace mf::os
