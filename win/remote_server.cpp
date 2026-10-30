// winsock2.h comes before windows.h (the targets define WIN32_LEAN_AND_MEAN, so windows.h does not pull winsock.h).
#include <winsock2.h>
#include <ws2tcpip.h>

#include "remote_server.hpp"

#include <chrono>

namespace amk::win {

namespace {

constexpr DWORD kReceiveTimeoutMs = 2000;

SOCKET AsSocket(uintptr_t value) { return static_cast<SOCKET>(value); }

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

}  // namespace

RemoteServer::~RemoteServer() { Stop(); }

bool RemoteServer::Start(const Config& config, remote::Backend& backend, std::string* error) {
    if (running()) return true;
    // The address is checked before any socket exists: this server never listens beyond the PC.
    if (!remote::IsLoopbackText(config.address)) {
        return Fail(error, "adresse refusée : " + config.address + " (la télécommande n'écoute que 127.0.0.1)");
    }
    if (!remote::IsValidToken(config.token)) return Fail(error, "jeton absent ou trop court (remote.token)");

    in_addr address{};
    if (inet_pton(AF_INET, config.address.c_str(), &address) != 1) {
        return Fail(error, "adresse refusée : " + config.address + " (IPv4 attendue, ex. 127.0.0.1)");
    }

    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return Fail(error, "Winsock indisponible");
    m_winsock = true;

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        Stop();
        return Fail(error, "socket impossible à créer");
    }
    // No other program may bind the same port while the kit holds it.
    BOOL exclusive = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof exclusive);

    sockaddr_in bound{};
    bound.sin_family = AF_INET;
    bound.sin_addr = address;
    bound.sin_port = htons(config.port);
    if (bind(listener, reinterpret_cast<sockaddr*>(&bound), sizeof bound) != 0) {
        int code = WSAGetLastError();
        closesocket(listener);
        Stop();
        return Fail(error, "port " + std::to_string(config.port) + " indisponible (code " + std::to_string(code) + ")");
    }
    // Checked again on the socket itself, in case the address text and the bound address ever disagree.
    sockaddr_in actual{};
    int size = sizeof actual;
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&actual), &size) != 0 ||
        !remote::IsLoopbackIpv4(ntohl(actual.sin_addr.s_addr))) {
        closesocket(listener);
        Stop();
        return Fail(error, "le socket n'est pas lié à 127.0.0.1 : arrêt");
    }
    if (listen(listener, 8) != 0) {
        closesocket(listener);
        Stop();
        return Fail(error, "écoute impossible");
    }

    m_socket = static_cast<uintptr_t>(listener);
    m_port = ntohs(actual.sin_port);
    m_backend = &backend;
    m_token = config.token;
    m_stop = false;
    m_thread = std::thread([this] { Run(); });
    return true;
}

void RemoteServer::Stop() {
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
    if (m_socket != ~uintptr_t{0}) closesocket(AsSocket(m_socket));
    m_socket = ~uintptr_t{0};
    if (m_winsock) WSACleanup();
    m_winsock = false;
}

void RemoteServer::Run() {
    remote::Router router(m_token);
    SOCKET listener = AsSocket(m_socket);
    while (!m_stop) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        timeval wait{0, 200 * 1000};  // checks m_stop five times a second
        if (select(0, &readable, nullptr, nullptr, &wait) <= 0) continue;

        sockaddr_in peer{};
        int peerSize = sizeof peer;
        SOCKET client = accept(listener, reinterpret_cast<sockaddr*>(&peer), &peerSize);
        if (client == INVALID_SOCKET) continue;
        bool loopback = peer.sin_family == AF_INET && remote::IsLoopbackIpv4(ntohl(peer.sin_addr.s_addr));

        DWORD timeout = kReceiveTimeoutMs;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);

        std::string buffer;
        remote::Request request;
        remote::ParseStatus status = remote::ParseStatus::Incomplete;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kReceiveTimeoutMs);
        char chunk[2048];
        while (status == remote::ParseStatus::Incomplete && loopback && std::chrono::steady_clock::now() < deadline) {
            int received = recv(client, chunk, sizeof chunk, 0);
            if (received <= 0) break;
            buffer.append(chunk, static_cast<size_t>(received));
            status = remote::ParseRequest(buffer, request);
        }

        remote::Response response;
        if (!loopback) {
            response = router.Handle(request, false, *m_backend);  // 403, without reading anything
        } else if (status == remote::ParseStatus::Complete) {
            response = router.Handle(request, true, *m_backend);
        } else if (status == remote::ParseStatus::TooLarge) {
            response = remote::Response{413, R"({"ok":false,"error":"requête trop grande"})"};
        } else {
            response = remote::Response{400, R"({"ok":false,"error":"requête HTTP illisible"})"};
        }
        std::string bytes = remote::Serialize(response);
        size_t sent = 0;
        while (sent < bytes.size()) {
            int n = send(client, bytes.data() + sent, static_cast<int>(bytes.size() - sent), 0);
            if (n <= 0) break;
            sent += static_cast<size_t>(n);
        }
        shutdown(client, SD_BOTH);
        closesocket(client);
    }
}

}  // namespace amk::win
