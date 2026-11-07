// The output files: the only writes an element can make, in <kit>\output\<id>\ (src/core/output_files.*), through
// Ampify.fs.writeText and the permission fs.output.

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/core/manifest.hpp"
#include "../src/core/output_files.hpp"
#include "service_host.hpp"
#include "support.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

std::string ReadText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

}  // namespace

TEST_CASE(OutputFiles_AcceptOnlyPlainTextNames) {
    for (const char* ok : {"now-playing.txt", "a.json", "stats_2026.csv", "page.html", "x.XML", "v1.2.txt"}) {
        CHECK(ampify::IsValidOutputName(ok));
    }
    for (const char* bad : {"", "../x.txt", "dir/x.txt", "dir\\x.txt", ".hidden.txt", "x.exe", "x", "x.", "con.txt",
                            "NUL.json", "a..b.txt", "C:x.txt", "x.txt ", "é.txt"}) {
        if (ampify::IsValidOutputName(bad)) ampify::test::ReportFailure(__FILE__, __LINE__, std::string("accepted: ") + bad);
    }
    CHECK(!ampify::IsValidOutputName(std::string(61, 'a') + ".txt"));
}

TEST_CASE(OutputFiles_WriteReplaceAndLimits) {
    ampify::test::ScratchDir root("write");
    std::string error;
    CHECK(ampify::WriteOutputFile(root, "demo", "now.txt", "Title - Artist", &error));
    CHECK(ReadText(root / "demo" / "now.txt") == "Title - Artist");
    CHECK(ampify::WriteOutputFile(root, "demo", "now.txt", "Autre", &error));
    CHECK(ReadText(root / "demo" / "now.txt") == "Autre");
    CHECK(!fs::exists(root / "demo" / "now.txt.tmp"));

    CHECK(!ampify::WriteOutputFile(root, "demo", "../evil.txt", "x", &error));
    CHECK(!fs::exists(root / "evil.txt"));
    CHECK(!ampify::WriteOutputFile(root, "demo", "big.txt", std::string(ampify::kMaxOutputFileBytes + 1, 'a'), &error));

    for (size_t i = 1; i < ampify::kMaxOutputFilesPerElement; i++) {
        CHECK(ampify::WriteOutputFile(root, "demo", "f" + std::to_string(i) + ".txt", "x", &error));
    }
    CHECK(!ampify::WriteOutputFile(root, "demo", "one-too-many.txt", "x", &error));
    CHECK(error.find("16") != std::string::npos);
    CHECK(ampify::WriteOutputFile(root, "demo", "now.txt", "remplacer reste possible", &error));
}

TEST_CASE(FsModule_WritesOnlyInTheElementFolder) {
    ampify::test::ServiceFixture<> f;
    ampify::test::ScratchDir scratch("module");
    f.host.outputRoot = scratch;
    auto without = f.Make("fs-none", R"(try { Ampify.fs.writeText("a.txt", "x"); } catch (e) { Ampify.log("refused " + e.name); })");
    CHECK(without->Start());
    CHECK(f.Logged("refused PermissionDenied"));

    auto element = f.Make("fs-demo", R"(
        Ampify.log("written " + Ampify.fs.writeText("now-playing.txt", "Title - Artist"));
        try { Ampify.fs.writeText("../../settings.txt", "x"); } catch (e) { Ampify.log("path " + e.name); }
        try { Ampify.fs.writeText("a.txt", "x".repeat(300000)); } catch (e) { Ampify.log("size " + e.name); }
    )", R"("fs.output")");
    CHECK(element->Start());
    CHECK(f.Logged("written true"));
    CHECK(f.Logged("path TypeError"));
    CHECK(f.Logged("size RangeError"));
    CHECK(ReadText(f.host.outputRoot / "fs-demo" / "now-playing.txt") == "Title - Artist");
}

TEST_CASE(Permissions_FsOutputIsKnown) {
    CHECK(ampify::IsKnownPermission("fs.output"));
}
