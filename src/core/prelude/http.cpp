// Module http: Ampify.http, GET and POST requests to the https hosts that the manifest declares, and their pages
// shown in the browser.
// Natives: src/core/natives/http.cpp. The request runs off the JavaScript thread: the native returns an id, and the
// promise resolves when dispatch.http gets the answer, on a later pump.

namespace ampify::prelude {

extern const char kHttp[];
const char kHttp[] = R"AMPIFYJS(
  const httpRequests = new Map();

  // `start` asks the kernel for a request and returns its id; the promise resolves with the answer dispatch.http gets.
  // A throw in `start` (URL refused, too many requests) rejects it. Ampify.color.fromArtwork goes through it too.
  dispatch.request = (start) => new Promise((resolve) => httpRequests.set(start(), resolve));

  // options.headers, lowercased: content-type, authorization, accept, origin. The kernel checks each one.
  const headersOf = (options) => {
    const headers = {};
    const given = (options && options.headers) || {};
    for (const name of Object.keys(given)) headers[name.toLowerCase()] = text(given[name]);
    return headers;
  };

  Ampify.http = {
    get(url, options = {}) {
      const headers = headersOf(options);
      return dispatch.request(() => (Object.keys(headers).length === 0
        ? native.httpGet(text(url))
        : native.httpGet(text(url), JSON.stringify(headers))));
    },
    // body: a string (sent as a form, application/x-www-form-urlencoded, unless headers say otherwise) or an
    // object (sent as JSON).
    post(url, body, options = {}) {
      return dispatch.request(() => {
        const headers = headersOf(options);
        let payload = body;
        if (payload === undefined || payload === null) {
          payload = "";
        } else if (typeof payload !== "string") {
          payload = JSON.stringify(payload);
          if (!headers["content-type"]) headers["content-type"] = "application/json";
        }
        if (!headers["content-type"]) headers["content-type"] = "application/x-www-form-urlencoded";
        return native.httpPost(text(url), payload, JSON.stringify(headers));
      });
    },
    // A multipart/form-data POST, for an upload service. fields: { name: "text", file: { filename, type, base64 } }.
    postForm(url, fields) {
      return dispatch.request(() => {
        if (typeof fields !== "object" || fields === null) throw new TypeError("postForm expects an object of fields");
        return native.httpPostForm(text(url), JSON.stringify(fields));
      });
    },
    // A short video (MP4, such as an album's motion artwork) as a GIF that loops: { type: "image/gif", base64 }, a file
    // for postForm. options: side (pixels, 200), fps (10), seconds (the start of the video, 30 at most).
    gifFromVideo(url, options = {}) {
      return dispatch.request(() => native.httpGif(text(url), Number(options.side) || 200, Number(options.fps) || 10,
                                                   Number(options.seconds) || 30))
        .then((answer) => {
          if (answer.status !== 200) throw new Error("http.gifFromVideo: " + answer.text);
          return JSON.parse(answer.text);
        });
    },
    // Shows the page in the user's browser, for a sign-in page. One page every 10 seconds at most; true if shown.
    open(url) {
      return native.httpOpen(text(url));
    },
  };

  dispatch.http = (payload) => {
    const answer = JSON.parse(payload);
    const resolve = httpRequests.get(answer.id);
    if (!resolve) return;
    httpRequests.delete(answer.id);
    resolve({ status: answer.status, text: answer.text });
  };
)AMPIFYJS";

}  // namespace ampify::prelude
