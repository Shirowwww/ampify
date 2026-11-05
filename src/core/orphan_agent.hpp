#pragma once

// A library agent (AMPLibraryAgent.exe) left running after its Apple Music was killed holds the library: Apple Music
// then cannot open it (error 9039). This is the decision only; the launcher reads the processes and acts on it.

#include <cstdint>

namespace ampify {

enum class OrphanAction { None, WaitForExit, End };

// How long an agent that is alone gets to leave by itself before it is ended.
inline constexpr uint64_t kOrphanGraceMs = 10000;

// `enabled`: the apple.orphanAgent setting is on. `appRunning`: an AppleMusic.exe of the package runs, so the agent
// belongs to it. `waitedMs`: how long this agent has been running with no Apple Music.
OrphanAction DecideOrphanAgent(bool enabled, bool appRunning, bool agentRunning, uint64_t waitedMs);

}  // namespace ampify
