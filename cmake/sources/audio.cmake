# Audio for the elements: analysis (levels, spectrum, tempo), equalizer, the AudioHost capability and its engine, the
# portable sample path the Windows audio hooks run (gain ramp, spatial object pass, K-weighting, live loudness), the
# bars of `content: bars(n)`, colour extraction, and the modules audioAnalysis and color of the Ampify object. See the
# convention at the top of CMakeLists.txt.
list(APPEND AMPIFY_CORE_SOURCES
    src/core/audio/analysis.cpp
    src/core/audio/equalizer.cpp
    src/core/audio/audio_engine.cpp
    src/core/audio/audio_share.cpp
    src/core/audio/bar_meter.cpp
    src/core/audio/gain_ramp.cpp
    src/core/audio/k_weighting.cpp
    src/core/audio/live_loudness.cpp
    src/core/audio/object_pass.cpp
    src/core/audio/sample_path.cpp
    src/core/color_extract.cpp
    src/core/prelude/audio_analysis.cpp
    src/core/prelude/color.cpp
    src/core/natives/audio_analysis.cpp
    src/core/natives/color.cpp)

list(APPEND AMPIFY_TESTS_SOURCES
    tests/audio_tests.cpp
    tests/audio_module_tests.cpp)
