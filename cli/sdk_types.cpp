// The declarations that `ampify types` writes. They follow section 5 of docs/extensions/API.md. tools/test_cli.py checks
// that every member named there appears here, so the two files cannot drift apart without a failing test.

#include "internal.hpp"

namespace ampify::cli {

std::string_view AmpifyDeclarations() {
    return R"ts(// ampify.d.ts : types de l'objet global Ampify (API d'extensions, kitApi 1.x).
// Suit la section 5 de docs/extensions/API.md. Les membres marqués provisoires peuvent encore changer.

type TooltipProps = { tooltip?: string; tooltipPlacement?: "top" | "bottom" | "left" | "right"; tooltipDelay?: number };
type UiNode = TooltipProps & {
  type: "stack" | "row" | "list" | "card" | "text" | "button" | "toggle" | "slider" | "textfield" | "image" | "icon" | "dropdown" | "option";
  children?: UiNode[];
  onClick?: number;   // numéro de rappel (button, card, image, icon)
  onChange?: number;  // numéro de rappel (toggle, slider, textfield, dropdown)
  [prop: string]: unknown;
};
type UiButton = { label: string; icon?: string; onClick: () => void };
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
    queueItemId: string;  // entrée de la file chez Apple, "" quand la source ne la donne pas
  } | null;
  source: "apple" | "smtc" | "";  // "" : état inconnu (aucune source n'a répondu)
};
type QueueItem = {
  id: string;
  title: string;
  artist: string;
  durationMs: number;
  section: "history" | "current" | "next-added" | "next" | "autoplay" | "unknown";
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

// 5.9 : Ampify.React, Ampify.ReactDOM, Ampify.hooks et Ampify.components.
type AmpifyElement = { readonly type: unknown; readonly key: string | null; readonly props: Readonly<Record<string, unknown>> };
type AmpifyNode = AmpifyElement | string | number | boolean | null | undefined | AmpifyNode[];
type AmpifyComponent<P = Record<string, unknown>> = (props: P & { children?: AmpifyNode }) => AmpifyNode;
type AmpifyContext<T> = { readonly defaultValue: T; readonly Provider: object; readonly Consumer: object };
type AmpifyTarget = string | { panel: string } | { page: string } | { popup: string | true };
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
type ButtonProps = TooltipProps & { label?: string; icon?: string; disabled?: boolean; variant?: "primary" | "secondary" | "tertiary"; onClick?: (event: ClickEvent) => void };
type ToggleProps = TooltipProps & { label?: string; value?: boolean; checked?: boolean; disabled?: boolean; onChange?: (checked: boolean, event: ChangeEvent) => void };
type SliderProps = TooltipProps & { label?: string; min?: number; max?: number; step?: number; value?: number; disabled?: boolean; onChange?: (value: number, event: ChangeEvent) => void };
type TextFieldProps = TooltipProps & { label?: string; value?: string; placeholder?: string; disabled?: boolean; onChange?: (text: string, event: ChangeEvent) => void };
type DropdownProps = TooltipProps & { label?: string; value?: string; options?: (string | { value: string; label?: string })[]; disabled?: boolean; onChange?: (value: string, event: ChangeEvent) => void };
type CardProps = TooltipProps & { title?: string; subtitle?: string; onClick?: (event: ClickEvent) => void };
type TextProps = TooltipProps & { text?: string; variant?: "title" | "subtitle" | "body" | "caption" };
type TooltipWrapperProps = { label?: string; content?: string; placement?: "top" | "bottom" | "left" | "right"; delay?: number; showDelay?: number };
type AudioChunk = { samples: Float32Array; channels: number; sampleRate: number; frames: number };
type AudioAnalysis = { level: number; spectrum: number[]; tempo: number };
type DiscordActivity = {
  clientId: string; details?: string; state?: string; largeImage?: string; largeText?: string;
  smallImage?: string; smallText?: string; startMs?: number; endMs?: number;
};
type MidiEvent = { type: "note" | "noteoff" | "cc"; channel: number; number: number; value: number };

declare const Ampify: {
  // 5.1 Base [ferme]
  readonly version: string;
  readonly kitApi: string;
  readonly process: "ui" | "engine";
  readonly locale: string;
  log(message: string): void;
  onStop(fn: () => void): void;

  // 5.2 Réglages et stockage [ferme]
  config: {
    get<T = unknown>(key: string, fallback?: T): T;
    set(key: string, value: unknown): void;
  };
  storage: {
    get<T = unknown>(key: string, fallback?: T): T;
    set(key: string, value: unknown): void;
    remove(key: string): void;
  };

  // 5.3 Lecteur [provisoire M4]
  player: {
    state(): PlayerState;
    on(event: "trackchange" | "statechange", fn: (state: PlayerState) => void): void;
    play(): boolean;
    pause(): boolean;
    next(): boolean;
    previous(): boolean;
    seek(ms: number): boolean;
    toggle(): boolean;
    queue: {
      list(): QueueState;
      add(trackId: string | number, options?: { position?: "next" | "last" }): boolean;
      remove(itemId: string | number): boolean;
    };
  };

  // 5.4 Son [provisoire M4]
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

  // 5.5 Interface [provisoire M8]
  ui: {
    topbar: { addButton(button: UiButton): void };
    playbar: { addButton(button: UiButton): void };
    menu: { addItem(where: "context" | "app", item: { label: string; onClick: () => void }): void };
    popup: { open(view: UiNode): void };
    panel: { register(id: string, view: UiNode): void };
    title: { set(text: string): void };
    page: { register(path: string, view: UiNode): void };
    navigate(path: string): void;
  };

  // 5.6 Services [provisoire M7 et M8]
  notify(options: { title: string; text: string }): void;
  keyboard: { bind(shortcut: string, fn: () => void): void };
  http: {
    get(url: string): Promise<{ status: number; text: string }>;
    post(url: string, body?: string | object, options?: { headers?: Record<string, string> }): Promise<{ status: number; text: string }>;
  };
  crypto: { md5(text: string): string };
  i18n: {
    readonly language: "fr" | "en";
    define(messages: { fr?: Record<string, string>; en?: Record<string, string> }): void;
    t(key: string, vars?: Record<string, unknown>): string;
  };

  // 5.7 Services locaux [provisoire M7] : false quand le réglage du service est à 0
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
  icons: { get(name: string): string };
  color: {
    extract(pixels: ArrayLike<number> | ArrayBuffer, width: number, height: number): ExtractedColors;
    fromArtwork(url: string): Promise<string>;
  };
  library: {
    playlists(): Promise<unknown[]>;
    tracks(): Promise<unknown[]>;
  };
  services: { call(service: string, method: string, args?: unknown): Promise<unknown> };
  lyrics: { current(): Promise<unknown> };

  // 5.8 Lien entre une app et son extension [provisoire M7]
  link: {
    post(message: unknown): boolean;
    on(fn: (message: any) => void): () => void;
    connected(): boolean;
  // 5.9 React, composants et ready [provisoire M8]
  readonly ready: Promise<void>;  // Ampify.ready.then(fn) : fn tourne quand le noyau a démarré l'élément
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
    List: AmpifyComponent<TooltipProps>;
    Text: AmpifyComponent<TextProps>;
    TextComponent: AmpifyComponent<TextProps>;
    Tooltip: AmpifyComponent<TooltipWrapperProps>;
    TooltipWrapper: AmpifyComponent<TooltipWrapperProps>;
  };
};
)ts";
}

}  // namespace ampify::cli
