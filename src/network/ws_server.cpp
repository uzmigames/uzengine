// ---------------------------------------------------------------------------
// ws_server.cpp -- WsServer implementation using IXWebSocket.
// ---------------------------------------------------------------------------

#include "ws_server.h"

#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXWebSocket.h>

#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace uz {

// ---------------------------------------------------------------------------
// Pimpl struct -- holds IXWebSocket state.
// ---------------------------------------------------------------------------

struct WsServer::Impl {
    std::unique_ptr<ix::WebSocketServer> server;

    // Connected client sockets (shared_ptr from IXWebSocket callback).
    mutable std::mutex clientsMutex;
    std::set<std::shared_ptr<ix::WebSocket>> clients;

    // fMP4 init segment sent to every new client.
    std::mutex initMutex;
    std::vector<uint8_t> initSegment;

    LogCallback logCb;

    void log(const std::string& msg) {
        if (logCb) {
            logCb(msg);
        }
    }
};

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

WsServer::WsServer() : impl_(std::make_unique<Impl>()) {}

WsServer::~WsServer() {
    stop();
}

// ---------------------------------------------------------------------------
// setInitSegment
// ---------------------------------------------------------------------------

void WsServer::setInitSegment(std::vector<uint8_t> initSegment) {
    std::lock_guard<std::mutex> lock(impl_->initMutex);
    impl_->initSegment = std::move(initSegment);
}

// ---------------------------------------------------------------------------
// setLogCallback
// ---------------------------------------------------------------------------

void WsServer::setLogCallback(LogCallback cb) {
    impl_->logCb = std::move(cb);
}

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------

bool WsServer::start(uint16_t port) {
    impl_->server = std::make_unique<ix::WebSocketServer>(
        static_cast<int>(port),
        "0.0.0.0");

    // Configure the connection handler.
    impl_->server->setOnClientMessageCallback(
        [this](std::shared_ptr<ix::ConnectionState> connectionState,
               ix::WebSocket& ws,
               const ix::WebSocketMessagePtr& msg) {

            auto wsPtr = connectionState->getId(); // use ID as key handle
            (void)wsPtr;

            if (msg->type == ix::WebSocketMessageType::Open) {
                // Register client.
                std::shared_ptr<ix::WebSocket> sharedWs;
                {
                    // IXWebSocket doesn't give us a shared_ptr to the socket
                    // directly in this callback, so we wrap the raw reference.
                    // Since the callback keeps ws alive for the duration of
                    // the connection, we use a shared_ptr with a no-op deleter.
                    sharedWs = std::shared_ptr<ix::WebSocket>(&ws, [](ix::WebSocket*){});
                }

                {
                    std::lock_guard<std::mutex> lock(impl_->clientsMutex);
                    impl_->clients.insert(sharedWs);
                }

                impl_->log("Client connected. Total: " +
                           std::to_string(clientCount()));

                // Send the init segment immediately.
                std::vector<uint8_t> init;
                {
                    std::lock_guard<std::mutex> initLock(impl_->initMutex);
                    init = impl_->initSegment;
                }

                if (!init.empty()) {
                    ws.sendBinary(ix::IXWebSocketSendData(
                        reinterpret_cast<const char*>(init.data()),
                        init.size()));
                }

            } else if (msg->type == ix::WebSocketMessageType::Close) {
                // Remove client on disconnect.
                std::shared_ptr<ix::WebSocket> sharedWs(
                    &ws, [](ix::WebSocket*){});

                {
                    std::lock_guard<std::mutex> lock(impl_->clientsMutex);
                    impl_->clients.erase(sharedWs);
                }

                impl_->log("Client disconnected. Total: " +
                           std::to_string(clientCount()));

            } else if (msg->type == ix::WebSocketMessageType::Error) {
                impl_->log("WebSocket error: " + msg->errorInfo.reason);
            }
        });

    auto res = impl_->server->listen();
    if (!res.first) {
        impl_->log("Failed to listen on port " + std::to_string(port) +
                   ": " + res.second);
        return false;
    }

    impl_->server->start();
    impl_->log("WebSocket server listening on port " + std::to_string(port));
    return true;
}

// ---------------------------------------------------------------------------
// broadcast
// ---------------------------------------------------------------------------

void WsServer::broadcast(const std::vector<uint8_t>& data) {
    if (data.empty()) return;

    ix::IXWebSocketSendData sendData(
        reinterpret_cast<const char*>(data.data()),
        data.size());

    std::lock_guard<std::mutex> lock(impl_->clientsMutex);
    for (auto& client : impl_->clients) {
        client->sendBinary(sendData);
    }
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------

void WsServer::stop() {
    if (impl_->server) {
        impl_->server->stop();
        impl_->server.reset();
    }

    std::lock_guard<std::mutex> lock(impl_->clientsMutex);
    impl_->clients.clear();
}

// ---------------------------------------------------------------------------
// clientCount
// ---------------------------------------------------------------------------

int WsServer::clientCount() const {
    std::lock_guard<std::mutex> lock(impl_->clientsMutex);
    return static_cast<int>(impl_->clients.size());
}

} // namespace uz
