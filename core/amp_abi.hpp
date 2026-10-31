#pragma once

// The part of Apple's private WinRT player (AMP.Services, in AMP.Services.dll of AppleMusic.exe) that the kit calls,
// as plain numbers: interface identifiers and vtable slots. Portable on purpose: the Windows code (win/amp_player.cpp)
// calls these slots, and a Linux test (tests/player_model_tests.cpp) checks every slot against the method order of
// knowledge/winmd/AMP.Services.txt, the list of types and methods read from AMP.Services.winmd.
//
// How a slot is found. A WinRT interface derives from IInspectable, whose 6 methods take slots 0 to 5 (QueryInterface,
// AddRef, Release, GetIids, GetRuntimeClassName, GetTrustLevel). The interface's own methods follow in the order of
// its MethodDef rows in the winmd, which is the order of the "methods:" line in AMP.Services.txt. So the method at
// position k (from 0) of that line sits in slot 6 + k.
//
// Interface identifiers. MIDL 3 derives the identifier of an interface without an explicit [uuid] from a hash of the
// interface's definition, its methods and their types included. A successful QueryInterface with the identifier below
// therefore means the interface still has the layout these slots assume; a new Apple Music version that changes the
// interface changes its identifier, and QueryInterface fails cleanly instead of calling a wrong slot.
//
// The identifiers come from AMExclusive (https://github.com/AntiOblivionis/AMExclusive, MIT, src/ApplePlayback.h),
// which declares them from the same metadata for Apple Music 1.1540.23042.0. Reused with the owner's permission. The
// 1.1792 ones are the GuidAttribute values of AMP.Services.winmd 1.1792.
//
// Never called, by rule: SetVolumeAsync, SetMuteAsync and the EQ methods of IPlayer, and every FairPlay/DRM type.

