#include "discord_ipc.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

namespace amk::win {

namespace {

constexpr auto kRetryEvery = std::chrono::seconds(15);
constexpr DWORD kAnswerTimeoutMs = 3000;

}  // namespace

DiscordIpc::DiscordIpc(Journal& journal, i18n::Language language, std::wstring pipeBase)
    : m_journal(journal), m_language(language), m_pipeBase(std::move(pipeBase)) {
    m_thread = std::thread([this] { Run(); });
}

DiscordIpc::~DiscordIpc() {
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void DiscordIpc::Set(const std::string& clientId, std::optional<discord::Activity> activity) {
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!clientId.empty()) m_clientId = clientId;
        m_activity = std::move(activity);
        m_dirty = true;
    }
    m_wake.notify_all();
}

int DiscordIpc::accepted() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_accepted;
}

void DiscordIpc::Run() {
    bool retry = false;
    while (true) {
        std::string clientId;
        std::optional<discord::Activity> activity;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            auto ready = [this] { return m_stop || m_dirty; };
            if (retry) {
                m_wake.wait_for(lock, kRetryEvery, ready);
            } else {
                m_wake.wait(lock, ready);
            }
            if (m_stop) break;
            clientId = m_clientId;
            activity = m_activity;
            m_dirty = false;
        }
        retry = false;
        if (clientId.empty()) continue;

        if (m_pipe != INVALID_HANDLE_VALUE && m_connectedClient != clientId) Disconnect();
        if (m_pipe == INVALID_HANDLE_VALUE) {
            if (!activity) continue;  // nothing shown, nothing to clear
            if (!Connect(clientId)) {
                if (!m_warnedUnavailable) m_journal.Write(i18n::Text(m_language, "discord.unavailable"));
                m_warnedUnavailable = true;
                retry = true;
                continue;
            }
            m_warnedUnavailable = false;
            m_journal.Write(i18n::Text(m_language, "discord.connected"));
        }

        std::string nonce = std::to_string(++m_nonce);
        if (!Write(discord::Opcode::Frame, discord::SetActivityJson(GetCurrentProcessId(), activity, nonce))) {
            Disconnect();
            retry = activity.has_value();
            continue;
        }
        // Discord answers each command with the same nonce. Other frames (events) are skipped.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kAnswerTimeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            std::optional<discord::Frame> frame = Read(kAnswerTimeoutMs);
            if (!frame) break;
            if (std::optional<std::string> error = discord::ErrorOf(*frame)) {
                m_journal.Write(i18n::Text(m_language, "discord.error", {{"error", *error}}));
                if (frame->opcode == discord::Opcode::Close) Disconnect();
                break;
            }
            std::optional<Json> answer = Json::Parse(frame->json);
            const Json* answerNonce = answer ? answer->Find("nonce") : nullptr;
            if (answerNonce && answerNonce->AsString() && *answerNonce->AsString() == nonce) {
                std::lock_guard<std::mutex> guard(m_mutex);
                m_accepted++;
                break;
            }
        }
    }
    Disconnect();
}

bool DiscordIpc::Connect(const std::string& clientId) {
    for (int index = 0; index < 10; index++) {
        std::wstring name = m_pipeBase + std::to_wstring(index);
        HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) continue;
        m_pipe = pipe;
        m_reader = discord::FrameReader();
        if (Write(discord::Opcode::Handshake, discord::HandshakeJson(clientId))) {
            std::optional<discord::Frame> frame = Read(kAnswerTimeoutMs);
            if (frame && discord::IsReady(*frame)) {
                m_connectedClient = clientId;
                return true;
            }
            if (frame) {
                if (std::optional<std::string> error = discord::ErrorOf(*frame)) {
                    m_journal.Write(i18n::Text(m_language, "discord.error", {{"error", *error}}));
                }
            }
        }
        Disconnect();
    }
    return false;
}

void DiscordIpc::Disconnect() {
    if (m_pipe != INVALID_HANDLE_VALUE) CloseHandle(m_pipe);
    m_pipe = INVALID_HANDLE_VALUE;
    m_connectedClient.clear();
}

bool DiscordIpc::Write(discord::Opcode opcode, const std::string& json) {
    std::string bytes = discord::EncodeFrame(opcode, json);
    size_t sent = 0;
    while (sent < bytes.size()) {
        DWORD written = 0;
        if (!WriteFile(m_pipe, bytes.data() + sent, static_cast<DWORD>(bytes.size() - sent), &written, nullptr)) {
            return false;
        }
        sent += written;
    }
    return true;
}

std::optional<discord::Frame> DiscordIpc::Read(DWORD timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::vector<char> chunk(4096);
    while (true) {
        if (std::optional<discord::Frame> frame = m_reader.Next()) {
            if (frame->opcode == discord::Opcode::Ping) {
                if (!Write(discord::Opcode::Pong, frame->json)) return std::nullopt;
                continue;
            }
            return frame;
        }
        if (m_reader.failed()) return std::nullopt;
        // Polled, so that a silent Discord cannot hold the worker forever.
        DWORD available = 0;
        if (!PeekNamedPipe(m_pipe, nullptr, 0, nullptr, &available, nullptr)) return std::nullopt;
        if (available == 0) {
            if (std::chrono::steady_clock::now() >= deadline) return std::nullopt;
            Sleep(10);
            continue;
        }
        DWORD read = 0;
        DWORD want = std::min<DWORD>(available, static_cast<DWORD>(chunk.size()));
        if (!ReadFile(m_pipe, chunk.data(), want, &read, nullptr) || read == 0) return std::nullopt;
        m_reader.Feed(std::string_view(chunk.data(), read));
    }
}

}  // namespace amk::win
