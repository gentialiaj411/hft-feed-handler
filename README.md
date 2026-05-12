# High-Performance Market Data Handler

A production-quality market data feed handler designed for high-frequency trading systems. Processes UDP multicast market data with lock-free architecture, zero-copy parsing, and comprehensive performance monitoring. Demonstrates enterprise-grade low-latency engineering with real-world production considerations.

## Architecture Overview

```
[UDP Multicast Socket] --(batched recv)--> [Lock-Free SPSC Ring Buffer] --(zero-copy)--> [Message Parser] --> [Order Book Engine]
                                                         ↓
                                                [Real-Time Statistics] --> [Latency Histograms]
```

## Technical Implementation

### Lock-Free Design
- **Single-Producer Single-Consumer (SPSC) Ring Buffer**: Cache-aligned circular buffer using atomic operations with release-acquire memory ordering
- **Wait-Free Operations**: No mutexes, locks, or system calls in the hot path
- **Power-of-Two Sizing**: Optimized for efficient modulo operations and cache alignment
- **False Sharing Prevention**: 64-byte alignment for all performance-critical structures

### Zero-Copy Message Processing
- **Direct Buffer Access**: Messages parsed directly from network receive buffers
- **Type-Safe Casting**: Compile-time validation with runtime length checks
- **Efficient Deserialization**: No heap allocations in message processing pipeline
- **SIMD-Ready Layout**: Data structures optimized for potential vectorization

### Network Layer
- **UDP Multicast**: Efficient one-to-many distribution with proper IGMP group management
- **Non-Blocking I/O**: Event-driven network processing with configurable buffer sizes
- **Connection Resilience**: Automatic recovery from network interruptions
- **Platform Abstraction**: Cross-platform socket handling (Windows/Linux/macOS)

### Order Book Engine
- **Real-Time Updates**: Bid/ask price tracking with automatic spread calculation
- **Symbol Filtering**: Configurable symbol watching for focused analysis
- **Thread-Safe Operations**: Lock-free updates with atomic price tracking
- **Memory Efficient**: Compact representation with minimal overhead

### Performance Monitoring
- **Latency Statistics**: P50/P95/P99/P99.9 percentile tracking with histogram generation
- **Throughput Metrics**: Real-time message rate calculation with efficiency reporting
- **Sequence Validation**: Gap detection and recovery for data integrity
- **Resource Monitoring**: CPU, memory, and network utilization tracking

## Build System

### Linux/macOS
```bash
cd market-data-handler
make all
```

### Windows
```powershell
cd market-data-handler
.\build_windows.cmd
```

## Usage Examples

### Basic Operation
```bash
# Start market data feed simulator
./feed_simulator --multicast 239.255.0.1 --port 5000 --rate 1000000 --symbols 200 --duration 30

# Start market data handler (in another terminal)
./market_handler --multicast 239.255.0.1 --port 5000 --symbols 1000,1001 --duration 30
```

### Advanced Configuration
```bash
# High-throughput testing
./feed_simulator --rate 2000000 --symbols 500 --duration 60

# Focused symbol monitoring
./market_handler --symbols 1000,1001,1002,1005 --duration 300

# Benchmarking with custom multicast group
./market_handler --multicast 239.255.1.100 --port 6000 --duration 30
```

## Performance Evidence

This repository does not ship fixed benchmark claims in source because results depend on hardware, OS, dataset, and runtime configuration.

Use the built-in evidence workflows:

- Linux/WSL2 benchmark:
  - `bash scripts/bench_phase2_pipeline_wsl.sh 2000000 256 1048576`
- Linux/WSL2 tests + benchmark artifact bundle:
  - `bash scripts/run_phase2_evidence_wsl.sh <nasdaq_raw> <iex_pcap> [cboe_file] [out_dir]`
- Summarize an artifact:
  - `python3 scripts/summarize_phase2_artifact.py artifacts/perf/<artifact_file>.txt`
