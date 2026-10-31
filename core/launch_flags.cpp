#include "launch_flags.hpp"

namespace ampify {

std::optional<std::vector<std::string>> SplitLaunchFlags(std::string_view text, std::string* error) {
    auto fail = [error](const std::string& reason) -> std::optional<std::vector<std::string>> {
        if (error) *error = reason;
        return std::nullopt;
    };
    if (text.size() > kMaxLaunchFlagsLength) {
        return fail("app_launch_flags is longer than " + std::to_string(kMaxLaunchFlagsLength) + " characters");
    }

    std::vector<std::string> arguments;
    std::string current;
    bool inArgument = false;
    bool quoted = false;
    for (char c : text) {
        unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20 && c != '\t') return fail("app_launch_flags contains a control character");
        if (byte == 0x7F) return fail("app_launch_flags contains a control character");
        if (c == '"') {
            quoted = !quoted;
            inArgument = true;
            continue;
        }
        if (!quoted && (c == ' ' || c == '\t')) {
            if (inArgument) arguments.push_back(std::move(current));
            current.clear();
            inArgument = false;
            continue;
        }
        current += c;
        inArgument = true;
    }
    if (quoted) return fail("app_launch_flags: unclosed quote");
    if (inArgument) arguments.push_back(std::move(current));
    if (arguments.size() > kMaxLaunchFlags) {
        return fail("app_launch_flags contains more than " + std::to_string(kMaxLaunchFlags) + " arguments");
    }
    return arguments;
}

std::string JoinWindowsArguments(const std::vector<std::string>& arguments) {
    std::string line;
    for (const std::string& argument : arguments) {
        if (!line.empty()) line += ' ';
        if (!argument.empty() && argument.find_first_of(" \t\"") == std::string::npos) {
            line += argument;
            continue;
        }
        line += '"';
        size_t backslashes = 0;
        for (char c : argument) {
            if (c == '\\') {
                backslashes++;
                continue;
            }
            if (c == '"') {
                line.append(backslashes * 2 + 1, '\\');
            } else {
                line.append(backslashes, '\\');
            }
            backslashes = 0;
            line += c;
        }
        line.append(backslashes * 2, '\\');  // before the closing quote
        line += '"';
    }
    return line;
}

std::string AppLaunchArguments(std::string_view setting, std::string* error) {
    std::optional<std::vector<std::string>> arguments = SplitLaunchFlags(setting, error);
    if (!arguments) return {};
    return JoinWindowsArguments(*arguments);
}

}  // namespace ampify
