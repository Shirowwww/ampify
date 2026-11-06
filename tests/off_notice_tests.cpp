// The notice shown when Ampify stays off (src/core/off_notice.*): once per cause, again only after Ampify loaded.

#include <filesystem>

#include "../src/core/off_notice.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

const ampify::BuildSignature kMatching{true, {}};
const char* const kNewBuild = "1.9999.1.0";
const char* const kTested = "1.1540.23042.0";

fs::path FreshFile(const char* name) {
    fs::path dir = fs::temp_directory_path() / "ampify-tests" / "off-notice";
    fs::create_directories(dir);
    fs::path file = dir / name;
    fs::remove(file);
    return file;
}

ampify::PolicyVerdict Untested(const char* version = kNewBuild) {
    return ampify::DecideInjection(version, false, kMatching);
}

}  // namespace

TEST_CASE(OffNotice_AnUntestedBuildAndSafeModeCallForOne_OthersDoNot) {
    CHECK(ampify::OffNoticeKey(Untested(), kNewBuild) == std::string("version:") + kNewBuild);
    CHECK(ampify::OffNoticeKey(ampify::DecideInjection(kTested, true, kMatching), kTested) == "safe-mode");
    CHECK(ampify::OffNoticeKey(ampify::DecideInjection("", false, kMatching), "").empty());
    CHECK(ampify::OffNoticeKey(ampify::DecideInjection(kTested, false, kMatching), kTested).empty());
}

TEST_CASE(OffNotice_ABuildWithTheWrongExportsHasItsOwnCauseAndIsToldOnce) {
    ampify::BuildSignature missing{true, {"AVCFPlayerSetVolume"}};
    ampify::PolicyVerdict verdict = ampify::DecideInjection(kTested, false, missing);
    CHECK(verdict.cause == ampify::OffCause::WrongExports);
    CHECK(Untested().cause == ampify::OffCause::UntestedVersion);
    CHECK(ampify::OffNoticeKey(verdict, kTested) == std::string("version:") + kTested);
}

TEST_CASE(OffNotice_TheSameBuildIsToldOnce) {
    fs::path file = FreshFile("once.txt");
    CHECK(ampify::TakeOffNotice(file, Untested(), kNewBuild));
    CHECK(!ampify::TakeOffNotice(file, Untested(), kNewBuild));
    CHECK(!ampify::TakeOffNotice(file, Untested(), kNewBuild));
}

TEST_CASE(OffNotice_AnotherBuildIsToldAgain) {
    fs::path file = FreshFile("another.txt");
    CHECK(ampify::TakeOffNotice(file, Untested(), kNewBuild));
    CHECK(ampify::TakeOffNotice(file, Untested("1.9999.2.0"), "1.9999.2.0"));
}

TEST_CASE(OffNotice_AfterAnAllowedStartTheNextStopIsToldAgain) {
    fs::path file = FreshFile("again.txt");
    ampify::PolicyVerdict safe = ampify::DecideInjection(kTested, true, kMatching);
    CHECK(ampify::TakeOffNotice(file, safe, kTested));
    CHECK(!ampify::TakeOffNotice(file, safe, kTested));
    CHECK(!ampify::TakeOffNotice(file, ampify::DecideInjection(kTested, false, kMatching), kTested));
    CHECK(ampify::TakeOffNotice(file, safe, kTested));
}

TEST_CASE(OffNotice_AMissingVersionIsNeverRecordedNorTold) {
    fs::path file = FreshFile("empty.txt");
    CHECK(!ampify::TakeOffNotice(file, ampify::DecideInjection("", false, kMatching), ""));
    CHECK(!fs::exists(file));
}
