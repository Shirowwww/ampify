// English texts: lookups, placeholders and the language code of a locale (src/core/i18n.*), the sanity of the Ampify and
// command-line tables, and Ampify.i18n for extension authors.

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "../src/cli/messages.hpp"
#include "../src/core/i18n.hpp"
#include "service_host.hpp"
#include "test.hpp"

namespace {

void CheckTable(std::span<const ampify::i18n::Message> table, const char* name) {
    std::set<std::string_view> keys;
    for (const ampify::i18n::Message& message : table) {
        if (!keys.insert(message.key).second) {
            ampify::test::ReportFailure(__FILE__, __LINE__, std::string(name) + ": duplicate key " + std::string(message.key));
        }
        if (message.text.empty()) {
            ampify::test::ReportFailure(__FILE__, __LINE__, std::string(name) + ": empty text for " + std::string(message.key));
        }
    }
}

}  // namespace

TEST_CASE(I18n_LanguageCodeIsTheFirstTwoLettersOfTheLocale) {
    CHECK(ampify::i18n::LanguageCode("fr-FR") == "fr");
    CHECK(ampify::i18n::LanguageCode("en_US.UTF-8") == "en");
    CHECK(ampify::i18n::LanguageCode("DE-de") == "de");
    CHECK(ampify::i18n::LanguageCode("pt") == "pt");
    CHECK(ampify::i18n::LanguageCode("") == "en");  // no locale: English
    CHECK(ampify::i18n::LanguageCode("C") == "en");
    CHECK(ampify::i18n::LanguageCode("C.UTF-8") == "en");
    CHECK(ampify::i18n::LanguageCode("POSIX") == "en");
}

TEST_CASE(I18n_LookupFallsBackToTheKeyAndFormatFillsPlaceholders) {
    const ampify::i18n::Message table[] = {{"a", "hello {name}"}};
    CHECK(ampify::i18n::Lookup(table, "a") == "hello {name}");
    CHECK(ampify::i18n::Lookup(table, "absent") == "absent");
    CHECK(ampify::i18n::Format("hello {name}, {name}! {other}", {{"name", "Lea"}}) == "hello Lea, Lea! {other}");
    CHECK(ampify::i18n::Format("{x}{y}", {{"x", "1"}, {"y", "2"}}) == "12");
    CHECK(ampify::i18n::Format("lone { brace") == "lone { brace");
    CHECK(ampify::i18n::Placeholders("{b} and {a} and {not a variable}") == "b,a");
    CHECK(ampify::i18n::Text("tray.next") == "Next track");
    CHECK(ampify::i18n::Text("remote.started", {{"port", "8974"}}).find("127.0.0.1:8974") != std::string::npos);
    CHECK(ampify::i18n::Text("no.such.key") == "no.such.key");
}

TEST_CASE(I18n_EveryTextIsFilledAndEveryKeyIsUnique) {
    CheckTable(ampify::i18n::KitMessages(), "kit");
    CheckTable(ampify::cli::CliMessages(), "cli");
    std::string help(ampify::i18n::Lookup(ampify::cli::CliMessages(), "help"));
    for (const char* command : {"install <id>", "theme    check", "export <", "--kit-dir"}) {
        CHECK(help.find(command) != std::string::npos);
    }
    CHECK(help.find("config language") == std::string::npos);  // Ampify is English only
}

TEST_CASE(I18n_CommandLineTextsAreEnglish) {
    CHECK(ampify::cli::Tr("unknownCommand", {{"command", "x"}}) == "unknown command: x");
    CHECK(ampify::cli::Tr("needsId", {{"action", "install"}}) == "install needs an id");
    CHECK(ampify::cli::Tr("absent") == "absent");
}

TEST_CASE(I18nModule_TextsOfAnElementInTheDisplayLanguage) {
    const std::string script = R"(
        Ampify.i18n.define({ fr: { hello: "Bonjour {name}" }, en: { hello: "Hello {name}", only: "English only" } });
        Ampify.log("language " + Ampify.i18n.language + " " + Ampify.locale);
        Ampify.log("t1 " + Ampify.i18n.t("hello", { name: "Lea" }));
        Ampify.log("t2 " + Ampify.i18n.t("only"));
        Ampify.log("t3 " + Ampify.i18n.t("kit.tray.next"));
        Ampify.log("t4 " + Ampify.i18n.t("missing"));
        Ampify.log("t5 " + Ampify.i18n.t("hello"));
        try { Ampify.i18n.define({ fr: 3 }); } catch (e) { Ampify.log("define " + e.name); }
    )";
    ampify::test::ServiceFixture<> f;
    f.host.locale = "fr-FR";
    auto french = f.Make("i18n-fr", script);
    CHECK(french->Start());
    CHECK(f.Logged("[i18n-fr] language fr fr-FR"));
    CHECK(f.Logged("[i18n-fr] t1 Bonjour Lea"));
    CHECK(f.Logged("[i18n-fr] t2 English only"));  // no French text: English
    CHECK(f.Logged("[i18n-fr] t3 Next track"));    // the kit's own texts are English
    CHECK(f.Logged("[i18n-fr] t4 missing"));
    CHECK(f.Logged("[i18n-fr] t5 Bonjour {name}"));  // a placeholder without a value stays visible
    CHECK(f.Logged("[i18n-fr] define TypeError"));

    f.host.locale = "en-US";
    auto english = f.Make("i18n-en", script);
    CHECK(english->Start());
    CHECK(f.Logged("[i18n-en] language en en-US"));
    CHECK(f.Logged("[i18n-en] t1 Hello Lea"));

    f.host.locale = "de-DE";  // no German text: English
    auto german = f.Make("i18n-de", script);
    CHECK(german->Start());
    CHECK(f.Logged("[i18n-de] language de de-DE"));
    CHECK(f.Logged("[i18n-de] t1 Hello Lea"));
}
