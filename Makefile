CXX := g++
CXXFLAGS := -std=c++17 -O3 -march=native -Wall -Wextra -I./src

ifeq ($(OS),Windows_NT)
LIBS := -lws2_32
else
CXXFLAGS += -pthread
LIBS :=
endif

SRCS := src/main.cpp src/udp_receiver.cpp src/message_parser.cpp src/order_book.cpp

.PHONY: all clean

all: market_handler feed_simulator latency_benchmark test_ring_buffer test_parser test_order_book

market_handler: $(SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS)

feed_simulator: tools/feed_simulator.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LIBS)

latency_benchmark: benchmarks/latency_benchmark.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LIBS)

test_ring_buffer: tests/test_ring_buffer.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LIBS)

test_parser: tests/test_parser.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LIBS)

test_order_book: tests/test_order_book.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LIBS)

clean:
	rm -f market_handler feed_simulator latency_benchmark test_ring_buffer test_parser test_order_book

