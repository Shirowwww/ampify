// amk: command line of the Apple Music Mod Kit. See docs/cli/COMMANDES.md.

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "commands.hpp"
#include "platform.hpp"

int main(int argc, char** argv) {
    std::vector<std::string> all = amk::platform::Initialize(argc, argv);
    std::vector<std::string> arguments(all.size() > 1 ? all.begin() + 1 : all.end(), all.end());

    std::string error;
    std::optional<amk::cli::Options> options = amk::cli::ParseOptions(arguments, error);
    if (!options) {
        std::cerr << "amk : " << error << "\nAide : amk help\n";
        return 2;
    }
    return amk::cli::Execute(*options, std::cin, std::cout, std::cerr);
}
