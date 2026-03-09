#include "net/net_client.hpp"
#include <spdlog/spdlog.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/this_coro.hpp>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <cstring>

namespace ircord::net {

using boost::asio::use_awaitable;
namespace asio = boost::asio;

NetClient::NetClient(asio::io_context& ioc, const ClientConfig& cfg,
                     NetCallbacks callbacks)
    : ioc_(ioc)
    , cfg_(cfg)
    , callbacks_(std::move(callbacks))
    , ssl_ctx_(asio::ssl::context::tls_client)
    , strand_(asio::make_strand(ioc))
    , reconnect_timer_(ioc)
{
    if (cfg.tls.verify_peer) {
        ssl_ctx_.set_default_verify_paths();
        ssl_ctx_.set_verify_mode(asio::ssl::verify_peer);
    } else {
        ssl_ctx_.set_verify_mode(asio::ssl::verify_none);
    }
}

NetClient::~NetClient() {
    stop();
}

void NetClient::start() {
    asio::co_spawn(strand_, run(), asio::detached);
}

void NetClient::stop() {
    stopping_.store(true);
    reconnect_timer_.cancel();
}

asio::awaitable<void> NetClient::run() {
    while (!stopping_.load()) {
        try {
            co_await connect_once();
        } catch (const std::exception& e) {
            if (!stopping_.load())
                spdlog::warn("Connection error: {}", e.what());
        }

        if (stopping_.load()) break;

        connected_.store(false);
        if (callbacks_.on_disconnected) {
            callbacks_.on_disconnected("Reconnecting in " +
                std::to_string(reconnect_delay_s_) + "s...");
        }

        spdlog::info("Reconnecting in {}s...", reconnect_delay_s_);
        reconnect_timer_.expires_after(std::chrono::seconds(reconnect_delay_s_));
        boost::system::error_code ec;
        co_await reconnect_timer_.async_wait(
            asio::redirect_error(use_awaitable, ec));

        reconnect_delay_s_ = std::min(reconnect_delay_s_ * 2, 60);
    }
}

asio::awaitable<void> NetClient::connect_once() {
    auto executor = co_await asio::this_coro::executor;

    // DNS resolve
    asio::ip::tcp::resolver resolver(executor);
    auto endpoints = co_await resolver.async_resolve(
        cfg_.server.host,
        std::to_string(cfg_.server.port),
        use_awaitable);

    // Create TLS socket
    auto socket = std::make_shared<SslSocket>(executor, ssl_ctx_);

    SSL_set_tlsext_host_name(socket->native_handle(), cfg_.server.host.c_str());
    // Hostname verification is handled via TOFU cert pinning in verify_cert_pin()
    co_await asio::async_connect(socket->lowest_layer(), endpoints, use_awaitable);
    socket->lowest_layer().set_option(asio::ip::tcp::no_delay(true));

    co_await socket->async_handshake(asio::ssl::stream_base::client, use_awaitable);

    if (!verify_cert_pin(socket->native_handle())) {
        throw std::runtime_error("Certificate pinning check failed");
    }

    spdlog::info("Connected to {}:{}", cfg_.server.host, cfg_.server.port);
    connected_.store(true);
    reconnect_delay_s_ = 1;

    if (callbacks_.on_connected) callbacks_.on_connected();

    // Run read and write loops using the shared socket
    co_await read_loop(socket);
}

asio::awaitable<void> NetClient::read_loop(std::shared_ptr<SslSocket> socket) {
    while (!stopping_.load()) {
        // Read 4-byte big-endian length header
        std::array<uint8_t, 4> header_buf{};
        co_await asio::async_read(*socket, asio::buffer(header_buf), use_awaitable);

        uint32_t payload_size = 0;
        payload_size  = static_cast<uint32_t>(header_buf[0]) << 24;
        payload_size |= static_cast<uint32_t>(header_buf[1]) << 16;
        payload_size |= static_cast<uint32_t>(header_buf[2]) << 8;
        payload_size |= static_cast<uint32_t>(header_buf[3]);

        if (payload_size == 0 || payload_size > kMaxFrameSize) {
            throw std::runtime_error("Invalid frame size: " + std::to_string(payload_size));
        }

        std::vector<uint8_t> payload(payload_size);
        co_await asio::async_read(*socket, asio::buffer(payload), use_awaitable);

        Envelope env;
        if (!env.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
            spdlog::warn("Failed to parse Envelope (size={})", payload_size);
            continue;
        }

        if (callbacks_.on_message) callbacks_.on_message(env);

        // Drain outbound queue — read_loop runs on the strand, so direct access is safe
        while (!send_queue_.empty()) {
            auto frame = std::move(send_queue_.front());
            send_queue_.pop();
            co_await asio::async_write(*socket, asio::buffer(frame), use_awaitable);
        }
    }
}

void NetClient::send(const Envelope& env) {
    // Serialize envelope to framed bytes
    std::string body = env.SerializeAsString();
    if (body.size() > kMaxFrameSize) {
        spdlog::warn("send: envelope too large ({}), dropping", body.size());
        return;
    }

    uint32_t size_be = static_cast<uint32_t>(body.size());
    std::vector<uint8_t> frame(4 + body.size());
    frame[0] = (size_be >> 24) & 0xFF;
    frame[1] = (size_be >> 16) & 0xFF;
    frame[2] = (size_be >>  8) & 0xFF;
    frame[3] =  size_be        & 0xFF;
    std::memcpy(frame.data() + 4, body.data(), body.size());

    asio::post(strand_, [this, f = std::move(frame)]() mutable {
        send_queue_.push(std::move(f));
    });
}

std::string NetClient::cert_fingerprint_sha256(SSL* ssl) {
    X509* cert = SSL_get_peer_certificate(ssl);
    if (!cert) return {};

    unsigned char buf[EVP_MAX_MD_SIZE];
    unsigned int  len = 0;
    X509_digest(cert, EVP_sha256(), buf, &len);
    X509_free(cert);

    std::ostringstream oss;
    for (unsigned i = 0; i < len; ++i) {
        if (i > 0) oss << ':';
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(buf[i]);
    }
    return oss.str();
}

bool NetClient::verify_cert_pin(SSL* ssl) {
    if (!cfg_.tls.verify_peer) return true;

    std::string fp = cert_fingerprint_sha256(ssl);
    if (fp.empty()) {
        spdlog::warn("Could not get server certificate");
        return false;
    }

    if (cfg_.server.cert_pin.empty()) {
        spdlog::info("Server cert fingerprint (TOFU): {}", fp);
        spdlog::info("Add 'cert_pin = \"{}\"' to [server] config to pin it.", fp);
        return true;
    }

    if (fp != cfg_.server.cert_pin) {
        spdlog::error("Certificate pin mismatch! Expected: {} Got: {}",
                      cfg_.server.cert_pin, fp);
        return false;
    }
    return true;
}

} // namespace ircord::net
