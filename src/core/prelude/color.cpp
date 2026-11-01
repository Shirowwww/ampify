// Module color: Ampify.color, the colours of an image (the counterpart of Spicetify's colorExtractor).
// Natives: src/core/natives/color.cpp. extract takes pixels the script holds (RGBA bytes, row by row, with the width and
// the height); fromArtwork downloads and decodes an artwork of Apple Music in the kernel.

namespace ampify::prelude {

extern const char kColor[];
const char kColor[] = R"AMPIFYJS(
  function rgbaBytes(pixels) {
    if (pixels instanceof ArrayBuffer) return new Uint8Array(pixels);
    if (pixels instanceof Uint8Array || pixels instanceof Uint8ClampedArray) {
      return new Uint8Array(pixels.buffer, pixels.byteOffset, pixels.byteLength);
    }
    if (Array.isArray(pixels)) return Uint8Array.from(pixels);
    if (typeof pixels === "string") {
      throw new TypeError("color.extract takes RGBA pixels; for an artwork address, use color.fromArtwork");
    }
    throw new TypeError("color.extract expects RGBA pixels: array, Uint8Array or ArrayBuffer");
  }

  Ampify.color = {
    extract(pixels, width, height) {
      const bytes = rgbaBytes(pixels);
      const w = Number(width);
      const h = Number(height);
      if (!Number.isInteger(w) || !Number.isInteger(h) || w <= 0 || h <= 0) {
        throw new RangeError("color.extract expects a whole, positive width and height");
      }
      if (bytes.length !== w * h * 4) {
        throw new RangeError("color.extract: " + bytes.length + " bytes received, " + w * h * 4 + " expected (RGBA)");
      }
      const json = native.colorExtract(bytes, w, h);
      if (json === null) throw new RangeError("color.extract: image too big or fully transparent");
      return JSON.parse(json);
    },
    // An artwork address of Apple Music, such as track.artwork. Rejects with the reason when it cannot be read.
    fromArtwork(url) {
      return dispatch.request(() => native.colorFromArtwork(text(url))).then((answer) => {
        if (answer.status !== 200) throw new Error("color.fromArtwork: " + answer.text);
        return JSON.parse(answer.text);
      });
    },
  };
)AMPIFYJS";

}  // namespace ampify::prelude
