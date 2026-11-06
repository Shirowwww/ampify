#include "smtc_session.hpp"

#include <algorithm>
#include <cctype>

#include "../core/text_util.hpp"
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
// ... TrackNumber 11, Genres 12, AlbumTrackCount 13, PlaybackType 14, Thumbnail 15.
constexpr int kPropertiesThumbnail = 15;

// Windows.Storage.Streams, from windows.storage.streams.h: IRandomAccessStreamReference.OpenReadAsync 6,
// IContentTypeProvider.get_ContentType 6.
const GUID kIidStreamReference = rt::GuidOf("33EE3134-1DD6-4E3A-8067-D1C162E8642B");
const GUID kIidContentTypeProvider = rt::GuidOf("97D098A5-3B99-4DE9-88A5-E11D2F50C795");
const GUID kIidRandomAccessStream = rt::GuidOf("905A0FE1-BC53-11DF-8C49-001E4FC686DA");
constexpr int kReferenceOpenRead = 6;
constexpr int kContentType = 6;
constexpr size_t kMaxThumbnailBytes = 4 * 1024 * 1024;  // what Ampify.player.artwork() hands a script

constexpr unsigned long long kSearchEveryMs = 2000;
constexpr unsigned long long kPropertiesEveryMs = 1000;
constexpr DWORD kCommandTimeoutMs = 3000;

// Apple Music's package family is AppleInc.AppleMusicWin_nzyj5cx40ttqa; its session id contains "AppleMusic".
bool IsAppleMusic(const std::string& appId) {
    std::string lower = ToLowerAscii(appId);
    return lower.find("applemusic") != std::string::npos;
}

// The Apple Music session among the manager's sessions. False when there is none.
bool FindAppleMusicSession(const rt::Ref& manager, rt::Ref& found, std::string& foundAppId) {
    rt::Ref sessions;
    if (FAILED(rt::CallSlot(manager, kManagerGetSessions, sessions.Put())) || !sessions) return false;
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
            found = session;
            foundAppId = appId;
            return true;
        }
    }
    return false;
}

// Starts the asynchronous call at `slot` of `object`, waits for it and gives its result.
bool AwaitResult(const rt::Ref& object, int slot, DWORD timeoutMs, rt::Ref& result) {
    rt::Ref operation;
    return SUCCEEDED(rt::CallSlot(object, slot, operation.Put())) && operation && rt::Wait(operation, timeoutMs) &&
           SUCCEEDED(rt::CallSlot(operation, rt::kAsyncGetResultsSlot, result.Put())) && result;
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
    FindAppleMusicSession(m_manager, m_session, m_appId);
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

std::optional<ArtworkHost::File> ReadSmtcThumbnail(DWORD timeoutMs) {
    rt::EnsureMultithreaded();
    rt::Ref statics, manager, session, propertiesObject, properties, thumbnailObject, thumbnail, stream;
    std::string appId;
    if (FAILED(rt::GetActivationFactory(L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager",
                                        kIidManagerStatics, statics)
                   .result) ||
        !AwaitResult(statics, kStaticsRequestAsync, timeoutMs, manager) ||
        !FindAppleMusicSession(manager, session, appId) ||
        !AwaitResult(session, kSessionMediaPropertiesAsync, timeoutMs, propertiesObject) ||
        !rt::Query(propertiesObject, kIidMediaProperties, properties) ||
        FAILED(rt::CallSlot(properties, kPropertiesThumbnail, thumbnailObject.Put())) || !thumbnailObject ||
        !rt::Query(thumbnailObject, kIidStreamReference, thumbnail) ||
        !AwaitResult(thumbnail, kReferenceOpenRead, timeoutMs, stream)) {
        return std::nullopt;
    }
    ArtworkHost::File file;
    file.title = rt::ReadString(properties, kPropertiesTitle);
    file.artist = rt::ReadString(properties, kPropertiesArtist);
    rt::Ref typed, randomAccess;
    // Windows may list several names ("image/jpeg,image/jpe,image/jpg"): the first is the type.
    if (rt::Query(stream, kIidContentTypeProvider, typed)) file.contentType = rt::ReadString(typed, kContentType);
    file.contentType = file.contentType.substr(0, file.contentType.find(','));
    if (file.contentType.empty()) file.contentType = "image/jpeg";

    // shcore's bridge turns the WinRT stream into a plain IStream, read synchronously on this worker thread.
    using CreateStream = HRESULT(WINAPI*)(IUnknown*, REFIID, void**);
    static const HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto create = shcore ? reinterpret_cast<CreateStream>(GetProcAddress(shcore, "CreateStreamOverRandomAccessStream"))
                         : nullptr;
    IStream* plain = nullptr;
    if (!create || !rt::Query(stream, kIidRandomAccessStream, randomAccess) ||
        FAILED(create(static_cast<IUnknown*>(randomAccess.Get()), IID_IStream, reinterpret_cast<void**>(&plain)))) {
        return std::nullopt;
    }
    char chunk[64 * 1024];
    ULONG read = 0;
    while (SUCCEEDED(plain->Read(chunk, sizeof chunk, &read)) && read > 0 && file.bytes.size() <= kMaxThumbnailBytes) {
        file.bytes.append(chunk, read);
    }
    plain->Release();
    if (file.bytes.empty() || file.bytes.size() > kMaxThumbnailBytes) return std::nullopt;
    return file;
}

}  // namespace ampify::win
