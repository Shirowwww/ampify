// The declarations that `amk types` writes. They follow section 5 of docs/extensions/API.md. tools/test_cli.py checks
// that every member named there appears here, so the two files cannot drift apart without a failing test.

#include "internal.hpp"

namespace amk::cli {

std::string_view AmkDeclarations() {
    return R"ts(// amk.d.ts : types de l'objet global AMK (API d'extensions, kitApi 1.x).
// Suit la section 5 de docs/extensions/API.md. Les membres marqués provisoires peuvent encore changer.

type UiNode = { kind: "stack" | "row" | "text" | "button" | "toggle" | "slider" | "list" | "image" | "icon"; [prop: string]: unknown };
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
  };
};
)ts";
}

}  // namespace amk::cli
