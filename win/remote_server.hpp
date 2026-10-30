#pragma once

// The local remote control: a small HTTP server on 127.0.0.1, for a phone app on the same PC, a Stream Deck or a
// script. It refuses any address other than a loopback one before opening a socket, checks the bound address again
// after bind, and checks each peer. Requests are answered by core/remote_protocol.* (token, routes). One thread,
// one request at a time: the clients are few and the answers small.

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "../core/remote_protocol.hpp"

namespace amk::win {

class RemoteServer {
public:
    struct Config {
        std::string address = "127.0.0.1";  // must be a loopback address; anything else is refused
        uint16_t port = remote::kDefaultPort;  // 0 picks a free port (tests)
        std::string token;                     // see remote::IsValidToken
    };

    RemoteServer() = default;
    ~RemoteServer();

    RemoteServer(const RemoteServer&) = delete;
    RemoteServer& operator=(const RemoteServer&) = delete;

    // Binds and starts the thread. `backend` must outlive the server. False with `error` in French.
    bool Start(const Config& config, remote::Backend& backend, std::string* error);
    void Stop();

    bool running() const { return m_thread.joinable(); }
    // The port actually bound.
    uint16_t port() const { return m_port; }

private:
    void Run();

    uintptr_t m_socket = ~uintptr_t{0};  // a SOCKET, kept opaque so that this header needs no winsock2.h
    uint16_t m_port = 0;
    remote::Backend* m_backend = nullptr;
    std::string m_token;
    std::atomic<bool> m_stop{false};
    bool m_winsock = false;
    std::thread m_thread;
};

}  // namespace amk::win
