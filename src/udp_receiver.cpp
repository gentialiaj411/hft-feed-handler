
 #include "udp_receiver.h"
#include "utils/timestamp.h"

#include <array>
 #include <stdexcept>
 #include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#else
 #include <arpa/inet.h>
 #include <errno.h>
 #include <fcntl.h>
 #include <netinet/in.h>
 #include <sys/socket.h>
 #include <sys/types.h>
 #include <sys/uio.h>
 #include <unistd.h>
#endif

 namespace market {

 #ifdef _WIN32
 namespace {

 class WSAInitializer {
 public:
     static void ensure() {
         static WSAInitializer instance;
         (void)instance;
     }

 private:

     WSAInitializer() {
         WSADATA data{};
         if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
             throw std::runtime_error("WSAStartup failed");
         }
     }

     ~WSAInitializer() {
         WSACleanup();
     }

     WSAInitializer(const WSAInitializer&) = delete;
     WSAInitializer& operator=(const WSAInitializer&) = delete;
 };

 }
 #endif

 UDPReceiver::UDPReceiver(const std::string& multicast_ip, uint16_t port)
     : multicast_ip_(multicast_ip), port_(port) {

 #ifdef _WIN32
     WSAInitializer::ensure();
 #endif

     socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
     if (socket_fd_ == kInvalidSocket) {
         throw std::runtime_error("Failed to create UDP socket");
     }

#ifdef _WIN32
#ifdef SIO_UDP_CONNRESET

    DWORD bytes_returned = 0;
    BOOL new_behavior = FALSE;
    WSAIoctl(socket_fd_, SIO_UDP_CONNRESET, &new_behavior, sizeof(new_behavior),
             nullptr, 0, &bytes_returned, nullptr, nullptr);
#endif
#endif

     int reuse = 1;
     if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR,
                    reinterpret_cast<char*>(&reuse), sizeof(reuse)) < 0) {
         throw std::runtime_error("Failed to set SO_REUSEADDR");
     }

     int recv_buf = 16 * 1024 * 1024;
     setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF,
                reinterpret_cast<char*>(&recv_buf), sizeof(recv_buf));

     sockaddr_in local_addr{};
     local_addr.sin_family = AF_INET;
     local_addr.sin_port = htons(port_);
     local_addr.sin_addr.s_addr = htonl(INADDR_ANY);

     if (bind(socket_fd_, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr)) < 0) {
         throw std::runtime_error("Failed to bind UDP socket");
     }

     ip_mreq mreq{};

     if (inet_pton(AF_INET, multicast_ip_.c_str(), &mreq.imr_multiaddr) != 1) {
         throw std::runtime_error("Invalid multicast address");
     }

     mreq.imr_interface.s_addr = htonl(INADDR_ANY);

     if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                    reinterpret_cast<char*>(&mreq), sizeof(mreq)) < 0) {
         throw std::runtime_error("Failed to join multicast group");
     }

 #ifdef _WIN32
     u_long non_block = 1;
     ioctlsocket(socket_fd_, FIONBIO, &non_block);
 #else
     const int flags = fcntl(socket_fd_, F_GETFL, 0);
     fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
 #endif
 }

 UDPReceiver::~UDPReceiver() {
     stop();
     if (socket_fd_ != kInvalidSocket) {

 #ifdef _WIN32
         closesocket(socket_fd_);
 #else
         close(socket_fd_);
 #endif
     }
 }

 void UDPReceiver::start(SPSCRingBuffer<RawMessage, 65536>& output_queue) {
     if (running_.load(std::memory_order_relaxed)) {
         return;
     }
     running_.store(true, std::memory_order_release);

     receiver_thread_ = std::thread(&UDPReceiver::run, this, std::ref(output_queue));
 }

 void UDPReceiver::stop() {
     running_.store(false, std::memory_order_release);
     if (receiver_thread_.joinable()) {
         receiver_thread_.join();
     }
 }

 uint64_t UDPReceiver::messages_received() const {
     return messages_received_.load(std::memory_order_acquire);
 }

 uint64_t UDPReceiver::bytes_received() const {
     return bytes_received_.load(std::memory_order_acquire);
 }

 uint64_t UDPReceiver::ring_push_failures() const {
     return push_failures_.load(std::memory_order_acquire);
 }

 void UDPReceiver::run(SPSCRingBuffer<RawMessage, 65536>& output_queue) {

#if defined(__linux__)
     static constexpr size_t BatchSize = 8;

     std::array<RawMessage, BatchSize> batch_buffer{};
     std::array<mmsghdr, BatchSize> msg_vec{};
     std::array<iovec, BatchSize> iovecs{};

     for (size_t idx = 0; idx < BatchSize; ++idx) {
         iovecs[idx].iov_base = batch_buffer[idx].payload.data();
         iovecs[idx].iov_len = RawMessage::MaxPayload;
         msg_vec[idx].msg_hdr.msg_iov = &iovecs[idx];
         msg_vec[idx].msg_hdr.msg_iovlen = 1;
     }

     while (running_.load(std::memory_order_acquire)) {

         const int received = recvmmsg(socket_fd_, msg_vec.data(),
                                       static_cast<unsigned int>(BatchSize), 0, nullptr);
         if (received < 0) {

            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                 std::this_thread::yield();
                 continue;
            }
            break;
         }

         for (int idx = 0; idx < received; ++idx) {
             auto& message_entry = batch_buffer[idx];
             message_entry.len = static_cast<size_t>(msg_vec[idx].msg_len);
             message_entry.recv_timestamp_ns = now_ns();

             if (!output_queue.try_push(message_entry)) {
                 push_failures_.fetch_add(1, std::memory_order_relaxed);
                 continue;
             }

             messages_received_.fetch_add(1, std::memory_order_relaxed);
             bytes_received_.fetch_add(message_entry.len, std::memory_order_relaxed);
         }
     }
#else

     RawMessage message;

     while (running_.load(std::memory_order_acquire)) {

#ifdef _WIN32
         const int len = recvfrom(socket_fd_, message.payload.data(),
                                  static_cast<int>(RawMessage::MaxPayload), 0, nullptr, nullptr);
         if (len == SOCKET_ERROR) {
             const int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK || error == WSAEINTR || error == WSAECONNRESET) {
                 std::this_thread::yield();
                 continue;
            }
            break;
         }
         message.len = static_cast<size_t>(len);
#else
         const ssize_t len = recvfrom(socket_fd_, message.payload.data(),
                                      RawMessage::MaxPayload, 0, nullptr, nullptr);
         if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                 std::this_thread::yield();
                 continue;
            }
            break;
         }
         message.len = static_cast<size_t>(len);
#endif

         message.recv_timestamp_ns = now_ns();

         if (!output_queue.try_push(message)) {
             push_failures_.fetch_add(1, std::memory_order_relaxed);
             continue;
         }

         messages_received_.fetch_add(1, std::memory_order_relaxed);
         bytes_received_.fetch_add(message.len, std::memory_order_relaxed);
     }
#endif
 }

 }