// Windows only: the kit services of the Windows host. A fake Discord server on \\.\pipe\discord-ipc-0, a loopback
// client against the remote control, the notification area icon when the session has one, and MIDI input without a
// device.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../src/core/discord_frame.hpp"
#include "../src/core/midi_map.hpp"
#include "../src/core/settings.hpp"
#include "../src/win/discord_ipc.hpp"
#include "../src/win/midi.hpp"
#include "../src/win/remote_server.hpp"
#include "../src/win/tray.hpp"
#include "../src/win/win_host.hpp"
#include "../src/win/win_services.hpp"
#include "fake_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

const std::string kToken = "abcdefghijklmnopqrstuvwxyz0123456789ABCD";

fs::path KitFor(const std::string& name) {
    fs::path kit = fs::temp_directory_path() / "ampify-tests" / ("win-services-" + name);
    fs::remove_all(kit);
    fs::create_directories(kit);
    return kit;
}

// Waits up to `limit` for `done`, calling `tick` in between (the JavaScript thread's Service()).
template <class Done, class Tick>
bool WaitFor(Done done, Tick tick, std::chrono::milliseconds limit = 5000ms) {
    auto deadline = std::chrono::steady_clock::now() + limit;
    while (!done()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        tick();
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

// One frame from a blocking pipe, or nullopt when the client went away.
std::optional<ampify::discord::Frame> ReadFrame(HANDLE pipe, ampify::discord::FrameReader& reader) {
    char chunk[1024];
    while (true) {
        if (auto frame = reader.Next()) return frame;
        DWORD read = 0;
        if (!ReadFile(pipe, chunk, sizeof chunk, &read, nullptr) || read == 0) return std::nullopt;
        reader.Feed(std::string_view(chunk, read));
    }
}

bool WriteFrame(HANDLE pipe, ampify::discord::Opcode opcode, const std::string& json) {
    std::string bytes = ampify::discord::EncodeFrame(opcode, json);
    DWORD written = 0;
    return WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
           written == bytes.size();
}

// A whole HTTP exchange with the server on 127.0.0.1:port. Returns what the server sent.
std::string HttpExchange(uint16_t port, const std::string& request) {
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    std::string answer;
    if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0) {
        send(client, request.data(), static_cast<int>(request.size()), 0);
        char chunk[2048];
        int n = 0;
        while ((n = recv(client, chunk, sizeof chunk, 0)) > 0) answer.append(chunk, static_cast<size_t>(n));
    }
    closesocket(client);
    return answer;
}

std::string Request(const std::string& method, const std::string& path, const std::string& token) {
    std::string text = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    if (!token.empty()) text += "X-Ampify-Token: " + token + "\r\n";
    return text + "Content-Length: 0\r\n\r\n";
}

struct WinsockScope {
    WinsockScope() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockScope() { WSACleanup(); }
};

struct StaticBackend : ampify::remote::Backend {
    std::atomic<int> commands{0};
    std::string StatusJson() override { return R"({"playing":false})"; }
    bool Command(std::string_view) override {
        commands++;
        return true;
    }
    std::optional<double> SetGainDb(double db) override { return db; }
};

}  // namespace

