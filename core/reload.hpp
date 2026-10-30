#pragma once

// reload.txt is written by `amk` (apply, dev, enable, disable, install, update, remove) and read by the kernel on its
// JavaScript thread. `amk` appends to it; each line is a request:
//
//   start <cible>     load the element if the kernel does not know it yet, then start it if the settings enable it
//   stop <cible>      stop the element
//   restart <cible>   stop it, read its folder again (manifest and code), start it if the settings enable it
//   rescan            look for new folders (load them, start the enabled ones) and drop the removed ones
//
// <cible> is the id of an extension or of a theme script, or `app:<id>` for an app: an app and an extension may share
// an id (they are then linked, see AMK.link). Other lines, such as the counter that `amk apply` writes, are ignored.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amk {

struct ReloadRequest {
    enum class Action { Start, Stop, Restart, Rescan };
    Action action = Action::Rescan;
    std::string target;  // empty for Rescan

    bool operator==(const ReloadRequest& other) const = default;
};

// True for `<id>` or `app:<id>`, with an id made of lowercase letters, digits and dashes.
bool IsReloadTarget(std::string_view target);

// The requests of complete lines, in file order, without repeats. Blank and unknown lines are skipped.
std::vector<ReloadRequest> ParseReloadRequests(std::string_view text);

// The targets named by `restart` lines, in file order, without repeats.
std::vector<std::string> ParseRestartRequests(std::string_view text);

// Follows the content of reload.txt. The first text only sets the baseline: the elements were just loaded, so the
// requests already in the file are not repeated. After that, when the new text extends the old one (amk appended),
// only the added lines count; when it was rewritten, every line of the new text counts. A line counts once it ends
// with a newline, so a line read half-written is taken on the next pass.
class ReloadWatcher {
public:
    std::vector<ReloadRequest> Requests(const std::string& text);

    // The targets of the `restart` lines among Requests(text).
    std::vector<std::string> Changed(const std::string& text);

private:
    std::optional<std::string> m_consumed;  // the text up to its last newline, already handled
};

}  // namespace amk
