#include "journal.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <utility>

namespace ampify {

Journal::Journal(std::ostream& out, Stamp stamp) : m_out(out), m_stamp(std::move(stamp)) {}

Journal::Journal(Journal& parent, std::string prefix)
    : m_out(parent.m_out), m_parent(&parent), m_prefix(std::move(prefix)) {}

void Journal::Write(std::string_view message) {
    if (m_parent) {
        m_parent->Write(m_prefix + std::string(message));
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_out << '[' << m_stamp() << "] " << message << '\n';
    m_out.flush();
}

std::string Journal::LocalTimeStamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto millis = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::time_t seconds = system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03lld", local.tm_hour, local.tm_min, local.tm_sec,
                  static_cast<long long>(millis));
    return buffer;
}

}  // namespace ampify
