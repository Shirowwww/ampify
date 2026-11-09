#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "../src/core/js_runtime.hpp"
#include "support.hpp"
#include "test.hpp"

namespace {

// Evaluates `code` and returns its completion value as text, or "ERROR: ..." when it throws.
std::string EvalText(ampify::js::Runtime& runtime, const std::string& code) {
    std::string error;
    JSValue result = JS_UNDEFINED;
    if (!runtime.Eval(code, "test.js", &error, &result)) return "ERROR: " + error;
    std::string text = ampify::js::ToUtf8(runtime.context(), result, "not a string");
    if (JS_IsNumber(result)) {
        int32_t number = 0;
        JS_ToInt32(runtime.context(), &number, result);
        text = std::to_string(number);
    }
    runtime.Free(result);
    return text;
}

}  // namespace

TEST_CASE(Runtime_EvalReturnsTheCompletionValue) {
    ampify::js::Runtime runtime;
    CHECK(EvalText(runtime, "2 + 3") == "5");
    CHECK(EvalText(runtime, "'a' + 'b'") == "ab");
}

TEST_CASE(Runtime_DescribesExceptionsInOneLine) {
    ampify::js::Runtime runtime;
    std::string text = EvalText(runtime, "null.x");
    CHECK(text.find("ERROR: TypeError") == 0);
    CHECK(text.find('\n') == std::string::npos);
}

TEST_CASE(Runtime_CutsAnEntryThatRunsTooLong) {
    ampify::js::Limits limits;
    limits.budget = std::chrono::milliseconds(50);
    ampify::js::Runtime runtime(limits);
    std::string error;
    CHECK(!runtime.Eval("while (true) {}", "loop.js", &error));
    CHECK(error.find("budget") != std::string::npos);

    // The runtime is still usable afterwards: each entry gets a fresh budget.
    CHECK(EvalText(runtime, "1 + 1") == "2");
}

TEST_CASE(Runtime_StopsAHeapThatGrowsTooMuch) {
    ampify::js::Limits limits;
    limits.memoryBytes = size_t{1} << 20;
    ampify::js::Runtime runtime(limits);
    CHECK(EvalText(runtime, "var a = []; for (;;) a.push('x'.repeat(10000)); 0").find("ERROR:") == 0);
}

TEST_CASE(Runtime_TurnsDeepRecursionIntoAnError) {
    ampify::js::Limits limits;
    limits.stackBytes = size_t{64} << 10;
    ampify::js::Runtime runtime(limits);
    CHECK(EvalText(runtime, "function f() { return f() + 1; } f()").find("ERROR:") == 0);
}

TEST_CASE(Runtime_RunsPromiseJobsOnRequest) {
    ampify::js::Runtime runtime;
    CHECK(EvalText(runtime, "globalThis.x = 0; Promise.resolve(41).then((v) => { globalThis.x = v + 1; }); 0") == "0");
    std::string error;
    CHECK(runtime.RunJobs(&error));
    CHECK(EvalText(runtime, "x") == "42");
}

TEST_CASE(Runtime_HasNoFileNetworkOrProcessGlobals) {
    ampify::js::Runtime runtime;
    CHECK(EvalText(runtime, "[typeof std, typeof os, typeof require, typeof process, typeof XMLHttpRequest].join()") ==
          "undefined,undefined,undefined,undefined,undefined");
}

namespace {

namespace fs = std::filesystem;

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

}  // namespace

TEST_CASE(Modules_ResolveOnlyRelativePathsInsideTheFolder) {
    using ampify::js::ResolveModuleName;
    CHECK(ResolveModuleName("main.js", "./lib.js") == std::optional<std::string>("lib.js"));
    CHECK(ResolveModuleName("main.js", "./lib/util.mjs") == std::optional<std::string>("lib/util.mjs"));
    CHECK(ResolveModuleName("lib/util.mjs", "../shared.js") == std::optional<std::string>("shared.js"));
    CHECK(ResolveModuleName("lib/util.mjs", "./../lib/./x.js") == std::optional<std::string>("lib/x.js"));

    std::string reason;
    CHECK(!ResolveModuleName("main.js", "../outside.js", &reason));
    CHECK(reason.find("leaves the element folder") != std::string::npos);
    CHECK(!ResolveModuleName("lib/a.js", "../../outside.js"));
    CHECK(!ResolveModuleName("main.js", "/etc/passwd.js"));
    CHECK(!ResolveModuleName("main.js", "C:/x.js"));
    CHECK(!ResolveModuleName("main.js", "lodash"));
    CHECK(!ResolveModuleName("main.js", "std"));
    CHECK(!ResolveModuleName("main.js", ".\\lib.js"));
    CHECK(!ResolveModuleName("main.js", "./lib.js:stream"));
    CHECK(!ResolveModuleName("main.js", "./.hidden.js"));
    CHECK(!ResolveModuleName("main.js", "./a//b.js"));
    CHECK(!ResolveModuleName("main.js", "./data.json", &reason));
    CHECK(reason.find(".mjs") != std::string::npos);
}

