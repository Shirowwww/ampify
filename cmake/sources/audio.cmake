# Audio for the elements: analysis (levels, spectrum, tempo), equalizer, the AudioHost capability and its engine,
# colour extraction, and the modules audioAnalysis and color of the AMK object. See docs/extensions/API.md,
# "Organisation du code", and the convention at the top of CMakeLists.txt.
list(APPEND AMK_CORE_SOURCES
    core/audio/analysis.cpp
    core/audio/equalizer.cpp
    core/audio/audio_engine.cpp
    core/color_extract.cpp
    core/prelude/audio_analysis.cpp
    core/prelude/color.cpp
    core/natives/audio_analysis.cpp
    core/natives/color.cpp)

# The live loudness belongs to the leveler's portable library, next to the BS.1770 meter it reuses.
list(APPEND AMK_ATMOS_SOURCES extensions/atmos-leveler/live_loudness.cpp)

# The kit's processing on the leveler's sample path needs both amk_core and atmos_leveler: it is compiled into each
# executable that links both (the kernel, the Wine harness, the tests) rather than into either library.
list(APPEND AMK_KERNEL_SOURCES extensions/atmos-leveler/kit_audio.cpp)
list(APPEND AMK_HOOK_HARNESS_SOURCES extensions/atmos-leveler/kit_audio.cpp)
list(APPEND AMK_TESTS_SOURCES
    extensions/atmos-leveler/kit_audio.cpp
    tests/audio_tests.cpp
    tests/audio_module_tests.cpp)
