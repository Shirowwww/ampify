# Portable kernel: target ampify_core. The modules of the Ampify object are in api.cmake. See the convention at the top
# of CMakeLists.txt.
list(APPEND AMPIFY_CORE_SOURCES
    src/core/settings.cpp
    src/core/journal.cpp
    src/core/safe_mode.cpp
    src/core/version_gate.cpp
    src/core/version_list.cpp
    src/core/launch_flags.cpp
    src/core/extension.cpp
    src/core/apple_music.cpp
    src/core/build_signature.cpp
    src/core/injection_policy.cpp
    src/core/drm_guard.cpp
    src/core/json.cpp
    src/core/manifest.cpp
    src/core/js_runtime.cpp
    src/core/script_extension.cpp
    src/core/extension_loader.cpp
    src/core/gain_board.cpp
    src/core/player_watch.cpp
    src/core/ui_model.cpp
    src/core/reload.cpp
    src/core/package_watch.cpp
    src/core/kit_layout.cpp)