TEST_CASE(Modules_RelativeImportWorks) {
    ampify::test::ScratchDir dir("modules-import");
    Write(dir / "lib.js", "export default function double(x) { return x * 2; }\nexport const name = 'lib';\n");
    Write(dir / "sub" / "util.mjs", "import double from '../lib.js';\nexport const four = double(2);\n");
    ampify::js::Runtime runtime;
    runtime.SetModuleRoot(dir);
    std::string error;
    CHECK(runtime.EvalModule("import double, { name } from './lib.js';\n"
                             "import { four } from './sub/util.mjs';\n"
                             "globalThis.result = name + ':' + double(21) + ':' + four;\n",
                             "main.js", &error));
    CHECK(error.empty());
    CHECK(EvalText(runtime, "result") == "lib:42:4");
}

TEST_CASE(Modules_DynamicImportWorksFromAClassicScript) {
    ampify::test::ScratchDir dir("modules-dynamic");
    Write(dir / "lazy.js", "export const value = 7;\n");
    ampify::js::Runtime runtime;
    runtime.SetModuleRoot(dir);
    std::string error;
    CHECK(runtime.Eval("import('./lazy.js').then((m) => { globalThis.lazy = m.value; });", "main.js", &error));
    CHECK(runtime.RunJobs(&error));
    CHECK(EvalText(runtime, "lazy") == "7");
}

TEST_CASE(Modules_AnEscapeIsRefused) {
    ampify::test::ScratchDir outer("modules-escape");
    fs::path dir = outer / "module";
    fs::create_directories(dir);
    Write(outer / "modules-escape-secret.js", "export default 'secret';\n");
    ampify::js::Runtime runtime;
    runtime.SetModuleRoot(dir);
    std::string error;
    CHECK(!runtime.EvalModule("import s from '../modules-escape-secret.js';", "main.js", &error));
    CHECK(error.find("import refused") != std::string::npos);
    CHECK(!runtime.EvalModule("import s from '/tmp/x.js';", "main2.js", &error));
    CHECK(error.find("import refused") != std::string::npos);
    CHECK(!runtime.EvalModule("import s from './absent.js';", "main3.js", &error));
    CHECK(error.find("not found") != std::string::npos);
}

TEST_CASE(Modules_ASymbolicLinkIsRefused) {
    ampify::test::ScratchDir dir("modules-link");
    ampify::test::ScratchDir outside("modules-link-target");
    Write(outside / "secret.js", "export default 'secret';\n");
    std::error_code ec;
    fs::create_symlink(outside / "secret.js", dir / "file.js", ec);
    fs::create_directory_symlink(outside, dir / "folder", ec);
    if (ec) SKIP("this account may not create symbolic links");
    ampify::js::Runtime runtime;
    runtime.SetModuleRoot(dir);
    std::string error;
    CHECK(!runtime.EvalModule("import s from './file.js';", "main.js", &error));
    CHECK(error.find("symbolic link") != std::string::npos);
    CHECK(!runtime.EvalModule("import s from './folder/secret.js';", "main2.js", &error));
    CHECK(error.find("symbolic link") != std::string::npos);
}

TEST_CASE(Modules_WithoutARootNothingIsImported) {
    ampify::js::Runtime runtime;
    std::string error;
    CHECK(!runtime.EvalModule("import x from './x.js';", "main.js", &error));
    CHECK(error.find("import refused") != std::string::npos);
}

TEST_CASE(Modules_AnExceptionInTheModuleIsReported) {
    ampify::test::ScratchDir dir("modules-throws");
    Write(dir / "bad.js", "throw new Error('module failure');\n");
    ampify::js::Runtime runtime;
    runtime.SetModuleRoot(dir);
    std::string error;
    CHECK(!runtime.EvalModule("import './bad.js';", "main.js", &error));
    CHECK(error.find("module failure") != std::string::npos);
    CHECK(!runtime.EvalModule("await Promise.reject(new TypeError('awaited'));", "main2.js", &error));
    CHECK(error.find("TypeError: awaited") != std::string::npos);
    CHECK(!runtime.EvalModule("export const = ;", "main3.js", &error));
    CHECK(error.find("SyntaxError") != std::string::npos);
}
