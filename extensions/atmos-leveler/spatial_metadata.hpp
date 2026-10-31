#pragma once

// The spatial audio stream Apple Music renders Dolby Atmos with: ISpatialAudioObjectRenderStreamForMetadata, from
// the Windows SDK's spatialaudiometadata.h. MinGW ships that header empty, so the parts the leveler touches are
// declared here, in the order of the SDK declaration: the vtable slots below follow from it. The leveler only
// calls through slots it hooked; the Wine harness implements these interfaces to check the slot numbers.
// Not compiled on Linux.

#include <windows.h>
#include <spatialaudioclient.h>

namespace atmos::hooks {

// {BBC9C907-48D5-4A2E-A0C7-F7F0D67C1FB1}, as declared in spatialaudiometadata.h. Field notes give the Data1
// part (0xBBC9C907) on the stream Apple Music activates.
constexpr GUID kIidRenderStreamForMetadata = {0xBBC9C907, 0x48D5, 0x4A2E, {0xA0, 0xC7, 0xF7, 0xF0, 0xD6, 0x7C, 0x1F, 0xB1}};

// ISpatialAudioObjectRenderStreamForMetadata : ISpatialAudioObjectRenderStreamBase. The base (in MinGW) holds
// slots 3 to 9: GetAvailableDynamicObjectCount, GetService, Start, Stop, Reset, BeginUpdatingAudioObjects (8),
// EndUpdatingAudioObjects (9). The two activations follow, in slots 10 and 11.
//
// The objects they return, ISpatialAudioObjectForMetadataCommands and ISpatialAudioObjectForMetadataItems, both
// derive from ISpatialAudioObjectBase, whose GetBuffer is slot 3; their own methods come after it. The leveler
// only uses GetBuffer, so they are typed as the base here.
struct ISpatialAudioObjectRenderStreamForMetadataLite : public ISpatialAudioObjectRenderStreamBase {
    virtual HRESULT STDMETHODCALLTYPE ActivateSpatialAudioObjectForMetadataCommands(
        AudioObjectType type, ISpatialAudioObjectBase** audioObject) = 0;
    virtual HRESULT STDMETHODCALLTYPE ActivateSpatialAudioObjectForMetadataItems(
        AudioObjectType type, ISpatialAudioObjectBase** audioObject) = 0;
};

// SpatialAudioObjectRenderStreamForMetadataActivationParams: the blob Apple Music passes to
// ISpatialAudioClient::ActivateSpatialAudioStream for that stream. It starts like
// SpatialAudioObjectRenderStreamActivationParams, so ObjectFormat is read the same way for both streams.
struct SpatialAudioObjectRenderStreamForMetadataActivationParamsLite {
    const WAVEFORMATEX* ObjectFormat;
    AudioObjectType StaticObjectTypeMask;
    UINT32 MinDynamicObjectCount;
    UINT32 MaxDynamicObjectCount;
    AUDIO_STREAM_CATEGORY Category;
    HANDLE EventHandle;
    GUID MetadataFormatId;
    UINT16 MaxMetadataItemCount;
    const PROPVARIANT* MetadataActivationParams;
    ISpatialAudioObjectRenderStreamNotify* NotifyObject;
};

}  // namespace atmos::hooks