TEST_CASE(WinDiscord_HandshakeThenActivityReachAFakeDiscord) {
    HANDLE server = CreateNamedPipeW(L"\\\\.\\pipe\\discord-ipc-0", PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                     PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
    if (server == INVALID_HANDLE_VALUE) {
        std::cout << "WinDiscord: \\\\.\\pipe\\discord-ipc-0 is already taken (Discord open?), test skipped\n";
        return;
    }
    std::string handshake;
    std::string command;
    std::thread fake([&] {
        if (!ConnectNamedPipe(server, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) return;
        ampify::discord::FrameReader reader;
        auto hello = ReadFrame(server, reader);
        if (!hello || hello->opcode != ampify::discord::Opcode::Handshake) return;
        handshake = hello->json;
        WriteFrame(server, ampify::discord::Opcode::Frame, R"({"cmd":"DISPATCH","evt":"READY","data":{"v":1}})");
        auto activity = ReadFrame(server, reader);
        if (!activity) return;
        command = activity->json;
        std::optional<ampify::Json> parsed = ampify::Json::Parse(activity->json);
        const ampify::Json* nonce = parsed ? parsed->Find("nonce") : nullptr;
        std::string reply = R"({"cmd":"SET_ACTIVITY","evt":null,"data":{},"nonce":")" +
                            (nonce && nonce->AsString() ? *nonce->AsString() : std::string()) + "\"}";
        WriteFrame(server, ampify::discord::Opcode::Frame, reply);
        // Wait for the client to close, as Discord would.
        char rest[256];
        DWORD read = 0;
        while (ReadFile(server, rest, sizeof rest, &read, nullptr) && read > 0) {
        }
    });

    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    {
        ampify::win::DiscordIpc ipc(journal);
        ampify::discord::Activity activity;
        activity.details = "Test title";
        activity.state = "Artist";
        ipc.Set("123456789012345678", activity);
        CHECK(WaitFor([&] { return ipc.accepted() == 1; }, [] {}));
    }  // the client closes the pipe here
    fake.join();
    CloseHandle(server);

    CHECK(handshake == R"({"v":1,"client_id":"123456789012345678"})");
    CHECK(command.find(R"("cmd":"SET_ACTIVITY")") != std::string::npos);
    CHECK(command.find(R"("details":"Test title")") != std::string::npos);
    CHECK(command.find(R"("pid":)" + std::to_string(GetCurrentProcessId())) != std::string::npos);
    CHECK(log.str().find("Discord: connected") != std::string::npos);
}

TEST_CASE(WinDiscord_NoClientMeansARetryLaterNotABlock) {
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    auto start = std::chrono::steady_clock::now();
    {
        ampify::win::DiscordIpc ipc(journal, L"\\\\.\\pipe\\ampify-test-no-discord-");
        ampify::discord::Activity activity;
        activity.details = "Title";
        ipc.Set("123456789012345678", activity);
        WaitFor([&] { return log.str().find("not found") != std::string::npos; }, [] {}, 3000ms);
        CHECK(ipc.accepted() == 0);
    }
    CHECK(log.str().find("Discord: client not found") != std::string::npos);
    CHECK(std::chrono::steady_clock::now() - start < 10s);  // the destructor does not wait for the 15 s retry
}

TEST_CASE(WinRemote_RefusesEveryAddressButLoopback) {
    StaticBackend backend;
    std::string error;
    for (const char* address : {"0.0.0.0", "192.168.1.10", "10.0.0.2", "::", "localhost"}) {
        ampify::win::RemoteServer server;
        ampify::win::RemoteServer::Config config;
        config.address = address;
        config.port = 0;
        config.token = kToken;
        CHECK(!server.Start(config, backend, &error));
        CHECK(!server.running());
        CHECK(error.find("127.0.0.1") != std::string::npos);
    }
    ampify::win::RemoteServer noToken;
    ampify::win::RemoteServer::Config config;
    config.port = 0;
    CHECK(!noToken.Start(config, backend, &error));
}

TEST_CASE(WinRemote_LoopbackClientNeedsTheToken) {
    StaticBackend backend;
    ampify::win::RemoteServer server;
    ampify::win::RemoteServer::Config config;
    config.port = 0;
    config.token = kToken;
    std::string error;
    CHECK(server.Start(config, backend, &error));
    CHECK(server.port() != 0);

    WinsockScope winsock;
    std::string status = HttpExchange(server.port(), Request("GET", "/status", kToken));
    CHECK(status.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
    CHECK(status.find(R"({"playing":false})") != std::string::npos);
    CHECK(HttpExchange(server.port(), Request("GET", "/status", "")).rfind("HTTP/1.1 401", 0) == 0);
    CHECK(HttpExchange(server.port(), Request("POST", "/next", "faux" + kToken)).rfind("HTTP/1.1 401", 0) == 0);
    CHECK(HttpExchange(server.port(), "not HTTP\r\n\r\n").rfind("HTTP/1.1 400", 0) == 0);
    CHECK(backend.commands == 0);
    CHECK(HttpExchange(server.port(), Request("POST", "/next", kToken)).rfind("HTTP/1.1 200", 0) == 0);
    CHECK(backend.commands == 1);
    server.Stop();
    CHECK(!server.running());
}

TEST_CASE(WinServices_RemoteControlRunsCommandsOnTheJavaScriptThread) {
    fs::path kit = KitFor("remote");
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::test::FakeHost host;
    host.settings["remote.enabled"] = "1";
    host.settings["remote.port"] = "0";  // a free port, for the test
    ampify::win::Services services(host, "engine", kit, journal);
    services.Service();

    std::string token = host.settings["remote.token"];
    CHECK(ampify::remote::IsValidToken(token));  // created on the first start
    CHECK(services.remotePort() != 0);
    CHECK(log.str().find("token created") != std::string::npos);
    CHECK(services.PublishRemoteState("counter", R"({"plays":4})"));

    WinsockScope winsock;
    std::string answer;
    std::thread client([&] { answer = HttpExchange(services.remotePort(), Request("POST", "/next", token)); });
    WaitFor([&] { return !host.commands.empty(); }, [&] { services.Service(); });
    client.join();
    CHECK(answer.rfind("HTTP/1.1 200", 0) == 0);
    CHECK(host.commands.size() == 1 && host.commands[0] == ampify::PlayerCommand::Next);

    std::thread gain([&] { answer = HttpExchange(services.remotePort(), Request("POST", "/volume?db=-4", token)); });
    WaitFor([&] { return !host.gains.empty(); }, [&] { services.Service(); });
    gain.join();
    CHECK(answer.find(R"("gainDb":-4)") != std::string::npos);
    CHECK(host.gains.size() == 1 && host.gains[0].first == "remote" && host.gains[0].second == -4);

    std::string status = HttpExchange(services.remotePort(), Request("GET", "/status", token));
    CHECK(status.find(R"("extensions":{"counter":{"plays":4}})") != std::string::npos);

    // Off in the interface process, whatever the setting: only AMPLibraryAgent.exe listens.
    ampify::win::Services ui(host, "ui", kit, journal);
    ui.Service();
    CHECK(ui.remotePort() == 0);
    CHECK(!ui.PublishRemoteState("x", "{}"));
}

TEST_CASE(WinServices_MidiMappingWithoutADevice) {
    std::vector<std::string> devices = ampify::win::MidiInput::Devices();
    ampify::win::MidiInput input;
    std::string error;
    if (devices.empty()) {
        CHECK(!input.Open(0, [](uint32_t) {}, &error));
        CHECK(error.find("no MIDI device") != std::string::npos);
    }

    fs::path kit = KitFor("midi");
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::test::FakeHost host;
    host.settings["midi.enabled"] = "1";
    host.settings["midi.map"] = "note:60=next;cc:7=gain";
    ampify::win::Services services(host, "engine", kit, journal);
    std::vector<std::string> events;
    services.SetEventSink([&](const std::string& entry, const std::string& payload) { events.push_back(entry + " " + payload); });
    services.Service();
    if (devices.empty()) CHECK(log.str().find("no input device") != std::string::npos);

    services.SubscribeMidi("pad");
    services.InjectMidi(ampify::midi::Pack(0x90, 60, 100));
    services.InjectMidi(ampify::midi::Pack(0xB0, 7, 127));
    services.InjectMidi(ampify::midi::Pack(0xE0, 0, 64));  // pitch bend: ignored
    services.Service();
    CHECK(host.commands.size() == 1 && host.commands[0] == ampify::PlayerCommand::Next);
    CHECK(host.gains.size() == 1 && host.gains[0].first == "midi" && host.gains[0].second == 0);
    CHECK(events.size() == 2);
    CHECK(!events.empty() && events[0] == R"(midi {"type":"note","channel":1,"number":60,"value":100})");

    services.UnsubscribeMidi("pad");
    services.InjectMidi(ampify::midi::Pack(0x90, 61, 100));
    services.Service();
    CHECK(events.size() == 2);  // nobody listens any more
}

TEST_CASE(WinServices_TrayIconWhenTheSessionHasANotificationArea) {
    fs::path kit = KitFor("tray");
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::test::FakeHost host;
    host.settings["tray.enabled"] = "1";
    ampify::win::Services services(host, "ui", kit, journal);
    std::vector<std::string> events;
    services.SetEventSink([&](const std::string& entry, const std::string& payload) { events.push_back(entry + " " + payload); });
    services.Service();
    if (!services.trayRunning()) {
        std::cout << "WinTray: the notification icon was refused (no notification area), test "
                     "skipped. Log: "
                  << log.str();
        CHECK(log.str().find("Windows refused") != std::string::npos);
        return;
    }
    CHECK(log.str().find("Notification area icon shown") != std::string::npos);

    ampify::TrayItem item;
    item.id = 1;
    item.label = "Mode calme";
    CHECK(services.SetTrayMenu("calme", {item}));

    services.InjectTrayCommand(ampify::win::Services::kTrayNext);
    services.InjectTrayCommand(ampify::win::Services::kTrayFirstElementItem);
    services.InjectTrayCommand(0);
    services.Service();
    CHECK(host.commands.size() == 1 && host.commands[0] == ampify::PlayerCommand::Next);
    CHECK(events.size() == 2);
    CHECK(!events.empty() && events[0] == R"(tray {"to":"calme","item":1})");
    CHECK(events.size() == 2 && events[1] == R"(tray {"click":true})");

    services.RemoveTrayMenu("calme");
    services.InjectTrayCommand(ampify::win::Services::kTrayFirstElementItem);
    services.Service();
    CHECK(events.size() == 2);  // the item is gone
}

TEST_CASE(WinTray_MenuChoicePostedToTheIconWindow) {
    ampify::win::TrayIcon tray;
    std::atomic<unsigned> chosen{0};
    std::string error;
    if (!tray.Start(L"Test", [&](UINT command) { chosen = command; }, &error)) {
        std::cout << "WinTray: icon refused (" << error << "), test skipped\n";
        return;
    }
    PostMessageW(tray.window(), WM_COMMAND, 7, 0);
    CHECK(WaitFor([&] { return chosen == 7; }, [] {}));
    PostMessageW(tray.window(), ampify::win::TrayIcon::kCallbackMessage, 1, WM_LBUTTONUP);
    CHECK(WaitFor([&] { return chosen == 0; }, [] {}));
    tray.Stop();
    CHECK(!tray.running());
}

TEST_CASE(WinHost_OffersTheKitServicesAndPost) {
    fs::path kit = KitFor("host");
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("engine", kit, settings, journal);

    CHECK(host.Capability<ampify::HttpPostHost>() != nullptr);
    CHECK(host.Capability<ampify::DiscordHost>() != nullptr);
    CHECK(host.Capability<ampify::TrayHost>() != nullptr);
    CHECK(host.Capability<ampify::MidiHost>() != nullptr);
    CHECK(host.Capability<ampify::RemoteHost>() != nullptr);
    ampify::OutputFilesHost* files = host.Capability<ampify::OutputFilesHost>();
    CHECK(files != nullptr);

    ampify::HttpResponse response = host.Capability<ampify::HttpPostHost>()->HttpPost(
        "https://nonexistent.invalid/x", "a=1", {{"content-type", "application/x-www-form-urlencoded"}});
    CHECK(response.status == 0 && !response.text.empty());

    std::string error;
    CHECK(files && files->WriteOutputText("obs", "now.txt", "Title", &error));
    CHECK(fs::exists(kit / "output" / "obs" / "now.txt"));

    // Every service is off by default: nothing listens, no icon.
    host.Service([](const std::string&, const std::string&) {});
    CHECK(!host.Capability<ampify::RemoteHost>()->PublishRemoteState("x", "{}"));
    CHECK(!host.Capability<ampify::TrayHost>()->SetTrayMenu("x", {}));
    CHECK(!host.Capability<ampify::MidiHost>()->SubscribeMidi("x"));
}
