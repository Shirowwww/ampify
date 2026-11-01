// Module http: Ampify.http, GET and POST requests to the https hosts that the manifest declares.
// Natives: src/core/natives/http.cpp. The request runs off the JavaScript thread: the native returns an id, and the
// promise resolves when dispatch.http gets the answer, on a later pump.

namespace ampify::prelude {

extern const char kHttp[];
const char kHttp[] = R"AMPIFYJS(
  const httpRequests = new Map();

  // `start` asks the kernel for a request and returns its id; the promise resolves with the answer dispatch.http gets.
  // A throw in `start` (URL refused, too many requests) rejects it. Ampify.color.fromArtwork goes through it too.
  dispatch.request = (start) => new Promise((resolve) => httpRequests.set(start(), resolve));

  Ampify.http = {
    get(url) {
      return dispatch.request(() => native.httpGet(text(url)));
    },
    // body: a string (sent as a form, application/x-www-form-urlencoded, unless headers say otherwise) or an
    // object (sent as JSON). options.headers: content-type, authorization, accept.
    post(url, body, options = {}) {
      return dispatch.request(() => {
        const headers = {};
        const given = (options && options.headers) || {};
        for (const name of Object.keys(given)) headers[name.toLowerCase()] = text(given[name]);
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
