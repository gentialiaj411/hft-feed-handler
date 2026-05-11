#pragma once

#include "market_data.h"
#include "ring_buffer.h"

#include <atomic>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace market {

#ifdef _WIN32
using socket_handle_t = SOCKET;
constexpr socket_handle_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_handle_t = int;
constexpr socket_handle_t kInvalidSocket = -1;
#endif

class UDPReceiver {
public:

    UDPReceiver(const std::string& multicast_ip, uint16_t port);

    ~UDPReceiver();

    void start(SPSCRingBuffer<RawMessage, 65536>& output_queue);

    void stop();

    uint64_t messages_received() const;

    uint64_t bytes_received() const;

    uint64_t ring_push_failures() const;

private:

    void run(SPSCRingBuffer<RawMessage, 65536>& output_queue);

    socket_handle_t socket_fd_{kInvalidSocket};
    std::string multicast_ip_;
    uint16_t port_{0};

    std::atomic<bool> running_{false};
    std::thread receiver_thread_;

    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> bytes_received_{0};
    std::atomic<uint64_t> push_failures_{0};
};

}

}
