#pragma once

// What the kernel writes about Apple Music's own troubles (logic in src/core/reliability.*): the programs known to
// crash it, once at start, and the memory each of its processes holds, while the freeze log (apple.watchdog) is on.
// Neither changes anything in Apple Music.

#include <string>

#include "../core/journal.hpp"

namespace ampify::win {

// One line per known program that runs now, with its fix. Nothing when none runs.
void LogKnownConflicts(Journal& journal);

// The private memory of this process, read each minute and written when it reaches a new step of 512 MB. Never
// stopped: the thread ends with the process.
void StartMemoryWatch(Journal& journal, std::string process);

}  // namespace ampify::win
