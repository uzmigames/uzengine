// ---------------------------------------------------------------------------
// ws_server.h -- WebSocket streaming server.
//
// Listens on a configurable port (default 9002).  On connect, sends the
// fMP4 init segment.  Then broadcasts media segments to all connected clients.
// ---------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace uz {

/// Callback type for log messages from the server.
using LogCallback = std::function<void(const std::string&)>;

/// Callback for incoming text (non-binary) WebSocket messages from clients.
using TextMessageCallback = std::function<void(const std::string&)>;

/// WebSocket server that streams fMP4 fragments to connected clients.
///
/// Threading model:
///   - IXWebSocket handles connection accept/read on its own threads.
///   - broadcast() is called from the encode thread; it acquires the client
///     mutex and sends to all connected sockets.
///   - start()/stop() are called from the main thread.
class WsServer {
public:
    WsServer();
    ~WsServer();

    // Non-copyable, non-movable.
    WsServer(const WsServer&) = delete;
    WsServer& operator=(const WsServer&) = delete;
    WsServer(WsServer&&) = delete;
    WsServer& operator=(WsServer&&) = delete;

    /// Set the fMP4 init segment delivered to every new client on connect.
    /// Must be called before start().
    void setInitSegment(std::vector<uint8_t> initSegment);

    /// Set optional log callback (called from IXWebSocket threads).
    void setLogCallback(LogCallback cb);

    /// Set callback for incoming text (non-binary) WebSocket messages.
    /// Called from IXWebSocket threads -- the callback must be thread-safe.
    void setTextMessageCallback(TextMessageCallback cb);

    /// Start listening on the given port.
    /// Returns true if the server bound successfully.
    [[nodiscard]] bool start(uint16_t port = 9002);

    /// Broadcast a media segment to all currently connected clients.
    /// Thread-safe. No-op if no clients are connected.
    void broadcast(const std::vector<uint8_t>& data);

    /// Stop accepting connections and close all existing ones.
    void stop();

    /// Returns the number of currently connected clients.
    [[nodiscard]] int clientCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace uz
