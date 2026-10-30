# Portable kernel: target amk_core. The modules of the AMK object are in api.cmake. See the convention at the top
# of CMakeLists.txt.
list(APPEND AMK_CORE_SOURCES
    core/settings.cpp
    core/journal.cpp
    core/safe_mode.cpp
    core/version_gate.cpp
    core/extension.cpp
    core/apple_music.cpp
    core/build_signature.cpp
    core/injection_policy.cpp
    core/drm_guard.cpp
    core/json.cpp
    core/manifest.cpp
    core/js_runtime.cpp
    core/script_extension.cpp
    core/extension_loader.cpp
    core/gain_board.cpp
    core/player_watch.cpp
    core/ui_model.cpp
    core/reload.cpp
    core/package_watch.cpp
    core/kit_layout.cpp
    core/run_report.cpp)
