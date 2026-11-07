#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "apple_facts.hpp"

namespace ampify {

// "1.1792" for "1.1792.39942.0": the first two numbers, which Apple changes when the app itself changes. Empty when
// `version` is not a build of four numbers.
std::string_view AppVersionFamily(std::string_view version);

// Hooks are only installed on a build of a family tested in kKnownAppVersions (facts/apple.json) or versions.txt:
// Apple ships several builds of one family, and the build signature, checked next, says whether the player changed.
// On a build of any other family the kernel stays idle: the app runs untouched, nothing is guessed by address.
bool IsKnownAppVersion(std::string_view version,
                       std::span<const std::string_view> known = kKnownAppVersions);

// The built-in builds, then the builds of `extra` (read from <kit>\versions.txt, see version_list.hpp) that are not
// built in. The views point into kKnownAppVersions and into `extra`, which must outlive the result. Pass the result
// as `known` to IsKnownAppVersion, DecideInjection or DecideKernel: the build signature is still checked there.
std::vector<std::string_view> KnownAppVersionsWith(const std::vector<std::string>& extra);

}  // namespace ampify
