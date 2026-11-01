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

// The play queue, in AMP.Services.winmd 1.1792 (GuidAttribute values; a run-time probe found the first two in Apple
// Music 1.1792). A section of PlayQueue.Sections is a PlayQueueSection that holds its entries
// (IComponentModelContainer); an entry is a PlayQueueItem, titled (ITitledComponent) and backed by a ContentDescriptor
// whose AdamId is the catalog id. What was tried and learned: knowledge/apple-music-queue.md.
inline constexpr char kIidPlayQueueStatics[] = "1CF4C1E2-8DA2-5642-911B-6F68D69D9F68";  // AMP.Services.IPlayQueueStatics
inline constexpr char kIidPlayQueue[] = "5DA4729B-8546-5210-B8A2-066B11D68E8D";         // AMP.Services.IPlayQueue
inline constexpr char kIidPlayQueueSection[] = "D05888FD-5CD9-5DB6-98DE-D9C14F17529B";  // ...IPlayQueueSection
inline constexpr char kIidComponentModelContainer[] = "E4D9E42B-143E-55A0-B149-346A4AA1D6B4";  // CommonModels.
inline constexpr char kIidPlayQueueItem[] = "72FE9B2F-3DE1-5517-8F3D-EBAAFAFD2552";            // CommonModels.
inline constexpr char kIidTitledComponent[] = "29691940-53DC-5C20-942F-29EB78F38F1F";          // CommonModels.
inline constexpr char kIidContentDescriptorDriven[] = "0DB3EB30-B156-5201-AC4A-1326648C307B";  // CommonModels.
inline constexpr char kIidContentDescriptor[] = "659506EB-E228-5A4A-B8A4-0AF7125FB4E5";        // CommonModels.

// Collections of a given element type. A parameterized interface's identifier is the UUID version 5 of its signature
// in the WinRT namespace 11F47AD5-7B73-42C0-ABAE-878B1E16ADEE: in Python, uuid.uuid5(namespace,
// "pinterface({913337e9-11a1-4345-a3a2-4e7f956e222d};u8)") for IVector<UInt64>. IVector<IComponentModel> is one of
// the interfaces Apple Music 1.1792 lists for PlayQueue.Sections, with its IVectorView, IIterable and
// IObservableVector computed the same way.
inline constexpr char kIidVectorOfComponentModel[] = "2C559B4A-4165-5E91-86F1-CD3EC0884419";
inline constexpr const char* kIidsUInt64Collections[] = {
    "CF4A637F-456A-5A47-A927-BCF07FC69901",  // IVector<UInt64>
    "23D156C7-7EF9-5096-AABA-1E6C9AB5CEB4",  // IVectorView<UInt64>
    "4B3A3229-7995-5F3C-B248-6C1F7E664F01",  // IIterable<UInt64>
    "C473ED96-76E3-5FF2-9435-47FEEBFE9539",  // IIterator<UInt64>
};

// Microsoft.UI.Xaml.Data (WinUI 3), which Apple Music's view models implement: INotifyPropertyChanged and its
// delegate. Source: CsWinRT, src/WinRT.Runtime/Interop/IID.g.cs (IID_MUX_INotifyPropertyChanged and
// IID_MUX_PropertyChangedEventHandler).
inline constexpr char kIidNotifyPropertyChanged[] = "90B17601-B065-586E-83D9-9ADC3A695284";
inline constexpr char kIidPropertyChangedHandler[] = "E3DE52F6-1E32-5DA6-BB2D-B5B6096C962D";

// Apple's own full-screen player, in AMP.UI.Views.winmd 1.1792 (GuidAttribute values of IFullScreenPlayerStatics and
// IFullScreenPlayer). The full-screen button of Apple's mini player shows the same object. Opened by
// Ampify.ui.applePlayer (win/apple_player.cpp).
inline constexpr char kFullScreenPlayerClass[] = "AMP.UI.Views.FullScreenPlayer";
inline constexpr char kIidFullScreenPlayerStatics[] = "4D31E252-6ACE-5007-9125-9AD55E79E5E6";
inline constexpr char kIidFullScreenPlayer[] = "98034843-19EB-5B19-8A3F-A1AEDEDD9727";

