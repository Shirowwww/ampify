# The modules of the Ampify object, built into ampify_core: the registry (src/core/api), the JavaScript of each module
# (src/core/prelude) and its natives (src/core/natives). A new module lists its files in its own package file, such as
# cmake/sources/<package>.cmake, with list(APPEND AMPIFY_CORE_SOURCES ...). See docs/extensions/api.md,
# "Organisation du code", and the convention at the top of CMakeLists.txt.
list(APPEND AMPIFY_CORE_SOURCES
    src/core/api/modules.cpp
    src/core/prelude/frame.cpp
    src/core/prelude/core.cpp
    src/core/prelude/config.cpp
    src/core/prelude/storage.cpp
    src/core/prelude/player.cpp
    src/core/prelude/audio.cpp
    src/core/prelude/keyboard.cpp
    src/core/prelude/notify.cpp
    src/core/prelude/http.cpp
    src/core/prelude/icons.cpp
    src/core/prelude/uri.cpp
    src/core/prelude/timers.cpp
    src/core/prelude/console.cpp
    src/core/prelude/ui.cpp
    src/core/natives/core.cpp
    src/core/natives/config.cpp
    src/core/natives/storage.cpp
    src/core/natives/player.cpp
    src/core/natives/audio.cpp
    src/core/natives/keyboard.cpp
    src/core/natives/notify.cpp
    src/core/natives/http.cpp
    src/core/natives/timers.cpp
    src/core/natives/ui.cpp
    src/core/prelude/kit.cpp
    src/core/natives/kit.cpp)
