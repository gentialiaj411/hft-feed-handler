#include "mf/live/bitfinex/ws_client.hpp"

#include <openssl/ssl.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <memory>
#include <string>

namespace mf::live::bitfinex {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

struct WsClient::Impl {
  net::io_context ioc{};
  ssl::context ctx{ssl::context::tlsv12_client};
  std::unique_ptr<websocket::stream<beast::ssl_stream<beast::tcp_stream>>> ws{};
  beast::flat_buffer buffer{};
  std::string host{};
  std::string port{};
  std::string target{};

  explicit Impl(std::string h, std::string p, std::string t)
      : host(std::move(h)), port(std::move(p)), target(std::move(t)) {
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_peer);
  }
};

WsClient::WsClient(std::string host, std::string port, std::string target)
    : impl_(new Impl(std::move(host), std::move(port), std::move(target))) {}

WsClient::~WsClient() {
  delete impl_;
}

bool WsClient::connect() {
  try {
    impl_->ws = std::make_unique<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(impl_->ioc, impl_->ctx);
    if (!SSL_set_tlsext_host_name(impl_->ws->next_layer().native_handle(), impl_->host.c_str())) {
      return false;
    }
    auto const results = net::ip::tcp::resolver(impl_->ioc).resolve(impl_->host, impl_->port);
    beast::get_lowest_layer(*impl_->ws).expires_after(std::chrono::seconds(10));
    beast::get_lowest_layer(*impl_->ws).connect(results);
    impl_->ws->next_layer().handshake(ssl::stream_base::client);
    impl_->ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    impl_->ws->handshake(impl_->host, impl_->target);
    return true;
  } catch (...) {
    return false;
  }
}

void WsClient::close() {
  if (impl_->ws) {
    beast::error_code ec;
    impl_->ws->close(websocket::close_code::normal, ec);
    impl_->ws.reset();
  }
}

bool WsClient::send_text(const std::string& payload) {
  if (!impl_->ws) return false;
  try {
    impl_->ws->write(net::buffer(payload));
    return true;
  } catch (...) {
    return false;
  }
}

bool WsClient::read_message(std::string& out, std::uint32_t timeout_ms) {
  if (!impl_->ws) return false;
  try {
    beast::get_lowest_layer(*impl_->ws).expires_after(std::chrono::milliseconds(timeout_ms));
    impl_->ws->read(impl_->buffer);
    out.assign(static_cast<const char*>(impl_->buffer.data().data()), impl_->buffer.size());
    impl_->buffer.consume(impl_->buffer.size());
    return true;
  } catch (...) {
    return false;
  }
}

bool subscribe_book_r0(WsClient& client, const std::string& symbol, std::int64_t len) {
  if (!client.send_text(R"({"event":"conf","flags":65536})")) return false;
  const std::string sub =
      std::string(R"({"event":"subscribe","channel":"book","symbol":")") + symbol +
      R"(","prec":"R0","len":)" + std::to_string(len) + "}";
  return client.send_text(sub);
}

}  // namespace mf::live::bitfinex
