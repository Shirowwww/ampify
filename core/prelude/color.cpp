// Module color: Ampify.color, the colours of an image (the counterpart of Spicetify's colorExtractor).
// Natives: core/natives/color.cpp. The kernel has no access to the cover images yet, so the script hands the
// pixels itself: RGBA bytes, row by row, with the width and the height.

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
      throw new TypeError("color.extract: images given by address are not read yet; pass the RGBA pixels, the width and the height");
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
  };
)AMPIFYJS";

}  // namespace ampify::prelude
