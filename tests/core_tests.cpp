#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/extension.hpp"
#include "../src/core/journal.hpp"
#include "../src/core/safe_mode.hpp"
#include "../src/core/settings.hpp"
#include "../src/core/version_gate.hpp"
#include "support.hpp"
#include "test.hpp"

TEST_CASE(Settings_RoundTripThroughFile) {
    ampify::test::ScratchDir scratch("settings-roundtrip");
    std::string path = (scratch / "settings.txt").string();

    ampify::Settings written;
    written.Set("player.name", "Sam");
    written.SetBool("extensions.atmos-leveler.enabled", true);
    written.SetInt("atmos.fallbackGainDb", 5);
    CHECK(written.Save(path));

    ampify::Settings read;
    CHECK(read.Load(path));
    CHECK(read.GetString("player.name", "") == "Sam");
    CHECK(read.GetBool("extensions.atmos-leveler.enabled", false));
    CHECK(read.GetInt("atmos.fallbackGainDb", 0) == 5);
}

TEST_CASE(Settings_MissingFileGivesDefaults) {
    ampify::test::ScratchDir scratch("settings-missing");
    std::string path = (scratch / "absent.txt").string();

    ampify::Settings settings;
    CHECK(settings.Load(path));
    CHECK(!settings.Get("anything").has_value());
    CHECK(settings.GetBool("flag", true));
    CHECK(settings.GetInt("count", 7) == 7);
}

TEST_CASE(Settings_ParsesHandWrittenFile) {
    ampify::test::ScratchDir dir("settings-handwritten");
    std::string path = (dir / "settings.txt").string();
    {
        std::ofstream out(path);
        out << "# a comment\n\n   spaced   =   value with = sign  \nflag = ON\nbroken line\n";
    }

    ampify::Settings settings;
    CHECK(settings.Load(path));
    CHECK(settings.GetString("spaced", "") == "value with = sign");
    CHECK(settings.GetBool("flag", false));
    CHECK(!settings.Get("broken line").has_value());
}

TEST_CASE(Settings_ReadsAFileSavedWithAByteOrderMark) {
    ampify::test::ScratchDir dir("settings-bom");
    std::string path = (dir / "settings.txt").string();
    {
        std::ofstream out(path, std::ios::binary);
        out << "\xEF\xBB\xBFtheme.active = greenroom\n";  // Notepad and PowerShell 5 start UTF-8 files with it
    }

    ampify::Settings settings;
    CHECK(settings.Load(path));
    CHECK(settings.GetString("theme.active", "") == "greenroom");
}

TEST_CASE(Settings_BadNumbersFallBack) {
    ampify::Settings settings;
    settings.Set("count", "twelve");
    settings.Set("flag", "maybe");

    CHECK(settings.GetInt("count", 3) == 3);
    CHECK(settings.GetBool("flag", true));
}

TEST_CASE(Settings_UpdateFileKeepsWhatAnotherWriterChanged) {
    ampify::test::ScratchDir scratch("settings-update");
    std::string path = (scratch / "settings.txt").string();
    std::string error;
    CHECK(ampify::Settings::UpdateFile(path, "extensions.demo.size", "20", error));  // a missing file starts empty

    ampify::Settings kernelCopy;  // read at start, before the card writes
    CHECK(kernelCopy.Load(path));
    CHECK(ampify::Settings::UpdateFile(path, "extensions.demo.side", "Right", error));  // the card
    CHECK(ampify::Settings::UpdateFile(path, "extensions.demo.size", "21", error));     // then config.set

    ampify::Settings now;
    CHECK(now.Load(path));
    CHECK(now.GetString("extensions.demo.side", "") == "Right");
    CHECK(now.GetInt("extensions.demo.size", 0) == 21);
    CHECK(error.empty());
}

TEST_CASE(Settings_UpdateFileRefusesAValueOnTwoLines) {
    ampify::test::ScratchDir scratch("settings-update-line");
    std::string path = (scratch / "settings.txt").string();
    std::string error;
    CHECK(ampify::Settings::UpdateFile(path, "extensions.demo.greeting", "hello", error));
    CHECK(!ampify::Settings::UpdateFile(path, "extensions.demo.greeting", "a\nb", error));
    CHECK(error.find("one line") != std::string::npos);

    ampify::Settings now;
    CHECK(now.Load(path));
    CHECK(now.GetString("extensions.demo.greeting", "") == "hello");
}

TEST_CASE(SafeMode_TripsAfterTwoUnhealthyLaunchesAndRecovers) {
    ampify::test::ScratchDir scratch("safe-mode");
    std::string path = (scratch / "state.txt").string();

    ampify::SafeMode safeMode(path, 2);
    CHECK(!safeMode.BeginLaunch());  // first launch: normal
    CHECK(!safeMode.BeginLaunch());  // second launch, still no sign of life: normal
    CHECK(safeMode.BeginLaunch());   // third: two failures in a row, so safe mode
    CHECK(safeMode.UnhealthyLaunches() == 3);

    safeMode.MarkHealthy();
    CHECK(safeMode.UnhealthyLaunches() == 0);
    CHECK(!safeMode.BeginLaunch());  // back to normal after a healthy run
}

