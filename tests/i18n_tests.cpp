// English texts: lookups, placeholders and the language code of a locale (src/core/i18n.*), the sanity of the Ampify and
// command-line tables, and Ampify.i18n for extension authors.

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "../src/cli/element.hpp"
#include "../src/cli/messages.hpp"
#include "../src/core/i18n.hpp"
#include "../src/core/text_util.hpp"
#include "service_host.hpp"
#include "support.hpp"
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
    CHECK(ampify::i18n::LanguageCode("zh-CN") == "zh");
    CHECK(ampify::i18n::LanguageCode("zh-Hans-SG") == "zh");
    CHECK(ampify::i18n::LanguageCode("zh-TW") == "zh-hant");
    CHECK(ampify::i18n::LanguageCode("zh-Hant-HK") == "zh-hant");
    CHECK(ampify::i18n::LanguageCode("") == "en");  // no locale: English
    CHECK(ampify::i18n::LanguageCode("C") == "en");
    CHECK(ampify::i18n::LanguageCode("C.UTF-8") == "en");
    CHECK(ampify::i18n::LanguageCode("POSIX") == "en");
}

TEST_CASE(I18n_LookupReportsAMissingKeyAndTextShowsIt) {
    const ampify::i18n::Message table[] = {{"a", "hello {name}"}};
    CHECK(ampify::i18n::Lookup(table, "a") == "hello {name}");
    CHECK(!ampify::i18n::Lookup(table, "absent").has_value());
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
    std::string help(ampify::i18n::Lookup(ampify::cli::CliMessages(), "help").value_or(""));
    for (const char* command : {"install <id>", "theme    check", "export <", "--kit-dir"}) {
        CHECK(help.find(command) != std::string::npos);
    }
    CHECK(help.find("config language") == std::string::npos);  // Ampify is English only
}

TEST_CASE(I18n_FrenchTextsForWhatPeopleSeeAndEnglishForTheRest) {
    ampify::test::LanguageScope scope("en");
    CheckTable(ampify::i18n::KitMessagesIn("fr"), "kit fr");
    ampify::i18n::SetLanguage("fr");
    CHECK(ampify::i18n::Text("ui.on") == "Activé");
    CHECK(ampify::i18n::Text("page.back") == "Retour");
    CHECK(ampify::i18n::Text("discord.connected") == "Discord: connected, the presence is shown");  // the log stays English
    ampify::i18n::SetLanguage("sv");
    CHECK(ampify::i18n::Text("ui.on") == "On");  // a language without a table reads English
    ampify::i18n::SetLanguage("zh-hant");
    CHECK(ampify::i18n::Text("ui.on") == "开");  // Traditional Chinese reads the Simplified table
    ampify::i18n::SetLanguage("de");
    CHECK(ampify::i18n::Text("ui.on") == "Ein");
    CHECK(ampify::i18n::Text("mods.themeTitle", {{"name", "Nord"}}) == "Design: Nord");
    CHECK(ampify::i18n::UsesDecimalComma());
    ampify::i18n::SetLanguage("ja");
    CHECK(!ampify::i18n::UsesDecimalComma());
    ampify::i18n::SetLanguage("it");
    CHECK(ampify::i18n::Text("ui.on") == "Attivo");
    CHECK(ampify::i18n::UsesDecimalComma());
    ampify::i18n::SetLanguage("ko");
    CHECK(ampify::i18n::Text("ui.on") == "켜짐");
    CHECK(!ampify::i18n::UsesDecimalComma());

    ampify::i18n::SetLanguage("en");
    CHECK(ampify::i18n::Text("ui.on") == "On");
    // Every French key is one of the English table, so nothing French goes untranslated back to English by mistake.
    for (const ampify::i18n::Message& message : ampify::i18n::KitMessagesIn("fr")) {
        CHECK(ampify::i18n::Lookup(ampify::i18n::KitMessages(), message.key).has_value());
    }
}

