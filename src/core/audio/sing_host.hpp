#pragma once

// The host capability behind Ampify.audio.sing (src/core/natives/sing.cpp): what the interface asks of Sing and what the
// engine answers, as src/core/audio/audio_share.hpp lays them out. Read with host.Capability<SingHost>(): null on a host
// without it, where set answers false and the state says nothing is known. Both processes have it on Windows, since the
// request and the status cross through the shared block.

#include <cstdint>
#include <optional>
#include <string_view>

#include "audio_share.hpp"

namespace ampify {

struct SingHost {
    static constexpr std::string_view kCapabilityName = "audio.sing";

    virtual ~SingHost() = default;

    // False when the request could not be written (no shared block).
    virtual bool Set(audio::SingMode mode, double voice, audio::SingModel model) = 0;
    // Empty when the block cannot be read.
    virtual std::optional<audio::SingRequest> Request() = 0;
    virtual std::optional<audio::SingStatus> Status() = 0;
    // Whether the files studio mode needs with `model` are downloaded and checked, and how many bytes using it would
    // download; empty when this host cannot tell.
    struct Pack {
        bool installed = false;
        std::uint64_t downloadSize = 0;
    };
    virtual std::optional<Pack> PackFor(audio::SingModel) { return std::nullopt; }
};

}  // namespace ampify
