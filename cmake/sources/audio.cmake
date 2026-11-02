# Audio for the elements: analysis (levels, spectrum, tempo), equalizer, the AudioHost capability and its engine,
# colour extraction, and the modules audioAnalysis and color of the Ampify object. See docs/extensions/api.md,
# "Organisation du code", and the convention at the top of CMakeLists.txt.
list(APPEND AMPIFY_CORE_SOURCES
    src/core/audio/analysis.cpp
    src/core/audio/equalizer.cpp
    src/core/audio/audio_engine.cpp
    src/core/audio/audio_share.cpp
    src/core/color_extract.cpp
    src/core/prelude/audio_analysis.cpp
    src/core/prelude/color.cpp
    src/core/natives/audio_analysis.cpp
    src/core/natives/color.cpp)

# The live loudness belongs to the leveler's portable library, next to the BS.1770 meter it reuses.
list(APPEND AMPIFY_ATMOS_SOURCES extensions/atmos-leveler/live_loudness.cpp)

# Ampify's processing on the leveler's sample path needs both ampify_core and atmos_leveler: it is compiled into each
# executable that links both (the kernel, the tests) rather than into either library.
list(APPEND AMPIFY_KERNEL_SOURCES extensions/atmos-leveler/kit_audio.cpp)
list(APPEND AMPIFY_TESTS_SOURCES
    extensions/atmos-leveler/kit_audio.cpp
    tests/audio_tests.cpp
    tests/audio_module_tests.cpp)
