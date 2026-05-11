
#include "../src/market_data.h"
#include "../src/utils/timestamp.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef _WIN32
using socket_handle_t = SOCKET;
#else
using socket_handle_t = int;
#endif

namespace {

#ifdef _WIN32
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
#endif

struct FeedConfig {
    std::string multicast{"239.255.0.1"};
    uint16_t port{5000};
    uint32_t rate{1'000'000};
    uint32_t symbol_count{100};
    uint64_t duration_seconds{10};
};

FeedConfig parse_args(int argc, char** argv) {
    FeedConfig cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--multicast" && i + 1 < argc) {
            cfg.multicast = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            cfg.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--rate" && i + 1 < argc) {
            cfg.rate = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--symbols" && i + 1 < argc) {
            cfg.symbol_count = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--duration" && i + 1 < argc) {
            cfg.duration_seconds = static_cast<uint64_t>(std::stoull(argv[++i]));
        }
    }
    return cfg;
}

socket_handle_t create_socket() {
#ifdef _WIN32
    WSAInitializer::ensure();
#endif
    socket_handle_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1
#ifdef _WIN32
        || sock == INVALID_SOCKET
#endif
    ) {
        throw std::runtime_error("Failed to create UDP socket");
    }
    int ttl = 1;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<char*>(&ttl), sizeof(ttl));
    return sock;
}

void send_message(socket_handle_t fd, const sockaddr_in& endpoint, const char* buffer, size_t length) {
    sendto(fd, buffer, static_cast<int>(length), 0,
           reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint));
}

}

int main(int argc, char** argv) {

    const auto cfg = parse_args(argc, argv);
    std::cout << "Feed simulator -> " << cfg.multicast << ":" << cfg.port << " @ " << cfg.rate << " msg/sec\n";

    socket_handle_t sock = create_socket();

    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(cfg.port);
    inet_pton(AF_INET, cfg.multicast.c_str(), &endpoint.sin_addr);

    std::mt19937_64 rng(42);

    std::uniform_int_distribution<int64_t> price_delta(-500, 500);
    std::uniform_int_distribution<uint32_t> size_dist(100, 500);
    std::uniform_int_distribution<int> type_dist(1, 4);
    std::uniform_int_distribution<int> side_dist(0, 1);

    std::vector<uint32_t> symbols(cfg.symbol_count);
    for (uint32_t i = 0; i < cfg.symbol_count; ++i) {
        symbols[i] = 1000 + i;
    }

    uint32_t sequence = 1;
    uint64_t order_id = 1;

    auto next_send = std::chrono::steady_clock::now();
    const auto interval = std::chrono::nanoseconds(1'000'000'000LL / cfg.rate);
    const auto stop_time = std::chrono::steady_clock::now() + std::chrono::seconds(cfg.duration_seconds);

    while (std::chrono::steady_clock::now() < stop_time) {

        const uint32_t symbol = symbols[sequence % symbols.size()];

        const int msg_type = type_dist(rng);

        switch (msg_type) {
            case market::MSG_QUOTE: {

                market::Quote quote{};

                quote.header.msg_type = market::MSG_QUOTE;
                quote.header.msg_len = sizeof(quote);
                quote.header.sequence_num = sequence++;
                quote.header.timestamp_ns = market::now_ns();

                quote.symbol_id = symbol;
                quote.bid_price = 1'500'000 + price_delta(rng);
                quote.ask_price = quote.bid_price + 25;
                quote.bid_size = size_dist(rng);
                quote.ask_size = size_dist(rng);

                send_message(sock, endpoint, reinterpret_cast<const char*>(&quote), sizeof(quote));
                break;
            }

            case market::MSG_ORDER_ADD: {

                market::OrderAdd add{};

                add.header.msg_type = market::MSG_ORDER_ADD;
                add.header.msg_len = sizeof(add);
                add.header.sequence_num = sequence++;
                add.header.timestamp_ns = market::now_ns();

                add.order_id = order_id++;
                add.symbol_id = symbol;
                add.price = 1'500'000 + price_delta(rng);
                add.size = size_dist(rng);
                add.side = side_dist(rng) ? 'B' : 'S';

                send_message(sock, endpoint, reinterpret_cast<const char*>(&add), sizeof(add));
                break;
            }

            case market::MSG_ORDER_CANCEL: {

                market::OrderCancel cancel{};

                cancel.header.msg_type = market::MSG_ORDER_CANCEL;
                cancel.header.msg_len = sizeof(cancel);
                cancel.header.sequence_num = sequence++;
                cancel.header.timestamp_ns = market::now_ns();

                cancel.order_id = order_id > 0 ? order_id - 1 : 1;
                cancel.symbol_id = symbol;

                send_message(sock, endpoint, reinterpret_cast<const char*>(&cancel), sizeof(cancel));
                break;
            }

            case market::MSG_TRADE: {

                market::Trade trade{};

                trade.header.msg_type = market::MSG_TRADE;
                trade.header.msg_len = sizeof(trade);
                trade.header.sequence_num = sequence++;
                trade.header.timestamp_ns = market::now_ns();

                trade.symbol_id = symbol;
                trade.price = 1'500'000 + price_delta(rng);
                trade.size = size_dist(rng);
                trade.side = side_dist(rng) ? 'B' : 'S';

                send_message(sock, endpoint, reinterpret_cast<const char*>(&trade), sizeof(trade));
                break;
            }

            default: {

                break;
            }
        }

        std::this_thread::sleep_until(next_send);
        next_send += interval;
    }

    if (sock != -1
#ifdef _WIN32
        && sock != INVALID_SOCKET
#endif
    ) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }

    std::cout << "Feed simulator finished after " << cfg.duration_seconds << "s\n";
    return 0;
}
