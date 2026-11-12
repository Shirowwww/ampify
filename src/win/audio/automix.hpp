#pragma once

// AutoMix in AMPLibraryAgent.exe: Apple's queue player and a second player of Ampify's own (automix.cpp).

#include "../../core/automix/automix_host.hpp"

namespace ampify::win::audio_hooks {

AutoMixHost& AutoMix();

}  // namespace ampify::win::audio_hooks