TEST_CASE(I18n_PreferredLocaleIsTheSettingThenTheSystemThenEnglish) {
    CHECK(ampify::i18n::PreferredLocale("de", "fr-FR") == "de");
    CHECK(ampify::i18n::PreferredLocale("", "fr-FR") == "fr-FR");
    CHECK(ampify::i18n::PreferredLocale("", "") == "en");
    CHECK(ampify::i18n::LanguageCode(ampify::i18n::PreferredLocale("", "")) == "en");
}

TEST_CASE(I18n_TheInstallerTextsAreInEveryLanguage) {
    ampify::test::LanguageScope scope("en");
    CHECK(ampify::i18n::Text("setup.installedTitle") == "Ampify is installed");
    CHECK(ampify::i18n::Text("setup.fileMissing", {{"name", "x"}}) ==
          "A file is missing next to the installer: x\n\nExtract the whole zip first, then run the installer again.");
    CHECK(ampify::i18n::Text("setup.untested", {{"version", "1.2.3.4"}}).rfind("Apple Music 1.2.3.4 has not", 0) == 0);
    ampify::i18n::SetLanguage("fr");
    CHECK(ampify::i18n::Text("setup.installedTitle") == "Ampify est installé");
    CHECK(ampify::i18n::Text("setup.deleteData", {{"folder", "C:\\A"}}) ==
          "Supprimer aussi vos thèmes, réglages et extensions (C:\\A)");
    ampify::i18n::SetLanguage("zh");
    CHECK(ampify::i18n::Text("setup.installedTitle") == "Ampify 已安装");
}

TEST_CASE(I18n_TheListedLanguagesAreTheOnesWithATable) {
    std::vector<std::string_view> languages = ampify::i18n::Languages();
    REQUIRE(!languages.empty());
    CHECK(languages.front() == "en");
    CHECK(std::find(languages.begin(), languages.end(), "fr") != languages.end());
    for (size_t i = 1; i < languages.size(); ++i) {
        CHECK(ampify::i18n::KitMessagesIn(languages[i]).data() != ampify::i18n::KitMessages().data());
    }
    CHECK(ampify::i18n::KitMessagesIn("xx").data() == ampify::i18n::KitMessages().data());
}

TEST_CASE(I18n_EveryLanguageTranslatesWhatTheFrenchTableDoes) {
    auto french = ampify::i18n::KitMessagesIn("fr");
    for (std::string_view language : ampify::i18n::Languages()) {
        if (language == "en" || language == "fr") continue;
        auto table = ampify::i18n::KitMessagesIn(language);
        CheckTable(table, std::string(language).c_str());
        CHECK(table.size() == french.size());
        for (const ampify::i18n::Message& message : french) {
            auto text = ampify::i18n::Lookup(table, message.key);
            REQUIRE(text.has_value());
            CHECK(ampify::i18n::Placeholders(*text) == ampify::i18n::Placeholders(message.text));
        }
    }
}

// A Windows balloon cuts a title at 63 characters and a text at 255; the notices of a stopped Ampify must fit.
TEST_CASE(I18n_TheOffNoticesFitInABalloonInEveryLanguage) {
    for (const char* language : {"en", "fr", "de", "es", "pt", "ru", "ja", "zh", "it", "ko"}) {
        auto table = std::string_view(language) == "en" ? ampify::i18n::KitMessages() : ampify::i18n::KitMessagesIn(language);
        for (const char* group : {"off.version", "off.listed", "off.exports", "off.safe"}) {
            std::string title(ampify::i18n::Lookup(table, std::string(group) + "Title").value_or("off.missing"));
            std::string text(ampify::i18n::Lookup(table, std::string(group) + "Text").value_or("off.missing"));
            CHECK(ampify::CodePointCount(title) < 64);
            CHECK(ampify::CodePointCount(text) < 256);
            CHECK(title.find("off.") == std::string::npos);
        }
    }
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
