
#include "message_parser.h"
#include "order_book.h"
#include "ring_buffer.h"
#include "udp_receiver.h"
#include "utils/stats.h"
#include "utils/timestamp.h"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

static std::atomic<bool>* g_running_flag = nullptr;

void signal_handler(int) {
    if (g_running_flag) {
        g_running_flag->store(false, std::memory_order_release);
    }
}

struct Config {
    std::string multicast_ip{"239.255.0.1"};
    uint16_t port{5000};
    uint64_t duration_seconds{0};
    std::vector<uint32_t> watch_symbols;
};

Config parse_args(int argc, char** argv) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--multicast" && i + 1 < argc) {
            cfg.multicast_ip = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            cfg.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--duration" && i + 1 < argc) {
            cfg.duration_seconds = static_cast<uint64_t>(std::stoull(argv[++i]));
        } else if (arg == "--symbols" && i + 1 < argc) {

            const std::string list = argv[++i];
            std::istringstream iss(list);
            std::string token;
            while (std::getline(iss, token, ',')) {
                if (!token.empty()) {
                    cfg.watch_symbols.push_back(static_cast<uint32_t>(std::stoul(token)));
                }
            }
        }

    }

    return cfg;
}

std::string format_price(int64_t price) {
    if (price == 0) {
        return "n/a";
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4) << static_cast<double>(price) / 10000.0;
    return oss.str();
}

}

int main(int argc, char** argv) {

    const Config cfg = parse_args(argc, argv);

    std::cout << "=== Market Data Handler ===\n";
    std::cout << "Joining multicast " << cfg.multicast_ip << ":" << cfg.port << "\n\n";

    market::SPSCRingBuffer<market::RawMessage, 65536> ring;

    market::UDPReceiver receiver(cfg.multicast_ip, cfg.port);
    receiver.start(ring);

    std::unordered_set<uint32_t> watched(cfg.watch_symbols.begin(), cfg.watch_symbols.end());

    std::atomic<bool> running{true};
    g_running_flag = &running;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::thread processor([&]() {

        market::MessageParser parser;
        market::OrderBook order_book;
        market::LatencyStats latency_stats;

        uint64_t interval_start = market::now_ns();
        uint64_t interval_messages = 0;
        uint64_t interval_bytes = 0;
        uint32_t last_watched_symbol = 0;

        while (running.load(std::memory_order_acquire) || ring.size() > 0) {

            market::RawMessage raw;
            if (!ring.try_pop(raw)) {
                std::this_thread::yield();
                continue;
            }

            const market::MessageHeader* header = parser.parse(raw);
            if (!header) {
                continue;
            }

            const uint64_t latency = market::now_ns() - raw.recv_timestamp_ns;
            latency_stats.record(latency);

            interval_messages += 1;
            interval_bytes += raw.len;

            switch (header->msg_type) {
                case market::MSG_QUOTE: {

                    const auto* quote = parser.as<market::Quote>(header);
                    order_book.on_quote(*quote);

                    if (watched.count(quote->symbol_id)) {
                        last_watched_symbol = quote->symbol_id;
                    }
                    break;
                }

                case market::MSG_ORDER_ADD: {

                    order_book.on_order_add(*parser.as<market::OrderAdd>(header));
                    break;
                }

                case market::MSG_ORDER_CANCEL: {

                    order_book.on_order_cancel(*parser.as<market::OrderCancel>(header));
                    break;
                }

                case market::MSG_TRADE: {

                    break;
                }

                default: {

                    break;
                }
            }

            const uint64_t now = market::now_ns();
            if (now - interval_start >= 1'000'000'000ULL) {
                const double elapsed_s = static_cast<double>(now - interval_start) / 1e9;
                const auto snap = latency_stats.snapshot();

                std::cout << "[BBO] Bid: $" << format_price(order_book.best_bid())
                          << " x $" << format_price(order_book.best_ask())
                          << " (spread: $" << format_price(order_book.spread()) << ")\n";

                if (last_watched_symbol != 0) {
                    std::cout << "  Watching symbol " << last_watched_symbol << " updates\n";
                }

                std::cout << "Stats (last " << elapsed_s << "s):\n";
                std::cout << "  Messages received:  " << interval_messages << "\n";
                std::cout << "  Throughput:         " << (interval_messages / elapsed_s) << " msg/sec\n";
                std::cout << "  Avg latency:        " << snap.avg_ns << "ns\n";
                std::cout << "  P50 latency:        " << snap.p50_ns << "ns\n";
                std::cout << "  P95 latency:        " << snap.p95_ns << "ns\n";
                std::cout << "  P99 latency:        " << snap.p99_ns << "ns\n";
                std::cout << "  P99.9 latency:      " << snap.p999_ns << "ns\n";
                std::cout << "  Sequence gaps:      " << parser.sequence_gaps() << "\n";
                std::cout << "  Parse errors:       " << parser.invalid_messages() << "\n";

                const auto histogram = snap.histogram;
                const std::array<std::string, 5> labels = {
                    "<500ns", "500ns-1us", "1us-2us", "2us-5us", ">5us"};

                std::cout << "Latency Distribution:\n";
                for (size_t idx = 0; idx < histogram.size(); ++idx) {

                    const double percent =
                        interval_messages == 0 ? 0.0 : (static_cast<double>(histogram[idx]) / interval_messages) * 100.0;
                    std::cout << "  " << labels[idx] << ": " << std::fixed << std::setprecision(1) << percent
                              << "% (" << histogram[idx] << ")\n";
                }

                interval_messages = 0;
                interval_bytes = 0;
                interval_start = now;
                last_watched_symbol = 0;

                parser = market::MessageParser();
                latency_stats.reset();
            }
        }
    });

    const auto start_time = std::chrono::steady_clock::now();

    while (running.load(std::memory_order_acquire)) {

        if (cfg.duration_seconds > 0) {
            const auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed >= std::chrono::seconds(cfg.duration_seconds)) {
                running.store(false, std::memory_order_release);
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (processor.joinable()) {
        processor.join();
    }

    receiver.stop();

    std::cout << "\nFinal stats:\n";
    std::cout << "  Received:  " << receiver.messages_received() << " messages ("
              << receiver.bytes_received() << " bytes)\n";
    std::cout << "  Ring push failures: " << receiver.ring_push_failures() << "\n";

    return 0;
}
