// Module http: AMK.http, GET requests to the https hosts that the manifest declares. Natives: core/natives/http.cpp.

namespace amk::prelude {

extern const char kHttp[];
const char kHttp[] = R"AMKJS(
  AMK.http = {
    get(url) {
      return new Promise((resolve) => resolve(native.httpGet(text(url))));
    },
  };
)AMKJS";

}  // namespace amk::prelude
