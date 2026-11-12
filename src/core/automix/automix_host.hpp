#pragma once

// The host capability behind Ampify.automix (src/core/natives/automix.cpp): Apple's queue as a mix sees it, and the
// session that plays a plan (mix_session.hpp). Read with host.Capability<AutoMixHost>(); in Apple Music only the audio
// process (AMPLibraryAgent.exe) has it.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "mix_plan.hpp"
#include "mix_session.hpp"

namespace ampify {

struct AutoMixSong {
    std::string name;        // the song's stream address without its query; a plan's `next` names a song this way
    std::string id;          // its catalog id, empty when Apple's player does not say
    std::string storefront;  // the store it plays from, as Apple numbers it (143442 for France), or empty
    double time = 0;         // seconds into the song (0 for the next song)
    double duration = 0;     // seconds, 0 while unknown
    // It goes out in Dolby Atmos, which no mix can take: unknown until Apple's player has loaded its tracks, which
    // it does when the song starts.
    std::optional<bool> atmos;
};

struct AutoMixState {
    bool available = false;  // Apple's player was seen and every function the mix needs was found
    bool playing = false;
    std::optional<AutoMixSong> current;
    std::optional<AutoMixSong> next;
    automix::Status mix;
};

struct AutoMixHost {
    static constexpr std::string_view kCapabilityName = "automix";

    virtual ~AutoMixHost() = default;
    virtual AutoMixState State() = 0;
    // Empty when the plan is taken, else why not. The plan's `next` must still be the queue's next song.
    virtual std::string Schedule(const automix::MixPlan& plan) = 0;
    virtual void Cancel(const std::string& reason) = 0;
    // How many plans were taken so far: right after Schedule, the number of that plan.
    virtual std::uint64_t Plans() = 0;
};

}  // namespace ampify
