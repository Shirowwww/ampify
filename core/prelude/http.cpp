// Module http: Ampify.http, GET and POST requests to the https hosts that the manifest declares.
// Natives: core/natives/http.cpp.

namespace ampify::prelude {

extern const char kHttp[];
const char kHttp[] = R"AMPIFYJS(
  Ampify.http = {
    get(url) {
      return new Promise((resolve) => resolve(native.httpGet(text(url))));
    },
    // body: a string (sent as a form, application/x-www-form-urlencoded, unless headers say otherwise) or an
    // object (sent as JSON). options.headers: content-type, authorization, accept.
    post(url, body, options = {}) {
      return new Promise((resolve) => {
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
        resolve(native.httpPost(text(url), payload, JSON.stringify(headers)));
      });
    },
  };
)AMPIFYJS";

}  // namespace ampify::prelude