namespace slot {
// AMP.UI.Views.IFullScreenPlayerStatics: Instance, Shutdown. AMP.UI.Views.IFullScreenPlayer: get_ViewModel,
// put_ViewModel, Show (no argument).
inline constexpr int kFullScreenPlayerStaticsInstance = 6;
inline constexpr int kFullScreenPlayerViewModel = 6;
inline constexpr int kFullScreenPlayerShow = 8;

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
// get_AutoPlayActive, get_Sections, add_/remove_EnginePlayQueueChanged, SetAutoPlayEnabledAsync,
// PlayPlayQueueItemAsync, MovePlayQueueItemsAsync, DeletePlayQueueItemsAsync (IVector<UInt64>, IAsyncAction).
inline constexpr int kPlayQueueStaticsCurrent = 6;
inline constexpr int kPlayQueueSections = 9;
inline constexpr int kPlayQueueDelete = 15;

// AMP.Services.IPlayQueueSection: get_SectionKind (PlayQueueSectionKind, Int32), ... CommonModels:
// IComponentModelContainer.get_Items; IPlayQueueItem: get_PlayQueueItemID (UInt64), get_Subtitle,
// get_PlayQueueSectionKind, put_PlayQueueSectionKind, get_Duration (Int64); ITitledComponent.get_Title;
// IContentDescriptorDriven.get_ContentDescriptor; IContentDescriptor: ... get_AdamId (16th, a string).
inline constexpr int kSectionKind = 6;
inline constexpr int kContainerItems = 6;
inline constexpr int kQueueItemId = 6;
inline constexpr int kQueueItemSubtitle = 7;
inline constexpr int kQueueItemDuration = 10;
inline constexpr int kTitledTitle = 6;
inline constexpr int kDrivenContentDescriptor = 6;
inline constexpr int kDescriptorAdamId = 21;
}  // namespace slot

// Every slot above with its interface and method, for the test against AMP.Services.txt and AMP.UI.Views.txt.
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
    {"AMP.Services.IPlayQueue", "DeletePlayQueueItemsAsync", slot::kPlayQueueDelete},
    {"AMP.Services.IPlayQueueSection", "get_SectionKind", slot::kSectionKind},
    {"AMP.Services.CommonModels.IComponentModelContainer", "get_Items", slot::kContainerItems},
    {"AMP.Services.CommonModels.IPlayQueueItem", "get_PlayQueueItemID", slot::kQueueItemId},
    {"AMP.Services.CommonModels.IPlayQueueItem", "get_Subtitle", slot::kQueueItemSubtitle},
    {"AMP.Services.CommonModels.IPlayQueueItem", "get_Duration", slot::kQueueItemDuration},
    {"AMP.Services.CommonModels.ITitledComponent", "get_Title", slot::kTitledTitle},
    {"AMP.Services.CommonModels.IContentDescriptorDriven", "get_ContentDescriptor", slot::kDrivenContentDescriptor},
    {"AMP.Services.CommonModels.IContentDescriptor", "get_AdamId", slot::kDescriptorAdamId},
    {"AMP.UI.Views.IFullScreenPlayerStatics", "Instance", slot::kFullScreenPlayerStaticsInstance},
    {"AMP.UI.Views.IFullScreenPlayer", "get_ViewModel", slot::kFullScreenPlayerViewModel},
    {"AMP.UI.Views.IFullScreenPlayer", "Show", slot::kFullScreenPlayerShow},
};

// AMP.Services.CommonModels.AudioFormat, in the order of its fields in AMP.Services.txt: Other, Lossless,
// HiResLossless, DolbyAtmos, Spatial, Surround. The values are taken as 0 to 5, the MIDL default for an enum without
// explicit values; the PC confirms it on a lossless and an Atmos track.
enum class AudioFormat { Other = 0, Lossless = 1, HiResLossless = 2, DolbyAtmos = 3, Spatial = 4, Surround = 5 };

}  // namespace ampify::amp
