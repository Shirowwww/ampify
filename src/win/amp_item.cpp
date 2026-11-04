#include "amp_item.hpp"

#include "../core/amp_abi.hpp"

namespace ampify::win {

namespace {

namespace slot = amp::slot;

// A Boolean property (one byte), false when the call fails.
bool ReadFlag(const rt::Ref& descriptor, int index) {
    unsigned char value = 0;
    return SUCCEEDED(rt::CallSlot(descriptor, index, &value)) && value != 0;
}

std::string KindOf(const rt::Ref& descriptor) {
    if (ReadFlag(descriptor, slot::kDescriptorIsPlaylist)) return "playlist";
    if (ReadFlag(descriptor, slot::kDescriptorIsAlbum)) return "album";
    if (ReadFlag(descriptor, slot::kDescriptorIsArtist)) return "artist";
    if (ReadFlag(descriptor, slot::kDescriptorIsComposer)) return "composer";
    if (ReadFlag(descriptor, slot::kDescriptorIsGenre)) return "genre";
    if (ReadFlag(descriptor, slot::kDescriptorIsRadioStation)) return "station";
    if (ReadFlag(descriptor, slot::kDescriptorIsVideo)) return "video";
    if (ReadFlag(descriptor, slot::kDescriptorIsConcert)) return "concert";
    if (ReadFlag(descriptor, slot::kDescriptorIsContainer)) return "container";
    return "song";
}

}  // namespace

rt::Ref ContentDescriptorOf(const rt::Ref& model) {
    rt::Ref driven, object, descriptor;
    if (!rt::Query(model, rt::GuidOf(amp::kIidContentDescriptorDriven), driven) ||
        FAILED(rt::CallSlot(driven, slot::kDrivenContentDescriptor, object.Put())) || !object) {
        return {};
    }
    rt::Query(object, rt::GuidOf(amp::kIidContentDescriptor), descriptor);
    return descriptor;
}

std::optional<AmpItemInfo> ReadItemInfo(const rt::Ref& data, const std::string& title) {
    rt::Ref descriptor = ContentDescriptorOf(data);
    if (!descriptor) return std::nullopt;
    if (!title.empty()) {
        rt::Ref titled;
        if (!rt::Query(data, rt::GuidOf(amp::kIidTitledComponent), titled) ||
            rt::ReadString(titled, slot::kTitledTitle) != title) {
            return std::nullopt;
        }
    }
    AmpItemInfo info;
    info.id = rt::ReadString(descriptor, slot::kDescriptorAdamId);
    info.canonicalId = rt::ReadString(descriptor, slot::kDescriptorCanonicalId);
    info.kind = KindOf(descriptor);
    return info;
}

}  // namespace ampify::win
