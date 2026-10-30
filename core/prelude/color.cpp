// Module color: AMK.color, the colours of an image (the counterpart of Spicetify's colorExtractor).
// Natives: core/natives/color.cpp. The kernel has no access to the cover images yet, so the script hands the
// pixels itself: RGBA bytes, row by row, with the width and the height.

namespace amk::prelude {

extern const char kColor[];
const char kColor[] = R"AMKJS(
  function rgbaBytes(pixels) {
    if (pixels instanceof ArrayBuffer) return new Uint8Array(pixels);
    if (pixels instanceof Uint8Array || pixels instanceof Uint8ClampedArray) {
      return new Uint8Array(pixels.buffer, pixels.byteOffset, pixels.byteLength);
    }
    if (Array.isArray(pixels)) return Uint8Array.from(pixels);
    if (typeof pixels === "string") {
      throw new TypeError("color.extract : les images par adresse ne sont pas encore lues ; passez les pixels RVBA, la largeur et la hauteur");
    }
    throw new TypeError("color.extract attend des pixels RVBA : tableau, Uint8Array ou ArrayBuffer");
  }

  AMK.color = {
    extract(pixels, width, height) {
      const bytes = rgbaBytes(pixels);
      const w = Number(width);
      const h = Number(height);
      if (!Number.isInteger(w) || !Number.isInteger(h) || w <= 0 || h <= 0) {
        throw new RangeError("color.extract attend une largeur et une hauteur entières et positives");
      }
      if (bytes.length !== w * h * 4) {
        throw new RangeError("color.extract : " + bytes.length + " octets reçus, " + w * h * 4 + " attendus (RVBA)");
      }
      const json = native.colorExtract(bytes, w, h);
      if (json === null) throw new RangeError("color.extract : image trop grande ou entièrement transparente");
      return JSON.parse(json);
    },
  };
)AMKJS";

}  // namespace amk::prelude
