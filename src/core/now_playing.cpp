#include "now_playing.hpp"

#include <mutex>
#include <utility>

namespace ampify {

namespace {

std::mutex g_lock;
NowPlaying g_now;

}  // namespace

void PublishNowPlaying(NowPlaying now) {
    std::lock_guard<std::mutex> guard(g_lock);
    g_now = std::move(now);
}

NowPlaying CurrentNowPlaying() {
    std::lock_guard<std::mutex> guard(g_lock);
    return g_now;
}

}  // namespace ampify
