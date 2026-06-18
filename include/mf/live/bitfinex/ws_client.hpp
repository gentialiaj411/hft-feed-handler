#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace mf::live::bitfinex {

class WsClient {
 public:
  using MessageFn = std::function<void(const std::string&)>;

  WsClient(std::string host, std::string port, std::string target);
  ~WsClient();

  bool connect();
  void close();
  bool send_text(const std::string& payload);
  bool read_message(std::string& out, std::uint32_t timeout_ms = 5000);

 private:
  struct Impl;
  Impl* impl_{nullptr};
};

[[nodiscard]] bool subscribe_book_r0(WsClient& client, const std::string& symbol, std::int64_t len = 25);

}  // namespace mf::live::bitfinex
