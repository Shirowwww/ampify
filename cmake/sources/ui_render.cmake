# The portable half of the native renderer (render trees, reconciliation, surfaces, anchors in the XAML tree, the
# Mods card files) and its tests. The Windows half (src/win/ui_renderer.cpp, src/win/ui_service.cpp, src/win/xaml_host.cpp) is
# listed in win.cmake under AMPIFY_XAML_SOURCES. See the convention at the top of CMakeLists.txt.
list(APPEND AMPIFY_CORE_SOURCES
    src/core/ui_render/render_tree.cpp
    src/core/ui_render/surfaces.cpp
    src/core/ui_render/probe.cpp
    src/core/ui_render/mods.cpp
    src/core/ui_render/apple_tweaks.cpp
    src/core/ui_render/svg_icon.cpp
    src/core/ui_render/style.cpp)

list(APPEND AMPIFY_TESTS_SOURCES tests/ui_render_tests.cpp)