TEST_CASE(SafeMode_CountSurvivesANewInstance) {
    ampify::test::ScratchDir scratch("safe-mode-persist");
    std::string path = (scratch / "state.txt").string();

    {
        ampify::SafeMode first(path, 2);
        CHECK(!first.BeginLaunch());
        CHECK(!first.BeginLaunch());
    }
    ampify::SafeMode second(path, 2);
    CHECK(second.BeginLaunch());  // the count was saved to disk, so this launch is safe
}

TEST_CASE(VersionGate_KnowsOnlyTestedFamilies) {
    CHECK(ampify::IsKnownAppVersion("1.1540.23042.0"));
    CHECK(ampify::IsKnownAppVersion("1.1540.23042.1"));  // same family as a tested build
    CHECK(!ampify::IsKnownAppVersion("1.1541.0.0"));
    CHECK(!ampify::IsKnownAppVersion("1.15.0.0"));
    CHECK(!ampify::IsKnownAppVersion("1.1540"));         // a family alone is not a build
    CHECK(!ampify::IsKnownAppVersion(""));

    const std::string_view custom[] = {"2.0.0.0"};
    CHECK(ampify::IsKnownAppVersion("2.0.0.0", custom));
    CHECK(ampify::IsKnownAppVersion("2.0.7.1", custom));
    CHECK(!ampify::IsKnownAppVersion("1.1540.23042.0", custom));
}

TEST_CASE(VersionGate_TakesEveryBuildOfTheTested1792Family) {
    CHECK(ampify::IsKnownAppVersion("1.1792.39681.0"));
    CHECK(ampify::IsKnownAppVersion("1.1792.39942.0"));
    CHECK(!ampify::IsKnownAppVersion("1.1800.1.0"));
    CHECK(ampify::AppVersionFamily("1.1792.39942.0") == "1.1792");
    CHECK(ampify::AppVersionFamily("1.1792").empty());
    CHECK(ampify::AppVersionFamily("").empty());
}

TEST_CASE(Journal_WritesStampedLines) {
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("12:00:00.000"); });
    journal.Write("kernel started");
    journal.Write("second line");

    CHECK(out.str() == "[12:00:00.000] kernel started\n[12:00:00.000] second line\n");
}

TEST_CASE(Extensions_StartOnlyWhatIsEnabled) {
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    ampify::ExtensionHost host(journal);

    bool aStarted = false, bStarted = false, cStarted = false;
    host.Add({"a", "A", true, [&] { aStarted = true; return true; }, nullptr});
    host.Add({"b", "B", false, [&] { bStarted = true; return true; }, nullptr});
    host.Add({"c", "C", true, [&] { cStarted = true; return true; }, nullptr});

    ampify::Settings settings;
    settings.SetBool(ampify::ElementRef{ampify::ElementKind::Extension, "b"}.EnabledKey(), true);   // turned on by the user
    settings.SetBool(ampify::ElementRef{ampify::ElementKind::Extension, "c"}.EnabledKey(), false);  // turned off by the user

    host.StartAll(settings);
    CHECK(aStarted);
    CHECK(bStarted);
    CHECK(!cStarted);
    CHECK(host.StateOf("a") == ampify::ExtensionState::Running);
    CHECK(host.StateOf("c") == ampify::ExtensionState::Off);
}

TEST_CASE(Extensions_OneFailureDoesNotStopTheOthers) {
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    ampify::ExtensionHost host(journal);

    host.Add({"throws", "", true, []() -> bool { throw std::runtime_error("boom"); }, nullptr});
    host.Add({"refuses", "", true, [] { return false; }, nullptr});
    host.Add({"fine", "", true, [] { return true; }, nullptr});

    host.StartAll(ampify::Settings{});
    CHECK(host.StateOf("throws") == ampify::ExtensionState::Failed);
    CHECK(host.StateOf("refuses") == ampify::ExtensionState::Failed);
    CHECK(host.StateOf("fine") == ampify::ExtensionState::Running);
    CHECK(out.str().find("boom") != std::string::npos);
}

TEST_CASE(Extensions_SafeModeBlocksEverything) {
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    ampify::ExtensionHost host(journal);

    bool started = false;
    host.Add({"a", "", true, [&] { started = true; return true; }, nullptr});

    host.StartAll(ampify::Settings{}, "safe mode");
    CHECK(!started);
    CHECK(host.StateOf("a") == ampify::ExtensionState::Blocked);
    CHECK(out.str().find("safe mode") != std::string::npos);
}

TEST_CASE(Extensions_StopInReverseOrderAndOnlyRunning) {
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    ampify::ExtensionHost host(journal);

    std::vector<std::string> order;
    host.Add({"first", "", true, [] { return true; }, [&] { order.push_back("first"); }});
    host.Add({"second", "", true, [] { return true; }, [&] { order.push_back("second"); }});
    host.Add({"off", "", false, [] { return true; }, [&] { order.push_back("off"); }});

    host.StartAll(ampify::Settings{});
    host.StopAll();

    CHECK((order == std::vector<std::string>{"second", "first"}));
    CHECK(host.StateOf("first") == ampify::ExtensionState::Off);
}

TEST_CASE(Extensions_RejectEmptyAndDuplicateIds) {
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    ampify::ExtensionHost host(journal);

    CHECK(host.Add({"a", "", false, nullptr, nullptr}));
    CHECK(!host.Add({"a", "", false, nullptr, nullptr}));
    CHECK(!host.Add({"", "", false, nullptr, nullptr}));
}
