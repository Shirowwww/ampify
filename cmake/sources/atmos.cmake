# Atmos leveler, portable part: target atmos_leveler. Its Windows glue (atmos_windows.cpp) is in win.cmake. See the
# convention at the top of CMakeLists.txt.
list(APPEND AMK_ATMOS_SOURCES
    extensions/atmos-leveler/loudness_meter.cpp
    extensions/atmos-leveler/gain_ramp.cpp
    extensions/atmos-leveler/object_pass.cpp
    extensions/atmos-leveler/format_detection.cpp
    extensions/atmos-leveler/leveler.cpp
    extensions/atmos-leveler/level_file.cpp)
