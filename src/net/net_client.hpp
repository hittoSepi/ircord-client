#pragma once

#include "config.hpp"
#include "ircord.pb.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/redirect_error.hpp>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace ircord::net {

// Callbacks for received messages
struct NetCallbacks {
    std::function<void(const Envelope&)> on_message;
    std::function<void()>                on_connected;
    std::function<void(const std::string&)> on_disconnected;
};

// Async TLS TCP client with auto-reconnect and framing.
class NetClient : public std::enable_shared_from_this<NetClient> {
public:
    using TcpSocket = boost::asio::ip::tcp::socket;
    using SslSocket = boost::asio::ssl::stream<TcpSocket>;

    NetClient(boost::asio::io_context& ioc, const ClientConfig& cfg,
              NetCallbacks callbacks);
    ~NetClient();

    // Start connecting (non-blocking, posts coroutine to ioc)
    void start();

    // Stop all activity
    void stop();

    // Send an envelope (thread-safe, can call from any thread)
    void send(const Envelope& env);

    bool is_connected() const { return connected_.load(); }

private:
    boost::asio::awaitable<void> run();
    boost::asio::awaitable<void> connect_once();
    boost::asio::awaitable<void> read_loop(std::shared_ptr<SslSocket> socket);
    boost::asio::awaitable<void> write_loop(std::shared_ptr<SslSocket> socket);

    void schedule_reconnect();

    static std::string cert_fingerprint_sha256(SSL* ssl);
    bool verify_cert_pin(SSL* ssl);

    boost::asio::io_context& ioc_;
    const ClientConfig&      cfg_;
    NetCallbacks             callbacks_;

    boost::asio::ssl::context                           ssl_ctx_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::steady_timer                           reconnect_timer_;

    std::atomic<bool> connected_{false};
    std::atomic<bool> stopping_{false};

    // Outbound queue (accessed on strand_)
    std::queue<std::vector<uint8_t>> send_queue_;
    boost::asio::steady_timer        send_notify_{ioc_};
    std::shared_ptr<SslSocket>       active_socket_;

    // Reconnect backoff (seconds): 1→2→4→...→60
    int reconnect_delay_s_ = 1;

    static constexpr size_t kMaxFrameSize = 65536;
    static constexpr uint32_t kProtocolVersion = 1;

    uint64_t next_seq_ = 1;
};

} // namespace ircord::net
