# The portable half of the native renderer (render trees, reconciliation, surfaces, anchors in the XAML tree, the
# Mods card files) and its tests. The Windows half (win/ui_renderer.cpp, win/ui_service.cpp, win/xaml_host.cpp) is
# listed in win.cmake under AMPIFY_XAML_SOURCES. See the convention at the top of CMakeLists.txt.
list(APPEND AMPIFY_CORE_SOURCES
    core/ui_render/render_tree.cpp
    core/ui_render/surfaces.cpp
    core/ui_render/probe.cpp
    core/ui_render/mods.cpp
    core/ui_render/svg_icon.cpp
    core/ui_render/style.cpp)

list(APPEND AMPIFY_TESTS_SOURCES tests/ui_render_tests.cpp)
