#pragma once

// What `ampify report` takes out of the files it gathers, so a person can attach the report to a public issue.

#include <string>
#include <string_view>
#include <vector>

namespace ampify::cli {

// What names this person and this computer. Empty fields are skipped.
struct Identity {
    std::vector<std::string> homes;  // profile folders, as C:\Users\name or /home/name
    std::string user;                // the account name
    std::string computer;            // the computer name
};

// The identity of the person running the command, read from the environment.
Identity CurrentIdentity();

// Masks, line by line: the profile folders and the account and computer names, e-mail addresses, artwork addresses,
// library ids, UUIDs and long hashes, the values of secret-looking URL parameters, and quoted text that names what is
// playing (the lines of the player and of extensions, and the quoted text after words such as "track" or "album").
std::string Anonymize(std::string_view text, const Identity& who);

// settings.txt with the values of the catalogue's secret keys and of secret-looking ones (an element's token, password,
// session, key...) replaced by <hidden>.
std::string HideSecretSettings(std::string_view settings);

}  // namespace ampify::cli
