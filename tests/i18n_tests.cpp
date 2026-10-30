// French and English texts: the language choice, lookups and placeholders (core/i18n.*), the completeness of the
// kit and command-line tables, and AMK.i18n.

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "../cli/messages.hpp"
#include "../core/i18n.hpp"
#include "service_host.hpp"
#include "test.hpp"

namespace {

using amk::i18n::Language;

// Each placeholder name once, sorted: the two languages may order them differently.
std::set<std::string> NamesOf(std::string_view pattern) {
    std::set<std::string> names;
    std::string list = amk::i18n::Placeholders(pattern);
    size_t start = 0;
    while (start < list.size()) {
        size_t comma = list.find(',', start);
        names.insert(list.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return names;
}

void CheckTable(std::span<const amk::i18n::Message> table, const char* name) {
    std::set<std::string_view> keys;
    for (const amk::i18n::Message& message : table) {
        if (!keys.insert(message.key).second) {
            amk::test::ReportFailure(__FILE__, __LINE__, std::string(name) + " : clé en double " + std::string(message.key));
        }
        if (message.fr.empty() || message.en.empty()) {
            amk::test::ReportFailure(__FILE__, __LINE__, std::string(name) + " : texte vide pour " + std::string(message.key));
        }
        if (NamesOf(message.fr) != NamesOf(message.en)) {
            amk::test::ReportFailure(__FILE__, __LINE__,
                                     std::string(name) + " : variables différentes pour " + std::string(message.key));
        }
    }
}

}  // namespace

TEST_CASE(I18n_LanguageFollowsTheSettingThenTheLocale) {
    CHECK(amk::i18n::ResolveLanguage("fr", "en-US") == Language::Fr);
    CHECK(amk::i18n::ResolveLanguage("en", "fr-FR") == Language::En);
    CHECK(amk::i18n::ResolveLanguage("auto", "fr-FR") == Language::Fr);
    CHECK(amk::i18n::ResolveLanguage("auto", "fr_CA.UTF-8") == Language::Fr);
    CHECK(amk::i18n::ResolveLanguage("auto", "en-GB") == Language::En);
    CHECK(amk::i18n::ResolveLanguage("", "de-DE") == Language::En);  // other languages read English more often
    CHECK(amk::i18n::ResolveLanguage("auto", "") == Language::Fr);   // no locale: the kit's reference language
    CHECK(amk::i18n::ResolveLanguage("auto", "C") == Language::Fr);
    CHECK(amk::i18n::ResolveLanguage("auto", "C.UTF-8") == Language::Fr);
    CHECK(amk::i18n::ResolveLanguage("bogus", "en-US") == Language::En);
    CHECK(amk::i18n::IsLanguageSetting("auto") && amk::i18n::IsLanguageSetting("") && !amk::i18n::IsLanguageSetting("de"));
    CHECK(amk::i18n::Code(Language::En) == "en" && amk::i18n::Code(Language::Fr) == "fr");
}

TEST_CASE(I18n_LookupFallsBackAndFormatFillsPlaceholders) {
    const amk::i18n::Message table[] = {{"a", "bonjour {name}", "hello {name}"}, {"b", "seulement français", ""}};
    CHECK(amk::i18n::Lookup(table, Language::En, "a") == "hello {name}");
    CHECK(amk::i18n::Lookup(table, Language::En, "b") == "seulement français");
    CHECK(amk::i18n::Lookup(table, Language::Fr, "absent") == "absent");
    CHECK(amk::i18n::Format("bonjour {name}, {name} ! {autre}", {{"name", "Léa"}}) == "bonjour Léa, Léa ! {autre}");
    CHECK(amk::i18n::Format("{x}{y}", {{"x", "1"}, {"y", "2"}}) == "12");
    CHECK(amk::i18n::Format("accolade { seule") == "accolade { seule");
    CHECK(amk::i18n::Placeholders("{b} et {a} et {pas une variable}") == "b,a");
    CHECK(amk::i18n::Text(Language::En, "tray.next") == "Next track");
    CHECK(amk::i18n::Text(Language::Fr, "remote.started", {{"port", "8974"}}).find("127.0.0.1:8974") != std::string::npos);
}

TEST_CASE(I18n_EveryTextHasBothLanguagesWithTheSamePlaceholders) {
    CheckTable(amk::i18n::KitMessages(), "kit");
    CheckTable(amk::cli::CliMessages(), "cli");
    // The help names the same commands in both languages.
    std::string fr(amk::i18n::Lookup(amk::cli::CliMessages(), Language::Fr, "help"));
    std::string en(amk::i18n::Lookup(amk::cli::CliMessages(), Language::En, "help"));
    for (const char* command : {"install <id>", "config language fr|en|auto", "theme    check", "export <", "--kit-dir"}) {
        CHECK(fr.find(command) != std::string::npos);
        CHECK(en.find(command) != std::string::npos);
    }
}

TEST_CASE(I18n_CommandLineLanguageComesFromTheKitSettings) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "amk-tests" / "i18n-cli";
    fs::remove_all(dir);
    fs::create_directories(dir);
    CHECK(amk::cli::LanguageFor(dir / "settings.txt", "fr-FR") == Language::Fr);  // no file: auto
    CHECK(amk::cli::LanguageFor(dir / "settings.txt", "en-US") == Language::En);
    std::ofstream(dir / "settings.txt") << "language = fr\n";
    CHECK(amk::cli::LanguageFor(dir / "settings.txt", "en-US") == Language::Fr);

    amk::cli::SetLanguage(Language::En);
    CHECK(amk::cli::Tr("unknownCommand", {{"command", "x"}}) == "unknown command: x");
    amk::cli::SetLanguage(Language::Fr);
    CHECK(amk::cli::Tr("unknownCommand", {{"command", "x"}}) == "commande inconnue : x");
}

TEST_CASE(I18nModule_TextsOfAnElementInTheKitLanguage) {
    const std::string script = R"(
        AMK.i18n.define({ fr: { salut: "Bonjour {nom}", seul: "Seulement en français" }, en: { salut: "Hello {nom}" } });
        AMK.log("langue " + AMK.i18n.language + " " + AMK.locale);
        AMK.log("t1 " + AMK.i18n.t("salut", { nom: "Léa" }));
        AMK.log("t2 " + AMK.i18n.t("seul"));
        AMK.log("t3 " + AMK.i18n.t("kit.tray.next"));
        AMK.log("t4 " + AMK.i18n.t("absente"));
        try { AMK.i18n.define({ fr: 3 }); } catch (e) { AMK.log("define " + e.name); }
    )";
    amk::test::ServiceFixture<> f;
    auto french = f.Make("i18n-fr", script);
    CHECK(french->Start());
    CHECK(f.Logged("[i18n-fr] langue fr fr-FR"));
    CHECK(f.Logged("[i18n-fr] t1 Bonjour Léa"));
    CHECK(f.Logged("[i18n-fr] t3 Morceau suivant"));
    CHECK(f.Logged("[i18n-fr] t4 absente"));
    CHECK(f.Logged("[i18n-fr] define TypeError"));

    f.host.settings["language"] = "en";
    auto english = f.Make("i18n-en", script);
    CHECK(english->Start());
    CHECK(f.Logged("[i18n-en] langue en fr-FR"));  // AMK.locale stays the app locale
    CHECK(f.Logged("[i18n-en] t1 Hello Léa"));
    CHECK(f.Logged("[i18n-en] t2 Seulement en français"));
    CHECK(f.Logged("[i18n-en] t3 Next track"));
}
