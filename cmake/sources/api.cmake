# The modules of the AMK object, built into amk_core: the registry (core/api), the JavaScript of each module
# (core/prelude) and its natives (core/natives). A new module lists its files in its own package file, such as
# cmake/sources/<package>.cmake, with list(APPEND AMK_CORE_SOURCES ...). See docs/extensions/API.md,
# "Organisation du code", and the convention at the top of CMakeLists.txt.
list(APPEND AMK_CORE_SOURCES
    core/api/modules.cpp
    core/prelude/frame.cpp
    core/prelude/core.cpp
    core/prelude/config.cpp
    core/prelude/storage.cpp
    core/prelude/player.cpp
    core/prelude/audio.cpp
    core/prelude/keyboard.cpp
    core/prelude/notify.cpp
    core/prelude/http.cpp
    core/prelude/icons.cpp
    core/prelude/uri.cpp
    core/prelude/timers.cpp
    core/prelude/console.cpp
    core/prelude/ui.cpp
    core/natives/core.cpp
    core/natives/config.cpp
    core/natives/storage.cpp
    core/natives/player.cpp
    core/natives/audio.cpp
    core/natives/keyboard.cpp
    core/natives/notify.cpp
    core/natives/http.cpp
    core/natives/timers.cpp
    core/natives/ui.cpp
    core/prelude/kit.cpp
    core/natives/kit.cpp)
