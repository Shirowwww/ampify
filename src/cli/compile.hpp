#pragma once

// TypeScript and JSX for the people who write elements. `ampify build` and `ampify dev` call CompileSources before they
// check the folder: when the manifest's main file (main.js) has a source next to it (main.ts, main.tsx, main.jsx or
// main.mts), esbuild turns it into the main.js the kernel runs. Ampify does not ship a compiler: esbuild comes from
// the author's own `npm install --save-dev esbuild` (found in node_modules, here or in a parent folder) or from PATH.
// JSX becomes Ampify.React.createElement calls, fragments Ampify.React.Fragment.

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ampify::cli {

enum class CompileOutcome {
    NoSource,   // the folder has no TypeScript or JSX source for its main file: nothing to do
    UpToDate,   // main.js is newer than every source
    Compiled,   // main.js was written
    Failed,     // `message` says why
};

// Runs a program with its arguments and returns its exit code, or -1 when it cannot start. The commands pass
// platform::Run (see CompileElementSources in internal.hpp); the tests pass their own.
using RunProgram = std::function<int(const std::vector<std::string>& command)>;

CompileOutcome CompileSources(const std::filesystem::path& folder, std::string& message, const RunProgram& run);

}  // namespace ampify::cli
