// Module uri: AMK.uri, reads and writes music.apple.com links. JavaScript only, no natives.

namespace amk::prelude {

extern const char kUri[];
const char kUri[] = R"AMKJS(
  const SECTIONS = ["album", "playlist", "artist", "song", "station"];
  const LINK = /^(?:https:\/\/|music:\/\/)music\.apple\.com\/([^?#]*)(?:\?([^#]*))?/i;
  const ID = /^[A-Za-z0-9.-]+$/;

  AMK.uri = {
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
      if (!SECTIONS.includes(kind)) throw new RangeError("type inconnu : " + kind);
      if (!ID.test(text(id))) throw new RangeError("identifiant invalide : " + id);
      const storefront = (options && options.storefront) || "us";
      if (!/^[a-z]{2}$/.test(storefront)) throw new RangeError("storefront invalide : " + storefront);
      let link = "https://music.apple.com/" + storefront + "/" + kind + "/_/" + text(id);
      if (options && options.trackId !== undefined && options.trackId !== null) {
        if (!/^[0-9]+$/.test(String(options.trackId))) throw new RangeError("trackId invalide");
        link += "?i=" + String(options.trackId);
      }
      return link;
    },
  };
)AMKJS";

}  // namespace amk::prelude
