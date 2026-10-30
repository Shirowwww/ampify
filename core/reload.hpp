#pragma once

// reload.txt is written by `amk apply` and `amk dev`, and read by the kernel on its JavaScript thread. Its lines are
// requests: `restart <id>` restarts one JavaScript element (ExtensionLoader::Restart). The other lines, such as the
// counter that makes the file change on every apply, are ignored.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amk {

// The ids named by `restart` lines, in file order, without repeats. Blank and unknown lines are skipped.
std::vector<std::string> ParseRestartRequests(std::string_view text);

// Follows the content of reload.txt. The first text only sets the baseline: the elements were just loaded, so the
// requests already in the file are not repeated. After that, each change reports the restart requests it contains.
class ReloadWatcher {
public:
    std::vector<std::string> Changed(const std::string& text);

private:
    std::optional<std::string> m_last;
};

}  // namespace amk
