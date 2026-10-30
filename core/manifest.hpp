#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amk {

enum class ElementKind { Extension, Theme, App, Snippet };

// The fields of a manifest.json that the kernel uses. The rules are those of docs/extensions/MANIFEST.md, which
// tools/validate.py also enforces. The kernel checks them again, because a folder can be edited by hand.
struct Manifest {
    std::string id;
    std::string name;
    std::string version;
    ElementKind kind = ElementKind::Extension;
    std::string author;
    std::string description;
    std::string kitApi;
    std::optional<std::string> homepage;
    std::optional<std::string> license;
    std::optional<std::string> main;
    std::optional<std::string> colors;
    std::optional<std::string> defaultScheme;
    std::optional<std::string> resources;
    std::optional<std::string> icon;
    std::vector<std::string> permissions;
    std::vector<std::string> appVersions;

    bool HasPermission(std::string_view permission) const;

    // The hosts named by `network:<host>` permissions, in manifest order.
    std::vector<std::string> NetworkHosts() const;
};

struct ManifestResult {
    std::optional<Manifest> manifest;  // set only when there are no errors
    std::vector<std::string> errors;   // French messages, one per problem
    std::vector<std::string> warnings; // unknown fields are ignored, with a warning
};

// Parses and checks the text of a manifest.json. `folderName` must match `id`. `hasFile` tells whether a file
// named in the manifest exists in the element's folder; it is a plain name such as "main.js", never a path.
ManifestResult ParseManifest(std::string_view text, std::string_view folderName, int supportedKitApiMajor,
                             const std::function<bool(const std::string&)>& hasFile);

// Accepts `storage`, `player.read`, ..., or `network:<host>` with a lowercase host name.
bool IsKnownPermission(std::string_view permission);

std::string_view ToString(ElementKind kind);

}  // namespace amk
