#include <set>
#include <string>

#include "../src/core/manifest.hpp"
#include "test.hpp"

namespace {

const std::string kHello = R"({
  "id": "hello-extension", "name": "Hello", "version": "0.1.0", "kind": "extension",
  "author": "Shirowwww", "description": "Example.", "kitApi": "1.0", "main": "main.js"
})";

// The hello extension with one settings field and, optionally, a translations field, both before "main".
std::string WithFields(const std::string& settings, const std::string& translations = "") {
    std::string text = kHello;
    if (!translations.empty()) text.insert(text.find("\"main\""), "\"translations\": " + translations + ", ");
    text.insert(text.find("\"main\""), "\"settings\": " + settings + ", ");
    return text;
}

ampify::ManifestResult Check(const std::string& json) {
    return ampify::ParseManifest(json, "hello-extension", 1, [](const std::string& file) { return file == "main.js"; });
}

// The errors of a manifest's settings field, as the theme engine reads them.
std::vector<std::string> SettingsErrorsOf(const std::string& manifest) {
    std::vector<std::string> errors;
    ampify::DeclaredSettings(manifest, &errors);
    return errors;
}

const std::string kToggle = R"({"key": "a", "label": "A", "type": "toggle", "default": true)";

}  // namespace

TEST_CASE(ManifestSettings_ReadsTheGroupAndTheDescription) {
    ampify::ManifestResult result = Check(WithFields(R"([
        {"key": "seconds", "label": "Show the seconds", "type": "toggle", "default": true,
         "group": "Clock", "description": "Adds the seconds to the clock."},
        {"key": "halo", "label": "Halo", "type": "toggle", "default": false}])"));
    CHECK(result.errors.empty());
    if (!result.manifest || result.manifest->settings.size() != 2) return;
    CHECK(result.manifest->settings[0].group == "Clock");
    CHECK(result.manifest->settings[0].description == "Adds the seconds to the clock.");
    CHECK(result.manifest->settings[1].group.empty() && result.manifest->settings[1].description.empty());
}

TEST_CASE(ManifestSettings_RefusesAGroupOrADescriptionThatDoesNotFit) {
    const std::string wrong[] = {
        kToggle + R"(, "group": ""})",
        kToggle + R"(, "group": ")" + std::string(41, 'g') + R"("})",
        kToggle + R"(, "description": "two\nlines"})",
        kToggle + R"(, "description": ")" + std::string(201, 'd') + R"("})",
        kToggle + R"(, "group": 3})",
    };
    for (const std::string& setting : wrong) {
        CHECK(!SettingsErrorsOf(WithFields("[" + setting + "]")).empty());
    }
    CHECK(SettingsErrorsOf(WithFields("[" + kToggle + ", \"group\": \"Clock\", \"description\": \"Ok.\"}]")).empty());
}

TEST_CASE(ManifestSettings_TranslatesGroupsAndSettingDescriptions) {
    const std::string settings = R"([{"key": "halo", "label": "Halo", "type": "toggle", "default": false, "group": "Effects"}])";
    const std::string translations = R"({"fr": {"name": "Bonjour", "description": "Exemple.",
        "groups": {"Effects": "Effets"}, "settingDescriptions": {"halo": "Ajoute une lueur."}}})";
    ampify::ManifestResult result = Check(WithFields(settings, translations));
    CHECK(result.errors.empty());
    if (!result.manifest) return;
    const ampify::ManifestText& fr = result.manifest->translations.at("fr");
    CHECK(fr.groups.at("Effects") == "Effets");
    CHECK(fr.settingDescriptions.at("halo") == "Ajoute une lueur.");

    const std::string tooLong = R"({"fr": {"name": "Bonjour", "description": "Exemple.",
        "settingDescriptions": {"halo": ")" + std::string(201, 'x') + R"("}}})";
    CHECK(!Check(WithFields(settings, tooLong)).manifest);
}
