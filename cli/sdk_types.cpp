// The declarations that `amk types` writes. They follow section 5 of docs/extensions/API.md. tools/test_cli.py checks
// that every member named there appears here, so the two files cannot drift apart without a failing test.

#include "internal.hpp"

namespace amk::cli {

std::string_view AmkDeclarations() {
    return R"ts(// amk.d.ts : types de l'objet global AMK (API d'extensions, kitApi 1.x).
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
  track: { id: string; title: string; artist: string; album: string; variantId: string; atmos: boolean; lossless: boolean } | null;
};
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

// 5.8 : AMK.React, AMK.ReactDOM, AMK.hooks et AMK.components.
type AmkElement = { readonly type: unknown; readonly key: string | null; readonly props: Readonly<Record<string, unknown>> };
type AmkNode = AmkElement | string | number | boolean | null | undefined | AmkNode[];
type AmkComponent<P = Record<string, unknown>> = (props: P & { children?: AmkNode }) => AmkNode;
type AmkContext<T> = { readonly defaultValue: T; readonly Provider: object; readonly Consumer: object };
type AmkTarget = string | { panel: string } | { page: string } | { popup: string | true };
type AmkRoot = { render(element: AmkNode): null; unmount(): boolean };
type ClickEvent = { type: "click"; id: number };
type ChangeEvent = { type: "change"; id: number; value: unknown; target: { value: unknown; checked: boolean } };
type SetState<T> = (value: T | ((previous: T) => T)) => void;
type Deps = readonly unknown[];
type AmkHooks = {
  useState<T>(initial: T | (() => T)): [T, SetState<T>];
  useReducer<S, A>(reducer: (state: S, action: A) => S, initial: S, init?: (arg: S) => S): [S, (action: A) => void];
  useEffect(effect: () => void | (() => void), deps?: Deps): void;
  useLayoutEffect(effect: () => void | (() => void), deps?: Deps): void;
  useRef<T>(initial: T): { current: T };
  useMemo<T>(compute: () => T, deps?: Deps): T;
  useCallback<F extends (...args: never[]) => unknown>(fn: F, deps?: Deps): F;
  useContext<T>(context: AmkContext<T>): T;
};
type CreateElement = (type: string | symbol | object | AmkComponent<never>, props?: Record<string, unknown> | null,
                      ...children: AmkNode[]) => AmkElement;
type ButtonProps = TooltipProps & { label?: string; icon?: string; disabled?: boolean; variant?: "primary" | "secondary" | "tertiary"; onClick?: (event: ClickEvent) => void };
type ToggleProps = TooltipProps & { label?: string; value?: boolean; checked?: boolean; disabled?: boolean; onChange?: (checked: boolean, event: ChangeEvent) => void };
type SliderProps = TooltipProps & { label?: string; min?: number; max?: number; step?: number; value?: number; disabled?: boolean; onChange?: (value: number, event: ChangeEvent) => void };
type TextFieldProps = TooltipProps & { label?: string; value?: string; placeholder?: string; disabled?: boolean; onChange?: (text: string, event: ChangeEvent) => void };
type DropdownProps = TooltipProps & { label?: string; value?: string; options?: (string | { value: string; label?: string })[]; disabled?: boolean; onChange?: (value: string, event: ChangeEvent) => void };
type CardProps = TooltipProps & { title?: string; subtitle?: string; onClick?: (event: ClickEvent) => void };
type TextProps = TooltipProps & { text?: string; variant?: "title" | "subtitle" | "body" | "caption" };
type TooltipWrapperProps = { label?: string; content?: string; placement?: "top" | "bottom" | "left" | "right"; delay?: number; showDelay?: number };

declare const AMK: {
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
    play(): void;
    pause(): void;
    next(): void;
    previous(): void;
    seek(ms: number): void;
    queue: {
      add(trackId: string): void;
      remove(trackId: string): void;
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
  http: { get(url: string): Promise<{ status: number; text: string }> };
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

  // 5.7 Lien entre une app et son extension [provisoire M7]
  link: {
    post(message: unknown): boolean;
    on(fn: (message: any) => void): () => void;
    connected(): boolean;
  // 5.7 React, composants et ready [provisoire M8]
  readonly ready: Promise<void>;  // AMK.ready.then(fn) : fn tourne quand le noyau a démarré l'élément
  React: AmkHooks & {
    createElement: CreateElement;
    h: CreateElement;
    readonly Fragment: symbol;
    isValidElement(value: unknown): value is AmkElement;
    cloneElement(element: AmkElement, props?: Record<string, unknown> | null, ...children: AmkNode[]): AmkElement;
    Children: {
      toArray(children: AmkNode): AmkNode[];
      map<T>(children: AmkNode, fn: (child: AmkNode, index: number) => T): T[];
      forEach(children: AmkNode, fn: (child: AmkNode, index: number) => void): void;
      count(children: AmkNode): number;
      only(children: AmkNode): AmkElement;
    };
    createContext<T>(defaultValue: T): AmkContext<T>;
    memo<C>(component: C): C;
  };
  ReactDOM: {
    render(element: AmkNode, target: AmkTarget): null;
    createRoot(target: AmkTarget): AmkRoot;
    unmountComponentAtNode(target: AmkTarget): boolean;
  };
  hooks: AmkHooks & { usePlayer(): PlayerState };
  components: {
    Button: AmkComponent<ButtonProps>;
    ButtonPrimary: AmkComponent<ButtonProps>;
    ButtonSecondary: AmkComponent<ButtonProps>;
    ButtonTertiary: AmkComponent<ButtonProps>;
    Toggle: AmkComponent<ToggleProps>;
    Slider: AmkComponent<SliderProps>;
    TextField: AmkComponent<TextFieldProps>;
    Dropdown: AmkComponent<DropdownProps>;
    Card: AmkComponent<CardProps>;
    List: AmkComponent<TooltipProps>;
    Text: AmkComponent<TextProps>;
    TextComponent: AmkComponent<TextProps>;
    Tooltip: AmkComponent<TooltipWrapperProps>;
    TooltipWrapper: AmkComponent<TooltipWrapperProps>;
  };
};
)ts";
}

}  // namespace amk::cli
