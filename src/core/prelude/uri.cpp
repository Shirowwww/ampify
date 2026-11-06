// Module uri: Ampify.uri, reads and writes music.apple.com links. JavaScript only, no natives.

namespace ampify::prelude {

extern const char kUri[];
const char kUri[] = R"AMPIFYJS(
  const SECTIONS = ["album", "playlist", "artist", "song", "station"];
  // https, and the schemes Apple Music's own links use (music:, itms:, itmss:), on music.apple.com or geo.music.apple.com.
  const LINK = /^(?:https:\/\/|music:\/\/|itmss?:\/\/)(?:geo\.)?music\.apple\.com\/([^?#]*)(?:\?([^#]*))?/i;
  const ID = /^[A-Za-z0-9.-]+$/;

  const kindOf = (input) => {
    const parsed = typeof input === "object" && input !== null ? input : Ampify.uri.parse(input);
    return parsed || { kind: null, trackId: null };
  };

  Ampify.uri = {
    parse(input) {
      const match = LINK.exec(text(input).trim());
      if (!match) return null;
      const segments = match[1].split("/").filter((segment) => segment.length > 0);
      const storefront = /^[a-z]{2}$/i.test(segments[0] || "") ? segments[0].toLowerCase() : null;
      const kind = segments[storefront ? 1 : 0];
      if (!SECTIONS.includes(kind)) return null;
      const id = segments[segments.length - 1];
      if (!id || id === kind || !ID.test(id)) return null;
      let trackId = null;
      for (const pair of (match[2] || "").split("&")) {
        const [name, value] = pair.split("=");
        if (name === "i" && /^[0-9]+$/.test(value || "")) trackId = value;
      }
      return { kind, storefront, id, trackId };
    },
    build(kind, id, options) {
      if (!SECTIONS.includes(kind)) throw new RangeError("unknown type: " + kind);
      if (!ID.test(text(id))) throw new RangeError("invalid id: " + id);
      const storefront = (options && options.storefront) || "us";
      if (!/^[a-z]{2}$/.test(storefront)) throw new RangeError("invalid storefront: " + storefront);
      let link = "https://music.apple.com/" + storefront + "/" + kind + "/_/" + text(id);
      if (options && options.trackId !== undefined && options.trackId !== null) {
        if (!/^[0-9]+$/.test(String(options.trackId))) throw new RangeError("invalid trackId");
        link += "?i=" + String(options.trackId);
      }
      return link;
    },
    // A link, or what parse gave. A song is a song link or an album link that names one of its songs (?i=); an
    // album link without one is an album.
    isSong: (input) => kindOf(input).kind === "song" || kindOf(input).trackId !== null,
    isAlbum: (input) => kindOf(input).kind === "album" && kindOf(input).trackId === null,
    isPlaylist: (input) => kindOf(input).kind === "playlist",
    isArtist: (input) => kindOf(input).kind === "artist",
    isStation: (input) => kindOf(input).kind === "station",
  };
)AMPIFYJS";

}  // namespace ampify::prelude