namespace ampify::amp {

inline constexpr int kInspectableSlots = 6;

// Runtime class names, for RoGetActivationFactory.
inline constexpr char kPlayerClass[] = "AMP.Services.Player";
inline constexpr char kPlayQueueClass[] = "AMP.Services.PlayQueue";

// Interface identifiers (see above).
inline constexpr char kIidPlayerStatics[] = "34D100F9-0F0A-5980-8F04-7AF57FDB29B0";  // AMP.Services.IPlayerStatics
inline constexpr char kIidPlayerState[] = "BEFB99EB-336F-5199-9FB2-63635BC47319";  // ...CommonModels.IPlayerState
// IPlayer and INowPlayingItem, newest Apple Music first. 1.1792 added SetPositionAsync overloads to IPlayer and
// methods at the end of INowPlayingItem, so both identifiers changed; the slots below did not move (checked against
// knowledge/winmd/AMP.Services.txt, read from 1.1792). The kit tries each identifier in turn.
inline constexpr const char* kIidsPlayer[] = {
    "F807B158-B2E7-5CF7-A512-8B0BE767DF1B",  // AMP.Services.IPlayer, 1.1792
    "958DA6F6-2E93-5BA7-B604-28DD57FEB8E0",  // 1.1540
};
inline constexpr const char* kIidsNowPlayingItem[] = {
    "2DC5F65C-28F6-58AA-87CF-E4A7DF9B9A4B",  // AMP.Services.CommonModels.INowPlayingItem, 1.1792
    "E376ACB7-0569-56C5-8443-A288ACD362EA",  // 1.1540
};

// The cover and the lyrics of the item, in AMP.Services.winmd 1.1792 (the ids are the GuidAttribute values of
// IArtwork and ILyricsInfo). Nothing in the repository lists these two interfaces for 1.1540, so they are only read
// from an item that answers to the 1.1792 INowPlayingItem; a changed interface changes its identifier, and
// QueryInterface fails, so the field is skipped.
inline constexpr char kIidArtwork[] = "6F00C1EF-8302-5354-BD7E-8D1A943B4F0D";      // AMP.Services.CommonModels.IArtwork
inline constexpr char kIidLyricsInfo[] = "8DC3CA60-B650-5649-B901-D152B6C110F4";    // ...CommonModels.ILyricsInfo
// Windows.Foundation.IUriRuntimeClass, from the Windows SDK (windows.foundation.h).
inline constexpr char kIidUriRuntimeClass[] = "9E365E57-48B2-4160-956F-C7385120BBFC";

// Microsoft.UI.Xaml.Data (WinUI 3), which Apple Music's view models implement: INotifyPropertyChanged and its
// delegate. Source: CsWinRT, src/WinRT.Runtime/Interop/IID.g.cs (IID_MUX_INotifyPropertyChanged and
// IID_MUX_PropertyChangedEventHandler).
inline constexpr char kIidNotifyPropertyChanged[] = "90B17601-B065-586E-83D9-9ADC3A695284";
inline constexpr char kIidPropertyChangedHandler[] = "E3DE52F6-1E32-5DA6-BB2D-B5B6096C962D";

namespace slot {
// AMP.Services.IPlayerStatics: get_Current.
inline constexpr int kPlayerStaticsCurrent = 6;

// AMP.Services.IPlayer: get_NowPlayingItem, get_PlayerState, StartOrQueueLibraryPlaybackAsync, StartVUIPlaybackAsync,
// CreateAndPlayStationAsync, EnsureObjectGraphAsync, PlayAsync, PauseAsync, StopAsync, GoBackAsync, SkipPreviousAsync,
// SkipNextAsync, ClearAsync, ... SetPositionAsync (22), ... SetVolumeAsync (27 in 1.1792, never called) ...
inline constexpr int kPlayerNowPlayingItem = 6;
inline constexpr int kPlayerPlayerState = 7;
inline constexpr int kPlayerPlayAsync = 12;
inline constexpr int kPlayerPauseAsync = 13;
inline constexpr int kPlayerSkipPreviousAsync = 16;
inline constexpr int kPlayerSkipNextAsync = 17;

// AMP.Services.CommonModels.IPlayerState: get_CanPlay, get_CanPause, ... get_IsMuted, get_IsPaused, get_IsPlaying.
inline constexpr int kStateIsPaused = 24;
inline constexpr int kStateIsPlaying = 25;

// AMP.Services.CommonModels.INowPlayingItem: get_Subtitle, get_Artist, get_Album, ... get_Name (13th), ...
inline constexpr int kItemSubtitle = 6;
inline constexpr int kItemArtist = 7;
inline constexpr int kItemAlbum = 8;
inline constexpr int kItemArtwork = 11;
inline constexpr int kItemName = 19;
inline constexpr int kItemIsPlaceholder = 24;
inline constexpr int kItemCurrentPosition = 30;
inline constexpr int kItemDuration = 31;
inline constexpr int kItemAudioFormat = 38;
inline constexpr int kItemPlayQueueItemId = 45;
inline constexpr int kItemLyricsInfo = 46;

// AMP.Services.CommonModels.IArtwork: get_BGColor (Windows.UI.Color: four bytes A, R, G, B, written through the out
// pointer), put_BGColor, get_CropStyle, put_CropStyle, get_Format, put_Format, get_Height (double), put_Height,
// get_Width (double), put_Width, get_Uri (Windows.Foundation.Uri), GetCropCode. Only the getters are called.
inline constexpr int kArtworkBgColor = 6;
inline constexpr int kArtworkFormat = 10;
inline constexpr int kArtworkHeight = 12;
inline constexpr int kArtworkWidth = 14;
inline constexpr int kArtworkUri = 16;
inline constexpr int kArtworkCropCode = 17;
// Windows.Foundation.IUriRuntimeClass: get_AbsoluteUri is its first method.
inline constexpr int kUriAbsoluteUri = 6;

// AMP.Services.CommonModels.ILyricsInfo: get_LyricsState, get_LyricsSource, get_ContentDescriptor, get_IsEditable,
// get_RequiresReporting, get_MetricsJSON, HaveLyrics, GetStaticLyrics, GetTTML, GetLanguage.
inline constexpr int kLyricsHaveLyrics = 12;
inline constexpr int kLyricsStatic = 13;
inline constexpr int kLyricsTtml = 14;
inline constexpr int kLyricsLanguage = 15;

// AMP.Services.IPlayQueueStatics: get_Current. AMP.Services.IPlayQueue: get_AutoPlaySupported, get_AutoPlayEnabled,
// get_AutoPlayActive, get_Sections, ... Their identifiers are not known: the queue probe finds them at run time
// (win/amp_player.cpp) and only reads.
inline constexpr int kPlayQueueStaticsCurrent = 6;
inline constexpr int kPlayQueueSections = 9;
}  // namespace slot

// Every slot above with its interface and method, for the test against AMP.Services.txt.
struct SlotEvidence {
    const char* interfaceName;
    const char* method;
    int slot;
};

inline constexpr SlotEvidence kSlotEvidence[] = {
    {"AMP.Services.IPlayerStatics", "get_Current", slot::kPlayerStaticsCurrent},
    {"AMP.Services.IPlayer", "get_NowPlayingItem", slot::kPlayerNowPlayingItem},
    {"AMP.Services.IPlayer", "get_PlayerState", slot::kPlayerPlayerState},
    {"AMP.Services.IPlayer", "PlayAsync", slot::kPlayerPlayAsync},
    {"AMP.Services.IPlayer", "PauseAsync", slot::kPlayerPauseAsync},
    {"AMP.Services.IPlayer", "SkipPreviousAsync", slot::kPlayerSkipPreviousAsync},
    {"AMP.Services.IPlayer", "SkipNextAsync", slot::kPlayerSkipNextAsync},
    {"AMP.Services.CommonModels.IPlayerState", "get_IsPaused", slot::kStateIsPaused},
    {"AMP.Services.CommonModels.IPlayerState", "get_IsPlaying", slot::kStateIsPlaying},
    {"AMP.Services.CommonModels.INowPlayingItem", "get_Subtitle", slot::kItemSubtitle},
    {"AMP.Services.CommonModels.INowPlayingItem", "get_Artist", slot::kItemArtist},
    {"AMP.Services.CommonModels.INowPlayingItem", "get_Album", slot::kItemAlbum},
    {"AMP.Services.CommonModels.INowPlayingItem", "get_Name", slot::kItemName},
    {"AMP.Services.CommonModels.INowPlayingItem", "get_IsPlaceholder", slot::kItemIsPlaceholder},
    {"AMP.Services.CommonModels.INowPlayingItem", "get_CurrentPosition", slot::kItemCurrentPosition},
    {"AMP.Services.CommonModels.INowPlayingItem", "get_Duration", slot::kItemDuration},
    {"AMP.Services.CommonModels.INowPlayingItem", "get_AudioFormat", slot::kItemAudioFormat},
    {"AMP.Services.CommonModels.INowPlayingItem", "get_PlayQueueItemID", slot::kItemPlayQueueItemId},
    {"AMP.Services.CommonModels.INowPlayingItem", "get_Artwork", slot::kItemArtwork},
    {"AMP.Services.CommonModels.INowPlayingItem", "get_LyricsInfo", slot::kItemLyricsInfo},
    {"AMP.Services.CommonModels.IArtwork", "get_BGColor", slot::kArtworkBgColor},
    {"AMP.Services.CommonModels.IArtwork", "get_Format", slot::kArtworkFormat},
    {"AMP.Services.CommonModels.IArtwork", "get_Height", slot::kArtworkHeight},
    {"AMP.Services.CommonModels.IArtwork", "get_Width", slot::kArtworkWidth},
    {"AMP.Services.CommonModels.IArtwork", "get_Uri", slot::kArtworkUri},
    {"AMP.Services.CommonModels.IArtwork", "GetCropCode", slot::kArtworkCropCode},
    {"AMP.Services.CommonModels.ILyricsInfo", "HaveLyrics", slot::kLyricsHaveLyrics},
    {"AMP.Services.CommonModels.ILyricsInfo", "GetStaticLyrics", slot::kLyricsStatic},
    {"AMP.Services.CommonModels.ILyricsInfo", "GetTTML", slot::kLyricsTtml},
    {"AMP.Services.CommonModels.ILyricsInfo", "GetLanguage", slot::kLyricsLanguage},
    {"AMP.Services.IPlayQueueStatics", "get_Current", slot::kPlayQueueStaticsCurrent},
    {"AMP.Services.IPlayQueue", "get_Sections", slot::kPlayQueueSections},
};

// AMP.Services.CommonModels.AudioFormat, in the order of its fields in AMP.Services.txt: Other, Lossless,
// HiResLossless, DolbyAtmos, Spatial, Surround. The values are taken as 0 to 5, the MIDL default for an enum without
// explicit values; the PC confirms it on a lossless and an Atmos track.
enum class AudioFormat { Other = 0, Lossless = 1, HiResLossless = 2, DolbyAtmos = 3, Spatial = 4, Surround = 5 };

}  // namespace ampify::amp
