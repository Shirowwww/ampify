// Module icons: Ampify.icons, the kit's SVG icons. JavaScript only, no natives.

namespace ampify::prelude {

extern const char kIcons[];
const char kIcons[] = R"AMPIFYJS(
  const ICONS = {
    play: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>',
    pause: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M6 5h4v14H6zM14 5h4v14h-4z"/></svg>',
    next: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M6 5l10 7-10 7zM16 5h2v14h-2z"/></svg>',
    previous: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M18 5L8 12l10 7zM6 5h2v14H6z"/></svg>',
    info: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M11 7h2v2h-2zm0 4h2v6h-2zM12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20z"/></svg>',
    heart: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M12 21l-8-8a5 5 0 0 1 8-6 5 5 0 0 1 8 6z"/></svg>',
    bell: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M12 22a2 2 0 0 0 2-2h-4a2 2 0 0 0 2 2zm6-6V11a6 6 0 0 0-5-5.9V4a1 1 0 0 0-2 0v1.1A6 6 0 0 0 6 11v5l-2 2v1h16v-1z"/></svg>',
    timer: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M9 2h6v2H9zm3 6a8 8 0 1 0 0 16 8 8 0 0 0 0-16zm1 8h-2V8h2z"/></svg>',
    drop: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M12 2C8 7 6 10 6 14a6 6 0 0 0 12 0c0-4-2-7-6-12z"/></svg>',
    puzzle: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M10 3a2 2 0 0 1 4 0v2h5v5h-2a2 2 0 0 0 0 4h2v7h-6v-2a2 2 0 0 0-4 0v2H3v-7h2a2 2 0 0 0 0-4H3V5h7z"/></svg>',
    apps: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M4 4h7v7H4zM13 4h7v7h-7zM4 13h7v7H4zM13 13h7v7h-7z"/></svg>',
    code: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M8 6l-6 6 6 6 1.4-1.4L4.8 12l4.6-4.6zm8 0l-1.4 1.4 4.6 4.6-4.6 4.6L16 18l6-6z"/></svg>',
  };

  Ampify.icons = {
    get(name) {
      if (!Object.prototype.hasOwnProperty.call(ICONS, name)) {
        throw new RangeError("unknown icon: " + name + " (" + Object.keys(ICONS).join(", ") + ")");
      }
      return ICONS[name];
    },
    names: () => Object.keys(ICONS),
  };
)AMPIFYJS";

}  // namespace ampify::prelude
