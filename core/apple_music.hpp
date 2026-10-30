#pragma once

#include <string>
#include <string_view>

namespace amk {

// The Microsoft Store package that holds Apple Music for Windows, and the id that starts its app.
inline constexpr std::string_view kPackageFamilyName = "AppleInc.AppleMusicWin_nzyj5cx40ttqa";
inline constexpr std::string_view kAppUserModelId = "AppleInc.AppleMusicWin_nzyj5cx40ttqa!App";

// The processes of the package that the kernel is loaded into.
enum class ProcessRole {
    Unknown,      // any other process: the kernel stays out
    AppleMusic,   // AppleMusic.exe: the WinUI 3 interface
    LibraryAgent  // AMPLibraryAgent.exe: library, playback and DRM
};

// Takes a bare file name or a full path. Case-insensitive.
ProcessRole RoleOfImageName(std::string_view imagePath);

// Version of Apple Music in a package full name, such as
// "AppleInc.AppleMusicWin_1.1540.23042.0_x64__nzyj5cx40ttqa" gives "1.1540.23042.0".
// Empty when the package is not Apple Music's or the name is malformed.
std::string AppleMusicVersion(std::string_view packageFullName);

}  // namespace amk
