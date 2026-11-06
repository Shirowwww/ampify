#pragma once

// A library agent (AMPLibraryAgent.exe) left running after its Apple Music was killed holds the library: Apple Music
// then cannot open it (error 9039). This is the decision only; the launcher reads the processes and acts on it.

#include <cstdint>
#include <set>
#include <vector>

namespace ampify {

enum class OrphanAction { None, WaitForExit, End };

// How long an agent that is alone gets to leave by itself before it is ended.
inline constexpr uint64_t kOrphanGraceMs = 10000;

// `enabled`: the apple.orphanAgent setting is on. `appRunning`: an AppleMusic.exe of the package runs, so the agent
// belongs to it. `waitedMs`: how long this agent has been running with no Apple Music.
OrphanAction DecideOrphanAgent(bool enabled, bool appRunning, bool agentRunning, uint64_t waitedMs);

// The agents Windows refused to end: each process is tried once, so a refusal is not tried and logged again at every
// look. An id that leaves the list of running agents is forgotten, since Windows may give it to another process.
class OrphanRefusals {
public:
    // The agents of `running` not refused yet. Forgets the refused ids that no longer run.
    std::vector<uint32_t> Untried(const std::vector<uint32_t>& running);
    void Refused(uint32_t processId);

private:
    std::set<uint32_t> m_refused;
};

}  // namespace ampify
