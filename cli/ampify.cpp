// ampify: command line of the Ampify. See docs/cli.md.

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "commands.hpp"
#include "messages.hpp"
#include "platform.hpp"

int main(int argc, char** argv) {
    std::vector<std::string> all = ampify::platform::Initialize(argc, argv);
    std::vector<std::string> arguments(all.size() > 1 ? all.begin() + 1 : all.end(), all.end());

    std::string error;
    std::optional<ampify::cli::Options> options = ampify::cli::ParseOptions(arguments, error);
    if (!options) {
        std::cerr << ampify::cli::Tr("prefix") << error << "\n" << ampify::cli::Tr("helpHint") << "\n";
        return 2;
    }
    return ampify::cli::Execute(*options, std::cin, std::cout, std::cerr);
}
