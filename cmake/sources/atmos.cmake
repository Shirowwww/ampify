# The Atmos leveler, a gain policy built into the kernel. Portable part: target atmos_leveler (the BS.1770 integrated
# meter, the track kinds, the gain policy, the levels file). Windows part: target atmos_leveler_win, the policy plugged
# into the audio platform of src/win/audio/. See the convention at the top of CMakeLists.txt.
list(APPEND AMPIFY_ATMOS_SOURCES
    extensions/atmos-leveler/loudness_meter.cpp
    extensions/atmos-leveler/format_detection.cpp
    extensions/atmos-leveler/leveler.cpp
    extensions/atmos-leveler/level_file.cpp)

list(APPEND AMPIFY_ATMOS_WIN_SOURCES extensions/atmos-leveler/atmos_windows.cpp)
