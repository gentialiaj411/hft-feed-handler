#include <cassert>
#include <cstdint>
#include <vector>

#include "mf/wire/framing.hpp"

namespace {
void be16(std::uint8_t* p, std::uint16_t v) { p[0] = static_cast<std::uint8_t>(v >> 8U); p[1] = static_cast<std::uint8_t>(v); }
void be64(std::uint8_t* p, std::uint64_t v) { for (int i = 7; i >= 0; --i) p[7 - i] = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU); }
}

int main() {
  { // MoldUDP64 + 2 messages
    std::vector<std::uint8_t> d(20);
    for (int i = 0; i < 10; ++i) d[i] = '0' + i;
    be64(d.data() + 10, 100);
    be16(d.data() + 18, 2);
    d.push_back(0); d.push_back(3); d.push_back('A'); d.push_back('1'); d.push_back('2');
    d.push_back(0); d.push_back(2); d.push_back('B'); d.push_back('3');
    mf::wire::DatagramFramer f(mf::wire::WireProtocol::NasdaqItch50);
    std::vector<std::uint64_t> seqs;
    f.frame(d.data(), d.size(), [&](mf::wire::FrameSlice s) { seqs.push_back(s.sequence); });
    assert(seqs.size() == 2 && seqs[0] == 100 && seqs[1] == 101);
  }
  { // IEX-TP + 1 message
    std::vector<std::uint8_t> d(36 + 2 + 4);
    d[0] = 1; d[1] = 1; be16(d.data() + 8, 6); be16(d.data() + 10, 1); be64(d.data() + 20, 77);
    be16(d.data() + 36, 4); d[38] = 'a'; d[39] = 'b'; d[40] = 'c'; d[41] = 'd';
    mf::wire::DatagramFramer f(mf::wire::WireProtocol::IexDeep);
    std::uint64_t seq = 0; std::size_t n = 0;
    f.frame(d.data(), d.size(), [&](mf::wire::FrameSlice s) { seq = s.sequence; ++n; });
    assert(n == 1 && seq == 77);
  }
  { // PITCH newline
    const char* lines = "00000001A...\n00000002X...\n";
    mf::wire::DatagramFramer f(mf::wire::WireProtocol::CboePitch);
    std::vector<std::uint64_t> seqs;
    f.frame(reinterpret_cast<const std::uint8_t*>(lines), 26, [&](mf::wire::FrameSlice s) { seqs.push_back(s.sequence); });
    assert(seqs.size() == 2 && seqs[0] == 1 && seqs[1] == 2);
  }
  { // malformed mold payload length -> no frames
    std::vector<std::uint8_t> d(22);
    be64(d.data() + 10, 1); be16(d.data() + 18, 1); d[20] = 0; d[21] = 5;
    mf::wire::DatagramFramer f(mf::wire::WireProtocol::NasdaqItch50);
    std::size_t n = 0;
    f.frame(d.data(), d.size(), [&](mf::wire::FrameSlice) { ++n; });
    assert(n == 0);
  }
  return 0;
}
