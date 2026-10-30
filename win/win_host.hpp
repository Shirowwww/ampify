#pragma once

#include <filesystem>
#include <string>

#include "../core/host.hpp"
#include "../core/journal.hpp"
#include "../core/settings.hpp"

namespace amk {

// The kernel's Host on Windows. Settings live in settings.txt and each element's storage in
// storage\<id>.json, both under the kit folder. The player (M4), the audio gain (M4), the shortcuts and the
// network (M7) are not connected yet: they answer "not available" and never throw, because an exception
// cannot cross the C frames of QuickJS.
class WinHost : public Host {
public:
    WinHost(std::string process, std::filesystem::path kitDirectory, Settings& settings, Journal& journal);

    std::string Process() const override;
    std::string Locale() const override;

    std::optional<std::string> ReadSetting(const std::string& key) override;
    void WriteSetting(const std::string& key, const std::string& value) override;

    std::optional<std::string> ReadStorage(const std::string& elementId) override;
    bool WriteStorage(const std::string& elementId, const std::string& json) override;

    PlayerSnapshot Player() override;
    bool Control(PlayerCommand command, int64_t seekMs) override;

    void SetGainDb(const std::string& elementId, double db) override;

    void Notify(const std::string& elementId, const std::string& title, const std::string& text) override;

    bool RegisterKey(const std::string& elementId, const std::string& combo) override;
    void UnregisterKeys(const std::string& elementId) override;

    HttpResponse HttpGet(const std::string& url) override;

private:
    std::string m_process;
    std::filesystem::path m_kit;
    Settings& m_settings;
    Journal& m_journal;
};

}  // namespace amk
