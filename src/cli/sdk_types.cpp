// The declarations that `ampify types` writes. They follow section 5 of docs/extensions/api.md. tools/test_cli.py checks
// that every member named there appears here, so the two files cannot drift apart without a failing test.

#include "internal.hpp"

namespace ampify::cli {

std::string_view AmpifyDeclarations() {
    return R"ts(// ampify.d.ts: types of the global Ampify object (extension API, kitApi 1.x).
// Follows section 5 of docs/extensions/api.md. Members marked provisional may still change.

type TooltipProps = { tooltip?: string; tooltipPlacement?: "top" | "bottom" | "left" | "right"; tooltipDelay?: number };
type UiColor = string;  // "#RRGGBB" or "#AARRGGBB"
// Any node. align on a text is its text alignment (left, center, right).
type PlaceProps = {
  opacity?: number;     // 0 to 1
  scale?: number;       // around the centre
  transition?: string;  // "opacity 400ms", "all 300ms", "opacity 400ms, scale 300ms", "none"
  align?: "start" | "center" | "end" | "stretch";
  valign?: "start" | "center" | "end" | "stretch";
  grow?: boolean;       // takes the remaining space of a row or stack
};
// stack, row, layer and card (image: cornerRadius, width, height).
type BoxProps = {
  background?: string;  // "#RRGGBB", "#AARRGGBB", "acrylic", or "acrylic(#AARRGGBB, 0.6)": tint, tint opacity
  padding?: number | string;                  // or "top,right,bottom,left"
  cornerRadius?: number;
  width?: number;
  height?: number;
  minWidth?: number;
  maxWidth?: number;
};
type TextStyleProps = { size?: number; weight?: number; color?: UiColor; wrap?: boolean; maxLines?: number };
type UiNode = TooltipProps & Omit<PlaceProps, "align"> & BoxProps & Omit<TextStyleProps, "size"> & {
  type: "stack" | "row" | "list" | "layer" | "card" | "text" | "button" | "toggle" | "slider" | "textfield" | "image" | "icon" | "dropdown" | "option" | "lyrics";
  align?: "start" | "center" | "end" | "stretch" | "left" | "right";
  size?: number;                          // text: 8 to 120 px; icon: its size
  fit?: "cover" | "contain" | "fill";     // image
  placeholder?: string;                   // lyrics: shown while the song has none
  children?: UiNode[];
  onClick?: number;   // callback number (button, card, image, icon)
  onChange?: number;  // callback number (toggle, slider, textfield, dropdown)
  [prop: string]: unknown;
};
// active: a top bar, play bar or sidebar entry that starts on, drawn in the accent colour (Ampify.ui.setActive changes it)
type UiButton = { label: string; icon?: string; onClick: () => void; active?: boolean };
// A context entry gets the item's name and, when Apple Music's data has it, its kind and identifiers (a song of the library often has no id)
type MenuTarget = { title?: string; item?: { kind: string; id?: string; canonicalId?: string } };
type MenuEntry = { label: string; icon?: string; onClick: (target: MenuTarget) => void };
// A menu entry, or a submenu: `items` holds entries (one level, 20 at most) and the entry has no onClick of its own
type MenuItem = MenuEntry | { label: string; icon?: string; items: MenuEntry[] };
type PlayerState = {
  playing: boolean;
  positionMs: number;
  durationMs: number;
  track: {
    id: string;
    title: string;
    artist: string;
    album: string;
    variantId: string;
    atmos: boolean;
    lossless: boolean;
    queueItemId: string;  // Apple's queue entry, "" when the source does not give it
    artwork: string;       // https address of the cover (about 1000 px), "" when unknown
    artworkColor: string;  // dominant colour of the cover, "#RRGGBB", "" when unknown
  } | null;
  source: "apple" | "smtc" | "";  // "": unknown state (no source answered)
};
type PlayerEvent = "trackchange" | "statechange" | "lyricschange" | "songchange" | "onplaypause";
type LyricWord = { start: number; end: number; text: string };
type LyricLine = { start?: number; end?: number; text: string; words: LyricWord[] };  // no start/end unless synced
type Lyrics = {
  source: "apple";
  synced: boolean;  // false: plain text lines, without times
  language: string;
  lines: LyricLine[];
  text: string;
};
type QueueItem = {
  id: string;  // the entry, what queue.remove() takes
  title: string;
  artist: string;
  album: string;
  durationMs: number;
  section: "history" | "current" | "next-added" | "next" | "autoplay" | "unknown";
  catalogId: string;  // the song's Apple Music catalog id, "" for an imported file
};
type QueueState = { available: boolean; complete: boolean; source: string; reason: string; items: QueueItem[] };
type AudioFrame = { bands: number[]; rms: number; peak: number; time: number };
type Loudness = { momentary: number | null; shortTerm: number | null };
type AudioAnalysis = {
  tempo: number | null;
  confidence: number;
  loudness: Loudness;
  rms: number | null;
  peak: number | null;
  bands: number[];
  frequencies: number[];
};
type EqBand = { frequency: number; gainDb: number; q: number; type: "peaking" | "lowshelf" | "highshelf" };
type ExtractedColors = {
  PROMINENT: string;
  VIBRANT: string;
  DARK_VIBRANT: string;
  LIGHT_VIBRANT: string;
  DESATURATED: string;
  VIBRANT_NON_ALARMING: string;
  palette: { color: string; population: number }[];
};
type FontRole = { family: string; weight: number; size: number; fromTheme: boolean };

// 5.9: Ampify.React, Ampify.ReactDOM, Ampify.hooks and Ampify.components.
type AmpifyElement = { readonly type: unknown; readonly key: string | null; readonly props: Readonly<Record<string, unknown>> };
type AmpifyNode = AmpifyElement | string | number | boolean | null | undefined | AmpifyNode[];
type AmpifyComponent<P = Record<string, unknown>> = (props: P & { children?: AmpifyNode }) => AmpifyNode;
type AmpifyContext<T> = { readonly defaultValue: T; readonly Provider: object; readonly Consumer: object };
type AmpifyTarget = string | { panel: string } | { page: string } | { popup: string | true } | { fullscreen: true };
type AmpifyRoot = { render(element: AmpifyNode): null; unmount(): boolean };
type ClickEvent = { type: "click"; id: number };
type ChangeEvent = { type: "change"; id: number; value: unknown; target: { value: unknown; checked: boolean } };
type SetState<T> = (value: T | ((previous: T) => T)) => void;
type Deps = readonly unknown[];
type AmpifyHooks = {
  useState<T>(initial: T | (() => T)): [T, SetState<T>];
  useReducer<S, A>(reducer: (state: S, action: A) => S, initial: S, init?: (arg: S) => S): [S, (action: A) => void];
  useEffect(effect: () => void | (() => void), deps?: Deps): void;
  useLayoutEffect(effect: () => void | (() => void), deps?: Deps): void;
  useRef<T>(initial: T): { current: T };
  useMemo<T>(compute: () => T, deps?: Deps): T;
  useCallback<F extends (...args: never[]) => unknown>(fn: F, deps?: Deps): F;
  useContext<T>(context: AmpifyContext<T>): T;
};
type CreateElement = (type: string | symbol | object | AmpifyComponent<never>, props?: Record<string, unknown> | null,
                      ...children: AmpifyNode[]) => AmpifyElement;
type ButtonProps = TooltipProps & PlaceProps & { label?: string; icon?: string; disabled?: boolean; variant?: "primary" | "secondary" | "tertiary"; onClick?: (event: ClickEvent) => void };
type ToggleProps = TooltipProps & { label?: string; value?: boolean; checked?: boolean; disabled?: boolean; onChange?: (checked: boolean, event: ChangeEvent) => void };
type SliderProps = TooltipProps & { label?: string; min?: number; max?: number; step?: number; value?: number; disabled?: boolean; onChange?: (value: number, event: ChangeEvent) => void };
type TextFieldProps = TooltipProps & { label?: string; value?: string; placeholder?: string; disabled?: boolean; onChange?: (text: string, event: ChangeEvent) => void };
type DropdownProps = TooltipProps & { label?: string; value?: string; options?: (string | { value: string; label?: string })[]; disabled?: boolean; onChange?: (value: string, event: ChangeEvent) => void };
type CardProps = TooltipProps & PlaceProps & BoxProps & { title?: string; subtitle?: string; onClick?: (event: ClickEvent) => void };
type TextProps = TooltipProps & Omit<PlaceProps, "align"> & TextStyleProps & {
  text?: string;
  variant?: "title" | "subtitle" | "body" | "caption";
  align?: "left" | "center" | "right";
};
type TooltipWrapperProps = { label?: string; content?: string; placement?: "top" | "bottom" | "left" | "right"; delay?: number; showDelay?: number };
type DiscordActivity = {
  clientId: string; type?: "playing" | "listening" | "watching" | "competing";
  statusDisplay?: "name" | "state" | "details"; details?: string; state?: string;
  largeImage?: string; largeText?: string; smallImage?: string; smallText?: string;
  detailsUrl?: string; stateUrl?: string; largeUrl?: string; smallUrl?: string;
  buttons?: { label: string; url: string }[]; startMs?: number; endMs?: number;
};
type FormFile = { filename: string; type?: string; base64: string };
type MidiEvent = { type: "note" | "noteoff" | "cc"; channel: number; number: number; value: number };
// Dates are Unix times in milliseconds, 0 when unknown or never; durationMs is 0 when unknown.
type LibraryTrack = {
  persistentId: string;  // 16 hexadecimal digits
  title: string;
  artist: string;
  album: string;
  albumArtist: string;  // "" when unknown, as genre and composer
  genre: string;
  composer: string;
  year: number;  // 0 when unknown
  playCount: number;
  skipCount: number;
  lastPlayed: number;
  dateAdded: number;
  durationMs: number;
};

declare const Ampify: {
  // 5.1 Base [stable]
  readonly version: string;
  readonly kitApi: string;
  readonly process: "ui" | "engine";
  readonly locale: string;
  log(message: string): void;
  onStop(fn: () => void): void;

  // 5.2 Settings and storage [stable]
  config: {
    get<T = unknown>(key: string, fallback?: T): T;
    set(key: string, value: unknown): void;
  };
  storage: {
    get<T = unknown>(key: string, fallback?: T): T;
    set(key: string, value: unknown): void;
    remove(key: string): void;
  };

  // 5.3 Player [preview]
  player: {
    state(): PlayerState;
    // songchange and onplaypause are Spicetify's names for trackchange and statechange.
    on(event: PlayerEvent, fn: (state: PlayerState) => void): void;
    off(event: PlayerEvent, fn: (state: PlayerState) => void): void;  // removes a listener added with on
    lyrics(): Lyrics | null;  // Apple Music's own lyrics of the song playing, null when it has none (yet)
    artwork(): Promise<{ type: string; base64: string } | null>;  // the cover Windows shows, files of one's own included
    play(): boolean;
    pause(): boolean;
    next(): boolean;
    previous(): boolean;
    seek(ms: number): boolean;
    toggle(): boolean;
    shuffle(): boolean | null;  // Apple's shuffle button, in the interface process; null when it is not known
    repeat(): "off" | "all" | "one" | null;
    setShuffle(on: boolean): boolean;  // true once the player is in that state
    toggleShuffle(): boolean;
    setRepeat(mode: "off" | "all" | "one"): boolean;
    toggleRepeat(): boolean;  // off, all, one, off...
    setVolume(volume: number): boolean;  // Apple's player volume, 0 to 1, once a song has started; moves Apple's slider from "ui"
    queue: {
      list(): QueueState;
      add(catalogId: string | number, options?: { position?: "next" | "last" }): boolean;  // false on Windows for now
      remove(itemId: string | number): boolean;
    };
  };

  // 5.4 Sound [preview]
  audio: {
    setGainDb(db: number): number;
    onFrame(fn: (frame: AudioFrame) => void): boolean;
    analyze(): AudioAnalysis;
    loudness(): Loudness;
    volume(): number | null;
    eq: {
      setBand(index: number, gainDb: number): boolean;
      preset(name: string): boolean;
      reset(): boolean;
      bands(): EqBand[];
      presets(): string[];
    };
  };

  // 5.5 Interface [preview]
  ui: {
    topbar: { addButton(button: UiButton): number };  // the id setLabel and setActive take
    playbar: { addButton(button: UiButton): number };
    sidebar: { addItem(item: UiButton): number };  // on while its own page is open, or after setActive
    menu: { addItem(where: "context" | "app", item: MenuItem): number };
    popup: { open(view: UiNode): void };
    // permission ui.fullscreen; onClose runs when the user closes it or another element takes the screen
    fullscreen: { open(view: UiNode): void; close(): void; onClose(fn: () => void): () => void };
    // permission ui.fullscreen; Apple Music's own full-screen player, laid over the window
    applePlayer: { open(): void };
    miniPlayer: { open(): void; close(): void };
    panel: { register(id: string, view: UiNode): void; remove(id: string): boolean; onClose(id: string, fn: () => void): () => void };
    title: { set(text: string): void };
    /** A new label for a top bar or play bar button this element added: its text without an icon, its tooltip with one. */
    setLabel(id: number, label: string): boolean;
    /** Switches a top bar or play bar button this element added on or off: on, it takes the accent colour. */
    setActive(id: number, on: boolean): boolean;
    page: { register(path: string, view: UiNode): void };
    navigate(path: string): void;
  };

  // 5.6 Services [provisional]
  notify(options: { title: string; text: string }): void;
  keyboard: { bind(shortcut: string, fn: () => void): void };
  http: {
    get(url: string, options?: { headers?: Record<string, string> }): Promise<{ status: number; text: string }>;
    post(url: string, body?: string | object, options?: { headers?: Record<string, string> }): Promise<{ status: number; text: string }>;
    postForm(url: string, fields: Record<string, string | FormFile>): Promise<{ status: number; text: string }>;
    // An MP4 of a declared host as a looping GIF, made on the PC: a file for postForm.
    gifFromVideo(url: string, options?: { side?: number; fps?: number; seconds?: number }): Promise<{ type: "image/gif"; base64: string }>;
    open(url: string): boolean;  // opens the page in the browser; false within 10 seconds of the previous page
  };
  crypto: { md5(text: string): string };
  i18n: {
    readonly language: string;
    define(messages: Record<string, Record<string, string>>): void;
    t(key: string, vars?: Record<string, unknown>): string;
  };

  // 5.7 Local services [provisional]: false when the setting of the service is 0
  discord: {
    setActivity(activity: DiscordActivity): boolean;
    clear(): void;
  };
  tray: {
    setMenu(items: Array<{ label: string; onClick: () => void; checked?: boolean; enabled?: boolean } | "-">): boolean;
    onClick(fn: () => void): boolean;
  };
  midi: {
    on(binding: string, fn: (event: MidiEvent) => void): boolean;
    devices(): string[];
  };
  remote: { setState(state: Record<string, unknown>): boolean };
  fs: { writeText(name: string, text: string): boolean };
  uri: {
    parse(text: string): unknown;
    build(parts: unknown): string;
  };
  icons: { get(name: string): string; names(): string[] };
  color: {
    extract(pixels: ArrayLike<number> | ArrayBuffer, width: number, height: number): ExtractedColors;
    fromArtwork(url: string): Promise<ExtractedColors>;  // an artwork address of Apple Music, such as track.artwork
    playing(): Promise<ExtractedColors | null>;           // the cover playing now (permission player.read)
  };
  fonts: {
    roles(): Record<string, FontRole>;
    style(name: string): (FontRole & { role: string }) | null;  // "text" is another name for "body"
  };
  lyrics: { current(): Promise<Lyrics | null> };  // Ampify.player.lyrics() as a promise
  // permission library.read; rejects when Apple Music's library file cannot be read
  library: { tracks(): Promise<LibraryTrack[]> };

  // 5.8 Link between an app and its extension [provisional]
  link: {
    post(message: unknown): boolean;
    on(fn: (message: any) => void): () => void;
    connected(): boolean;
  };

  // 5.9 React, components and ready [provisional]
  readonly ready: Promise<void>;  // Ampify.ready.then(fn): fn runs when the kernel has started the element
  React: AmpifyHooks & {
    createElement: CreateElement;
    h: CreateElement;
    readonly Fragment: symbol;
    isValidElement(value: unknown): value is AmpifyElement;
    cloneElement(element: AmpifyElement, props?: Record<string, unknown> | null, ...children: AmpifyNode[]): AmpifyElement;
    Children: {
      toArray(children: AmpifyNode): AmpifyNode[];
      map<T>(children: AmpifyNode, fn: (child: AmpifyNode, index: number) => T): T[];
      forEach(children: AmpifyNode, fn: (child: AmpifyNode, index: number) => void): void;
      count(children: AmpifyNode): number;
      only(children: AmpifyNode): AmpifyElement;
    };
    createContext<T>(defaultValue: T): AmpifyContext<T>;
    memo<C>(component: C): C;
  };
  ReactDOM: {
    render(element: AmpifyNode, target: AmpifyTarget): null;
    createRoot(target: AmpifyTarget): AmpifyRoot;
    unmountComponentAtNode(target: AmpifyTarget): boolean;
  };
  hooks: AmpifyHooks & { usePlayer(): PlayerState };
  components: {
    Button: AmpifyComponent<ButtonProps>;
    ButtonPrimary: AmpifyComponent<ButtonProps>;
    ButtonSecondary: AmpifyComponent<ButtonProps>;
    ButtonTertiary: AmpifyComponent<ButtonProps>;
    Toggle: AmpifyComponent<ToggleProps>;
    Slider: AmpifyComponent<SliderProps>;
    TextField: AmpifyComponent<TextFieldProps>;
    Dropdown: AmpifyComponent<DropdownProps>;
    Card: AmpifyComponent<CardProps>;
    List: AmpifyComponent;
    Text: AmpifyComponent<TextProps>;
    TextComponent: AmpifyComponent<TextProps>;
    Tooltip: AmpifyComponent<TooltipWrapperProps>;
    TooltipWrapper: AmpifyComponent<TooltipWrapperProps>;
  };
};
)ts";
}

}  // namespace ampify::cli
