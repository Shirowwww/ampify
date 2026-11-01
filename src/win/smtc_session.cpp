#include "smtc_session.hpp"

#include <algorithm>
#include <cctype>

#include "win_common.hpp"

namespace ampify::win {

namespace {

// Windows.Media.Control, from windows.media.control.h.
const GUID kIidManagerStatics = rt::GuidOf("2050C4EE-11A0-57DE-AED7-C97C70338245");
const GUID kIidSession = rt::GuidOf("7148C835-9B14-5AE2-AB85-DC9B1C14E1A8");
const GUID kIidPlaybackInfo = rt::GuidOf("94B4B6CF-E8BA-51AD-87A7-C10ADE106127");
const GUID kIidTimeline = rt::GuidOf("EDE34136-6F25-588D-8ECF-EA5B6735AAA5");
const GUID kIidMediaProperties = rt::GuidOf("68856CF6-ADB4-54B2-AC16-05837907ACB6");

// IGlobalSystemMediaTransportControlsSessionManagerStatics: RequestAsync 6.
constexpr int kStaticsRequestAsync = 6;
// IGlobalSystemMediaTransportControlsSessionManager: GetCurrentSession 6, GetSessions 7.
constexpr int kManagerGetSessions = 7;
// IVectorView<T>: GetAt 6, get_Size 7, IndexOf 8, GetMany 9.
constexpr int kVectorViewGetAt = 6;
constexpr int kVectorViewSize = 7;
// IGlobalSystemMediaTransportControlsSession: SourceAppUserModelId 6, TryGetMediaPropertiesAsync 7,
// GetTimelineProperties 8, GetPlaybackInfo 9, TryPlayAsync 10, TryPauseAsync 11, TryStopAsync 12, TryRecordAsync 13,
// TryFastForwardAsync 14, TryRewindAsync 15, TrySkipNextAsync 16, TrySkipPreviousAsync 17,
// TryChangeChannelUpAsync 18, TryChangeChannelDownAsync 19, TryTogglePlayPauseAsync 20,
// TryChangeAutoRepeatModeAsync 21, TryChangePlaybackRateAsync 22, TryChangeShuffleActiveAsync 23,
// TryChangePlaybackPositionAsync 24.
constexpr int kSessionAppId = 6;
constexpr int kSessionMediaPropertiesAsync = 7;
constexpr int kSessionTimeline = 8;
constexpr int kSessionPlaybackInfo = 9;
constexpr int kSessionPlay = 10;
constexpr int kSessionPause = 11;
constexpr int kSessionSkipNext = 16;
constexpr int kSessionSkipPrevious = 17;
constexpr int kSessionChangePosition = 24;
// IGlobalSystemMediaTransportControlsSessionPlaybackInfo: Controls 6, PlaybackStatus 7.
constexpr int kInfoStatus = 7;
// IGlobalSystemMediaTransportControlsSessionTimelineProperties: StartTime 6, EndTime 7, MinSeekTime 8,
// MaxSeekTime 9, Position 10, LastUpdatedTime 11.
constexpr int kTimelineStart = 6;
constexpr int kTimelineEnd = 7;
constexpr int kTimelinePosition = 10;
constexpr int kTimelineLastUpdated = 11;
// IGlobalSystemMediaTransportControlsSessionMediaProperties: Title 6, Subtitle 7, AlbumArtist 8, Artist 9,
// AlbumTitle 10.
constexpr int kPropertiesTitle = 6;
constexpr int kPropertiesArtist = 9;
constexpr int kPropertiesAlbum = 10;

constexpr unsigned long long kSearchEveryMs = 2000;
constexpr unsigned long long kPropertiesEveryMs = 1000;
constexpr DWORD kCommandTimeoutMs = 3000;

// Apple Music's package family is AppleInc.AppleMusicWin_nzyj5cx40ttqa; its session id contains "AppleMusic".
bool IsAppleMusic(const std::string& appId) {
    std::string lower = appId;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("applemusic") != std::string::npos;
}

int64_t NowFileTime() {
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    return static_cast<int64_t>((static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime);
}

}  // namespace

bool SmtcSession::Start(std::string& failure) {
    if (m_started) return true;
    rt::Ref statics;
    rt::Activation activation = rt::GetActivationFactory(
        L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager", kIidManagerStatics, statics);
    if (FAILED(activation.result) || FAILED(rt::CallSlot(statics, kStaticsRequestAsync, m_request.Put())) ||
        !m_request) {
        failure = "SMTC API not available on this Windows";
        return false;
    }
    m_started = true;
    return true;
}

void SmtcSession::FindSession() {
    m_searchedAt = GetTickCount64();
    rt::Ref sessions;
    if (FAILED(rt::CallSlot(m_manager, kManagerGetSessions, sessions.Put())) || !sessions) return;
    uint32_t count = 0;
    rt::CallSlot(sessions, kVectorViewSize, &count);
    for (uint32_t i = 0; i < count; ++i) {
        rt::Ref candidate, session;
        if (FAILED(rt::CallSlot(sessions, kVectorViewGetAt, i, candidate.Put())) ||
            !rt::Query(candidate, kIidSession, session)) {
            continue;
        }
        std::string appId = rt::ReadString(session, kSessionAppId);
        if (IsAppleMusic(appId)) {
            m_session = session;
            m_appId = appId;
            return;
        }
    }
}

std::optional<SmtcReading> SmtcSession::Read() {
    if (!m_started) return std::nullopt;
    if (!m_manager) {
        if (rt::StateOf(m_request) == rt::AsyncState::Running) return std::nullopt;
        if (rt::StateOf(m_request) == rt::AsyncState::Failed ||
            FAILED(rt::CallSlot(m_request, rt::kAsyncGetResultsSlot, m_manager.Put())) || !m_manager) {
            m_started = false;  // a later Start() asks again
            m_request.Reset();
            return std::nullopt;
        }
        m_request.Reset();
    }
    unsigned long long now = GetTickCount64();
    if (!m_session && now - m_searchedAt >= kSearchEveryMs) FindSession();
    if (!m_session) return std::nullopt;

    SmtcReading reading;
    rt::Ref infoObject, info;
    if (FAILED(rt::CallSlot(m_session, kSessionPlaybackInfo, infoObject.Put()))) {
        m_session.Reset();  // the session closed: look for it again
        return std::nullopt;
    }
    if (rt::Query(infoObject, kIidPlaybackInfo, info)) rt::CallSlot(info, kInfoStatus, &reading.status);

    rt::Ref timelineObject, timeline;
    if (SUCCEEDED(rt::CallSlot(m_session, kSessionTimeline, timelineObject.Put())) &&
        rt::Query(timelineObject, kIidTimeline, timeline)) {
        rt::CallSlot(timeline, kTimelineStart, &reading.startTicks);  // TimeSpan: 100 ns
        rt::CallSlot(timeline, kTimelineEnd, &reading.endTicks);
        rt::CallSlot(timeline, kTimelinePosition, &reading.positionTicks);
        rt::CallSlot(timeline, kTimelineLastUpdated, &reading.lastUpdated);  // DateTime: 100 ns since 1601
    }
    reading.now = NowFileTime();

    // The titles come from an asynchronous call: collected when done, asked again every second.
    if (m_properties && rt::StateOf(m_properties) != rt::AsyncState::Running) {
        rt::Ref propertiesObject, properties;
        if (rt::StateOf(m_properties) == rt::AsyncState::Completed &&
            SUCCEEDED(rt::CallSlot(m_properties, rt::kAsyncGetResultsSlot, propertiesObject.Put())) &&
            rt::Query(propertiesObject, kIidMediaProperties, properties)) {
            m_title = rt::ReadString(properties, kPropertiesTitle);
            m_artist = rt::ReadString(properties, kPropertiesArtist);
            m_album = rt::ReadString(properties, kPropertiesAlbum);
        }
        m_properties.Reset();
    }
    if (!m_properties && now - m_propertiesAt >= kPropertiesEveryMs) {
        m_propertiesAt = now;
        rt::CallSlot(m_session, kSessionMediaPropertiesAsync, m_properties.Put());
    }
    reading.title = m_title;
    reading.artist = m_artist;
    reading.album = m_album;
    return reading;
}

bool SmtcSession::Command(PlayerCommand command, int64_t seekMs) {
    if (!m_session) return false;
    rt::Ref operation;
    HRESULT hr = E_FAIL;
    switch (command) {
        case PlayerCommand::Play: hr = rt::CallSlot(m_session, kSessionPlay, operation.Put()); break;
        case PlayerCommand::Pause: hr = rt::CallSlot(m_session, kSessionPause, operation.Put()); break;
        case PlayerCommand::Next: hr = rt::CallSlot(m_session, kSessionSkipNext, operation.Put()); break;
        case PlayerCommand::Previous: hr = rt::CallSlot(m_session, kSessionSkipPrevious, operation.Put()); break;
        case PlayerCommand::Seek: {
            // The position is a TimeSpan from the start of the timeline, in 100 ns.
            int64_t start = 0;
            rt::Ref timelineObject, timeline;
            if (SUCCEEDED(rt::CallSlot(m_session, kSessionTimeline, timelineObject.Put())) &&
                rt::Query(timelineObject, kIidTimeline, timeline)) {
                rt::CallSlot(timeline, kTimelineStart, &start);
            }
            hr = rt::CallSlot(m_session, kSessionChangePosition, start + seekMs * 10'000, operation.Put());
            break;
        }
    }
    if (FAILED(hr) || !operation || !rt::Wait(operation, kCommandTimeoutMs)) return false;
    // IAsyncOperation<Boolean>.GetResults: whether the app accepted the command.
    unsigned char accepted = 0;
    return SUCCEEDED(rt::CallSlot(operation, rt::kAsyncGetResultsSlot, &accepted)) && accepted != 0;
}

}  // namespace ampify::win
